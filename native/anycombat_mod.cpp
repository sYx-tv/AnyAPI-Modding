// AnyCombat: hit markers and floating damage numbers.
//
// How: when the local player hits something, the game's client reports it to the server through
// client_peer.data.push_event_actor_take_damage (actor id, damage, hit point, surface normal, damage type). Melee
// stabs, bullets and other projectiles all go through it, so hooking it tells us exactly which hits are ours and
// where they landed, on the host and on a joining client alike. The camera comes from client_scene.get_camera (the
// exact view the frame renders with), sampled every frame, so the numbers can be pinned to the hit point in the
// world and drawn with the GPU overlay.
//
// Shows: an X hit marker around the crosshair on every hit, and a damage number that pops up at the hit point, rises
// and fades. Hits on the same target in quick succession (shotgun pellets, bursts) add up into one number. Hits on
// your own character are never shown.
//
// Kills: every target you hit while it is still up is looked up by id (client_scene.actor.container.get_actor_by_id)
// each frame for 1.5 s, and its replicated "incapacitated" flag (client_scene.actor.get_is_incapacitated, set by the
// server) is read. If it goes down inside that window, the marker flashes red and the number turns red.
//
// Settings (AnyHelpers, optional): hit marker, damage numbers, number size.
//
// EXPERIMENTAL. Hooks two game functions through the experimental SDK (anycombat_bindings.h): Anymaker 0.1.25 /
// Steam build 25904830 only. The first hits of each session are logged: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_experimental.hpp"
#include "anycombat_bindings.h"
#include "anycombat_logic.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace anycombat {
namespace {
constexpr const char* kModId = "anycombat";

// Layouts from the 0.1.25 reference (metadata).
namespace off {
constexpr ptrdiff_t cam_transform = 8;           // application.graphics.camera.m_transform (mat34: v0 right, v1 up, v2 forward, t)
constexpr ptrdiff_t cam_transform_t = 8 + 72;    // its translation
constexpr ptrdiff_t cam_transform_build = 192;   // m_transform_build (mat34), a fallback
constexpr ptrdiff_t cam_near = 104;              // m_near (f64)
constexpr ptrdiff_t cam_forward = 120;           // m_dir_forward (vec3)
constexpr ptrdiff_t cam_up = 144;                // m_dir_up
constexpr ptrdiff_t cam_right = 168;             // m_dir_right
constexpr ptrdiff_t cam_fov = 672;               // m_fov (f64, radians, vertical: get_camera uses set_perspective_fov_y)
constexpr ptrdiff_t cam_tan_half_fov = 680;      // m_tan_half_fov
constexpr size_t cam_size = 720;
constexpr ptrdiff_t scene_actors = 344;          // client_scene.m_actors (client_scene.actor.container)
constexpr ptrdiff_t actor_id = 8;                // client_scene.actor.m_id (s32)
constexpr ptrdiff_t actor_transform_t = 392 + 72; // client_scene.actor.m_transform (mat34) translation
}  // namespace off

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
uint64_t g_tokens[3], g_settings_revision = ~0ull;
std::atomic<bool> g_stop{false};

// Settings (defaults when AnyHelpers is absent).
std::atomic<bool> g_show_marker{true}, g_show_numbers{true};
std::atomic<double> g_number_size{1.0};

std::mutex g_mutex;            // camera, numbers and marker: written on the main thread, read by the renderer
Camera g_camera;
ULONGLONG g_camera_time;       // GetTickCount64 of the last camera sample
int32_t g_local_actor = -1;    // local player's actor id, refreshed with the camera
Numbers g_numbers;
KillWatch g_watch;
double g_marker_time = -1;
bool g_marker_kill;
int g_hit_logs, g_camera_logs, g_kill_logs;

LARGE_INTEGER g_qpc_freq;
double now_seconds() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) / double(g_qpc_freq.QuadPart); }

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }
Vec3 rd_vec(const uint8_t* p, ptrdiff_t o) { return {rd<double>(p, o), rd<double>(p, o + 8), rd<double>(p, o + 16)}; }

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// ------------------------------------------------------------------ hooks (main thread)
using get_camera_t = void (*)(uint8_t* ret, uint8_t* scene);
using push_damage_t = void (*)(uint8_t* data, const int32_t* actor, const double* damage, const double* position,
                               const double* normal, const int32_t* type, const int32_t* flesh);
using peer_actor_t = void (*)(uint8_t** ret, uint8_t* container);
using actor_by_id_t = void (*)(uint8_t** ret, uint8_t* container, const int32_t* id);
using incapacitated_t = void (*)(bool* ret, uint8_t* actor);
anymaker::cell_hook h_camera, h_damage;
peer_actor_t g_peer_actor;
actor_by_id_t g_actor_by_id;
incapacitated_t g_incapacitated;
uint8_t* g_scene;              // the client scene of the last camera sample (main thread only)

uint8_t* find_actor(uint8_t* scene, int32_t id) {
    if (!scene || !g_actor_by_id || id < 0) return nullptr;
    uint8_t* actor = nullptr;
    g_actor_by_id(&actor, scene + off::scene_actors, &id);
    return actor;
}

// 1 down, 0 up, -1 not found or the lookups are missing. Main thread only.
int actor_down(uint8_t* actor) {
    if (!actor || !g_incapacitated) return -1;
    bool down = false;
    g_incapacitated(&down, actor);
    return down ? 1 : 0;
}

// The actor's position in the world (client_scene.actor.m_transform), or false.
bool actor_position(uint8_t* actor, Vec3* out) {
    if (!actor || !anymaker::readable(actor + off::actor_transform_t, 24)) return false;
    Vec3 p = rd_vec(actor, off::actor_transform_t);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    *out = p;
    return true;
}

// Numbers ride along with their target: each frame, a live target's number sits at its current position plus the
// hit's offset from it (the game reports hit points relative to the actor).
void follow_targets(uint8_t* scene) {
    int32_t ids[kMaxNumbers]; int n = 0;
    {
        std::lock_guard lock(g_mutex);
        for (auto& it : g_numbers.items) if (it.used && it.actor >= 0) ids[n++] = it.actor;
    }
    for (int i = 0; i < n; ++i) {
        Vec3 p;
        if (!actor_position(find_actor(scene, ids[i]), &p)) continue;
        std::lock_guard lock(g_mutex);
        for (auto& it : g_numbers.items)
            if (it.used && it.actor == ids[i]) it.position = {p.x + it.offset.x, p.y + it.offset.y, p.z + it.offset.z};
    }
}

// Polls the targets you recently hit; a kill turns its number and the marker red.
void watch_kills(uint8_t* scene) {
    int32_t ids[kMaxWatched]; int n = 0;
    {
        std::lock_guard lock(g_mutex);
        g_watch.expire(now_seconds());
        for (auto& e : g_watch.items) if (e.used) ids[n++] = e.actor;
    }
    for (int i = 0; i < n; ++i) {
        int down = actor_down(find_actor(scene, ids[i]));
        if (down < 0) continue;   // gone (despawned) or not found: let the window run out
        double now = now_seconds();
        std::lock_guard lock(g_mutex);
        if (!g_watch.check(ids[i], down == 1, now)) continue;
        g_numbers.mark_kill(ids[i], now);
        g_marker_time = now; g_marker_kill = true;
        if (g_kill_logs < 20) { ++g_kill_logs; log(0, "kill: actor %d went down", ids[i]); }
    }
}

void hk_get_camera(uint8_t* ret, uint8_t* scene) {
    reinterpret_cast<get_camera_t>(h_camera.original)(ret, scene);
    if (g_stop || !ret || !anymaker::readable(ret, off::cam_size)) return;
    // The direction vectors are filled in later by the renderer (they read zero here), so the basis comes from
    // m_transform: v0 right, v1 up, v2 forward (ThirdPerson found the view looks along +v2), falling back to
    // m_transform_build and then the direction fields.
    Camera c;
    const char* source = "transform";
    c.position = rd_vec(ret, off::cam_transform_t);
    c.right = rd_vec(ret, off::cam_transform); c.up = rd_vec(ret, off::cam_transform + 24); c.forward = rd_vec(ret, off::cam_transform + 48);
    auto unit = [](Vec3 v) { return std::fabs(dot(v, v) - 1) < 0.05; };
    if (!unit(c.forward) || !unit(c.up) || !unit(c.right)) {
        source = "transform_build";
        c.position = rd_vec(ret, off::cam_transform_build + 72);
        c.right = rd_vec(ret, off::cam_transform_build); c.up = rd_vec(ret, off::cam_transform_build + 24); c.forward = rd_vec(ret, off::cam_transform_build + 48);
    }
    if (!unit(c.forward) || !unit(c.up) || !unit(c.right)) {
        source = "direction fields";
        c.position = rd_vec(ret, off::cam_transform_t);
        c.forward = rd_vec(ret, off::cam_forward); c.up = rd_vec(ret, off::cam_up); c.right = rd_vec(ret, off::cam_right);
    }
    c.near_plane = rd<double>(ret, off::cam_near);
    c.tan_half_fov = rd<double>(ret, off::cam_tan_half_fov);
    double fov = rd<double>(ret, off::cam_fov);
    if (!(c.tan_half_fov > 0.01 && c.tan_half_fov < 20) && fov > 0.05 && fov < 3.1) c.tan_half_fov = std::tan(fov / 2);
    int32_t local = -1;
    if (g_peer_actor && scene) {
        uint8_t* actor = nullptr;
        g_peer_actor(&actor, scene + off::scene_actors);
        if (actor && anymaker::readable(actor + off::actor_id, 4)) local = rd<int32_t>(actor, off::actor_id);
    }
    {
        std::lock_guard lock(g_mutex);
        if (g_camera_logs < 2 && c.valid()) {
            ++g_camera_logs;
            log(0, "camera (%s): position (%.2f, %.2f, %.2f) forward (%.2f, %.2f, %.2f) up (%.2f, %.2f, %.2f) right (%.2f, %.2f, %.2f) fov %.3f rad, tan(half) %.3f, near %.3f; local actor %d",
                source, c.position.x, c.position.y, c.position.z, c.forward.x, c.forward.y, c.forward.z, c.up.x, c.up.y, c.up.z,
                c.right.x, c.right.y, c.right.z, fov, c.tan_half_fov, c.near_plane, local);
        } else if (!c.valid() && g_camera_logs == 0) {
            g_camera_logs = -1;
            log(1, "camera sample looks wrong (fov %.3f, tan %.3f, |forward| %.3f): damage numbers may be misplaced", fov, c.tan_half_fov, std::sqrt(dot(c.forward, c.forward)));
        }
        if (c.valid()) { g_camera = c; g_camera_time = GetTickCount64(); }
        g_local_actor = local;
    }
    g_scene = scene;
    if (scene) { watch_kills(scene); follow_targets(scene); }
}

void hk_push_damage(uint8_t* data, const int32_t* actor, const double* damage, const double* position, const double* normal,
                    const int32_t* type, const int32_t* flesh) {
    reinterpret_cast<push_damage_t>(h_damage.original)(data, actor, damage, position, normal, type, flesh);
    if (g_stop || !actor || !damage || !position || !type) return;
    double dmg = *damage;
    if (!std::isfinite(dmg) || dmg <= 0) return;
    Vec3 at{position[0], position[1], position[2]};
    if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return;
    int32_t id = *actor;
    uint8_t* target = find_actor(g_scene, id);   // same main thread as the camera hook
    int down = actor_down(target);
    Vec3 base{}, world = at;
    bool placed = actor_position(target, &base);
    if (placed) world = {base.x + at.x, base.y + at.y, base.z + at.z};
    std::lock_guard lock(g_mutex);
    bool self = g_local_actor >= 0 && id == g_local_actor;
    if (g_hit_logs < 20) {
        ++g_hit_logs;
        Vec3 d = sub(world, g_camera.position);
        log(0, "hit: actor %d%s, damage %.3f, type %d, flesh %d, offset (%.2f, %.2f, %.2f), actor at (%.2f, %.2f, %.2f)%s, camera at (%.2f, %.2f, %.2f), %.1f m away, %s",
            id, self ? " (you, not shown)" : "", dmg, *type, flesh ? *flesh : -1, at.x, at.y, at.z, base.x, base.y, base.z,
            placed ? "" : " (not found)", g_camera.position.x, g_camera.position.y, g_camera.position.z, std::sqrt(dot(d, d)),
            down == 1 ? "already down" : down == 0 ? "up" : "not found");
    }
    if (self) return;
    double now = now_seconds();
    if (placed) g_numbers.add(id, dmg, world, at, *type, now);   // without the target there is nowhere to pin it
    if (down == 0) g_watch.hit(id, now);
    g_marker_time = now;
    g_marker_kill = false;
}

// ------------------------------------------------------------------ settings
void read_settings() {
    if (!settings) return;
    uint64_t revision = settings->revision();
    if (revision == g_settings_revision) return;
    g_settings_revision = revision;
    AnySettingValueV1 v;
    if (g_tokens[0] && settings->get(g_tokens[0], &v)) g_show_marker = v.number != 0;
    if (g_tokens[1] && settings->get(g_tokens[1], &v)) g_show_numbers = v.number != 0;
    if (g_tokens[2] && settings->get(g_tokens[2], &v) && std::isfinite(v.number)) g_number_size = std::clamp(v.number, 0.5, 2.5);
}

// ------------------------------------------------------------------ drawing (render thread)
void line(float x0, float y0, float x1, float y1, float width, uint32_t color, float opacity) {
    AnyGpuCommandV1 c; c.kind = ANY_GPU_LINE; c.rect[0] = x0; c.rect[1] = y0; c.rect[2] = x1; c.rect[3] = y1;
    c.stroke = width; c.color = color; c.opacity = opacity; gpu->emit(&c);
}

void text(const wchar_t* s, int n, float cx, float cy, float size, uint32_t color, float opacity) {
    float w = size * 6, h = size * 1.6f;
    AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.text = s; c.text_length = uint32_t(n); c.font_size = size;
    c.flags = ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD;
    // Shadow first, offset by about a pixel at 1080p, so numbers read against bright sky and snow.
    float o = std::max(1.f, size / 14);
    c.rect[0] = cx - w / 2 + o; c.rect[1] = cy - h / 2 + o; c.rect[2] = w; c.rect[3] = h; c.color = 0xff000000; c.opacity = opacity * 0.75f;
    gpu->emit(&c);
    c.rect[0] = cx - w / 2; c.rect[1] = cy - h / 2; c.color = color; c.opacity = opacity;
    gpu->emit(&c);
}

void draw(const AnyFrameV1* f, void*) {
    read_settings();
    if (!f || !f->focused || !gameplay() || !gpu) return;
    double now = now_seconds();
    float W = float(f->width), H = float(f->height), s = std::max(0.75f, H / 1080.f);
    Camera cam; bool cam_ok; Number numbers[kMaxNumbers]; double marker_time; bool marker_kill;
    {
        std::lock_guard lock(g_mutex);
        g_numbers.expire(now);
        cam = g_camera; cam_ok = g_camera_time && GetTickCount64() - g_camera_time < 250;
        std::memcpy(numbers, g_numbers.items, sizeof numbers);
        marker_time = g_marker_time; marker_kill = g_marker_kill;
    }
    if (g_show_numbers && cam_ok) {
        double size = 22.0 * s * g_number_size.load();
        for (const auto& n : numbers) {
            if (!n.used) continue;
            double age = now - n.born, sx, sy;
            if (!project(cam, n.position, W, H, &sx, &sy)) continue;
            sy -= number_rise(age) * kRiseFraction * H;
            if (sx < -100 || sx > W + 100 || sy < -100 || sy > H + 100) continue;
            wchar_t buf[24]; int len = format_damage(n.damage, buf, 24);
            double scale = number_scale(age) * (n.kill ? 1.25 : 1.0);
            text(buf, len, float(sx), float(sy), float(size * scale), damage_color(n.kill), float(number_alpha(age)));
        }
    }
    if (g_show_marker && marker_time >= 0) {
        double age = now - marker_time, a = marker_alpha(age);
        if (a > 0) {
            float cx = W / 2, cy = H / 2, spread = float(marker_spread(age));
            float grow = marker_kill ? 1.3f : 1.f;
            float r0 = 7 * s * spread * grow, r1 = 15 * s * spread * grow, w = 2.2f * s * grow;
            uint32_t color = damage_color(marker_kill);
            for (int i = 0; i < 4; ++i) {
                float dx = (i & 1) ? 1.f : -1.f, dy = (i & 2) ? 1.f : -1.f;
                line(cx + dx * r0 + 1, cy + dy * r0 + 1, cx + dx * r1 + 1, cy + dy * r1 + 1, w + 1, 0xff000000, float(a) * 0.6f);
                line(cx + dx * r0, cy + dy * r0, cx + dx * r1, cy + dy * r1, w, color, float(a));
            }
        }
    }
}

// ------------------------------------------------------------------ install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    bool camera_done = false, damage_done = false;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyCombat stays off"); return; }
        if (!g_peer_actor) g_peer_actor = reinterpret_cast<peer_actor_t>(function(bind::actor_container_get_peer_actor));
        if (!g_actor_by_id) g_actor_by_id = reinterpret_cast<actor_by_id_t>(function(bind::actor_container_get_actor_by_id));
        if (!g_incapacitated) g_incapacitated = reinterpret_cast<incapacitated_t>(function(bind::actor_get_is_incapacitated));
        if (!camera_done) {
            if (void** cell = hook_cell(bind::client_scene_get_camera)) {
                if (!h_camera.install(cell, (void*)&hk_get_camera)) { log(2, "the camera hook cell was not writable; AnyCombat is off"); return; }
                camera_done = true;
            }
        }
        if (!damage_done) {
            if (void** cell = hook_cell(bind::push_event_actor_take_damage)) {
                if (!h_damage.install(cell, (void*)&hk_push_damage)) { log(2, "the hit report hook cell was not writable; AnyCombat is off"); return; }
                damage_done = true;
            }
        }
        if (camera_done && damage_done) {
            if (!g_peer_actor) log(1, "local player lookup not located: hits on your own character may show numbers");
            if (!g_actor_by_id || !g_incapacitated) log(1, "actor lookup or incapacitated flag not located: kills are not marked red");
            log(0, "Ready after %d s: hit markers and damage numbers are on.", attempt);
            return;
        }
    }
    if (!g_stop) log(2, "could not locate %s%s%s; AnyCombat is off", camera_done ? "" : "client_scene.get_camera",
                     !camera_done && !damage_done ? " and " : "", damage_done ? "" : "client_peer.data.push_event_actor_take_damage");
}
}  // namespace
}  // namespace anycombat

using namespace anycombat;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    QueryPerformanceFrequency(&g_qpc_freq);
    out->shutdown = [](void*) { g_stop = true; h_damage.remove((void*)&hk_push_damage); h_camera.remove((void*)&hk_get_camera); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ui_state unavailable; AnyCombat is off"); return; }
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(2, "GPU drawing unavailable; AnyCombat is off"); return;
    }
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyCombat";
        d.setting_id = "hit_marker"; d.label = "Hit marker"; d.kind = ANY_SETTING_BOOL; d.order = 0; d.default_number = 1;
        d.description = "An X around the crosshair whenever you hit something. Red when the hit kills.";
        g_tokens[0] = settings->register_setting(&d);
        d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyCombat";
        d.setting_id = "damage_numbers"; d.label = "Damage numbers"; d.kind = ANY_SETTING_BOOL; d.order = 1; d.default_number = 1;
        d.description = "The damage you deal pops up where the hit landed. Quick hits on one target add up; red on a kill.";
        g_tokens[1] = settings->register_setting(&d);
        d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyCombat";
        d.setting_id = "number_size"; d.label = "Number size"; d.kind = ANY_SETTING_NUMBER; d.order = 2;
        d.default_number = 1.0; d.minimum = 0.5; d.maximum = 2.5; d.step = 0.1;
        g_tokens[2] = settings->register_setting(&d);
        read_settings();
    } else settings = nullptr;
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
