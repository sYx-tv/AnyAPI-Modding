// ThirdPerson: switches the player camera between first and third person, with an adjustable follow distance.
//
// How: Anymaker 0.1.25 already carries a third-person camera that only its developer tools switch on. client_scene
// has m_is_camera_first_person, m_is_render_player_third_person and m_debug_third_person_camera_dist, and
// client_scene.get_camera_transform (what rendering, the crosshair and interaction rays all use) offsets the view by
// that distance when the camera is not first person. This mod hooks get_camera_transform, sets those three fields on
// the scene it is called with, and lets the game do the rest, so the player's body, aiming and interactions follow
// the game's own third-person rules. The local player's head is hidden by actor_character.render unless the
// developer global g_is_debug_render_first_person_head is set, so that is switched on while in third person too.
//
// Safety net: the first frames in third person are calibrated against the local player's facing. If the game's
// offset points forward the distance is negated; if the game does not move the camera at all, the mod moves it
// back along the view axis itself. Both cases are logged.
//
// A steady chase camera (level horizon, smoothed heading and pitch) is used on foot and replaces the seat-locked
// view in vehicles.
//
// Controls: F5 toggles (rebindable in Controls under ThirdPerson when AnyHelpers is installed); Alt + mouse wheel
// changes the follow distance while in third person. Settings: start in third person, follow distance.
//
// EXPERIMENTAL. Hooks a game function through the experimental SDK (thirdperson_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_experimental.hpp"
#include "thirdperson_bindings.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <thread>

namespace thirdperson {
namespace {
constexpr const char* kModId = "thirdperson";
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinDistance = 1.0, kMaxDistance = 20.0, kWheelStep = 0.5, kDefaultDistance = 4.0;

// client_scene fields (0.1.25 reference layout, unchanged since 0.1.24, metadata).
namespace off {
constexpr ptrdiff_t third_person_dist = 944;        // f64 m_debug_third_person_camera_dist
constexpr ptrdiff_t render_player_third = 954;      // bool m_is_render_player_third_person
constexpr ptrdiff_t camera_first_person = 955;      // bool m_is_camera_first_person
constexpr ptrdiff_t mat_t = 72;                     // mat34.t (after mat33 m = v0, v1, v2)
constexpr ptrdiff_t scene_actors = 344;             // client_scene.m_actors (client_scene.actor.container)
constexpr ptrdiff_t seated_factor = 720 + 216;      // actor_character.m_seat_data._m_seated_factor (f64, 0 standing .. 1 seated)
}  // namespace off

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
const ModControlsV1* controls;
uint64_t g_tokens[3], g_action, g_settings_revision = ~0ull;
std::atomic<bool> g_ready{false}, g_stop{false};
std::atomic<bool> g_want{false};                    // third person requested
std::atomic<double> g_target{kDefaultDistance};     // follow distance in metres
std::atomic<bool> g_steady{true};                   // level, smoothed chase camera
std::atomic<ULONGLONG> g_toast_until{0};
std::mutex g_input_mutex;
bool g_key_held;

// Local player facing, sampled by the renderer (copy_players is not called from inside the camera hook).
std::atomic<double> g_yaw{0};
std::atomic<ULONGLONG> g_yaw_time{0};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }
template <class T> void wr(uint8_t* p, ptrdiff_t o, T v) { std::memcpy(p + o, &v, sizeof v); }

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// ------------------------------------------------------------------ camera hook (main and worker threads)
using transform_t = void (*)(double* ret, uint8_t* scene);
using peer_actor_t = void (*)(uint8_t** ret, uint8_t* container);
peer_actor_t g_peer_actor;
anymaker::cell_hook h_transform;

enum class Mode { Unknown, Game, Own };
std::recursive_mutex g_cam_mutex;   // recursive: calibration calls the original while holding it
Mode g_mode = Mode::Unknown;
double g_sign = 1;                 // multiplies the distance written to the game
int g_own_axis = 2;                // Own mode: index of the mat33 basis vector pointing back, times g_own_sign
double g_own_sign = 1;
int g_calibration_tries;
uint8_t* g_scene;                  // last scene seen
bool* g_head;                      // g_is_debug_render_first_person_head, or null if not located
bool g_head_orig;
bool g_touched;                    // g_scene currently carries our third-person values
double g_scene_orig_dist;
double g_current = kDefaultDistance;
LARGE_INTEGER g_last_qpc;

double smoothed_distance() {
    LARGE_INTEGER now, freq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    double target = g_target.load();
    double dt = g_last_qpc.QuadPart ? double(now.QuadPart - g_last_qpc.QuadPart) / double(freq.QuadPart) : 0.0;
    g_last_qpc = now;
    g_current += (target - g_current) * (1.0 - std::exp(-12.0 * std::clamp(dt, 0.0, 0.25)));
    if (std::fabs(target - g_current) < 0.001) g_current = target;
    return g_current;
}

void see_scene(uint8_t* scene) {
    if (scene == g_scene) return;
    g_scene = scene; g_touched = false;   // a new world: never write to the old scene again
    g_scene_orig_dist = rd<double>(scene, off::third_person_dist);
    log(0, "scene %p: camera first person %d, render player third person %d, third-person distance %.3f", (void*)scene,
        rd<bool>(scene, off::camera_first_person), rd<bool>(scene, off::render_player_third), g_scene_orig_dist);
}

void write_third_person(uint8_t* scene, double dist) {
    wr<bool>(scene, off::camera_first_person, false);
    wr<bool>(scene, off::render_player_third, true);
    wr<double>(scene, off::third_person_dist, dist);
    if (g_head) *g_head = true;
    g_touched = true;
}
void restore_first_person(uint8_t* scene) {
    wr<bool>(scene, off::camera_first_person, true);
    wr<double>(scene, off::third_person_dist, g_scene_orig_dist);
    if (g_head) *g_head = g_head_orig;
    g_touched = false;
}

const double* basis(const double* m, int i) { return m + i * 3; }
const double* translation(const double* m) { return m + off::mat_t / 8; }
double dot(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross(const double* a, const double* b, double* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
bool normalize(double* v) { double l = std::sqrt(dot(v, v)); if (!(l > 1e-9)) return false; for (int i = 0; i < 3; ++i) v[i] /= l; return true; }
double det(const double* m) { double c[3]; cross(basis(m, 1), basis(m, 2), c); return dot(basis(m, 0), c); }

// ------------------------------------------------------------------ steady camera
// Seated, the game's view turns with the vehicle, so bumps, hills and crashes throw the camera around. The steady
// camera keeps the horizon level, follows the view's heading and pitch through a smoothing filter that gets much
// slower when the vehicle tilts or tumbles, and sits a little higher than the head when seated. On foot the same
// filter gives the chase camera the same feel.
constexpr double kWorldUp[3] = {0, 1, 0};
double g_up_world[3] = {0, 1, 0};   // the camera's "up" last frame, followed continuously through rolls
int g_up_axis_logged = -1, g_up_logs;
bool g_steady_active;           // smoothing state below is valid
double g_cam_yaw, g_cam_pitch;
LARGE_INTEGER g_steady_qpc;
int g_seat_logs;

double seated_factor(uint8_t* scene) {
    if (!g_peer_actor) return 0;
    uint8_t* actor = nullptr;
    g_peer_actor(&actor, scene + off::scene_actors);
    if (!actor || !anymaker::readable(actor + off::seated_factor, 8)) return 0;
    double f = rd<double>(actor, off::seated_factor);
    return std::isfinite(f) ? std::clamp(f, 0.0, 1.0) : 0;
}

double angle_diff(double a, double b) { double d = std::fmod(a - b + 3 * kPi, 2 * kPi) - kPi; return d; }

// head: the view at the head (third-person flags set, distance 0). back: unit vector from the head towards the
// camera, as the game's own offset points. Writes the steady camera into ret.
void steady_camera(double* ret, const double* head, const double* back, double dist, double seated) {
    LARGE_INTEGER now, freq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    double dt = g_steady_active ? std::clamp(double(now.QuadPart - g_steady_qpc.QuadPart) / double(freq.QuadPart), 0.0, 0.1) : 0;
    g_steady_qpc = now;
    if (!g_steady_active) std::memcpy(g_up_world, kWorldUp, sizeof g_up_world);
    // Which basis vector carries "back", and with which sign (the game's offset runs along one of them).
    int bi = 0; double bd = 0;
    for (int i = 0; i < 3; ++i) { double d = dot(basis(head, i), back); if (std::fabs(d) > std::fabs(bd)) { bd = d; bi = i; } }
    if (std::fabs(bd) < 0.98) {
        static bool logged; if (!logged) { logged = true; log(1, "steady camera skipped: back (%.2f, %.2f, %.2f) matches basis %d only by %.2f", back[0], back[1], back[2], bi, bd); }
        return;
    }
    // The camera's up is whichever other basis vector (and sign) is closest to last frame's up, so it follows the
    // vehicle continuously through rolls instead of guessing from a fixed axis.
    int ui = -1; double us = 1, ubest = -2;
    for (int i = 0; i < 3; ++i) {
        if (i == bi) continue;
        double d = dot(basis(head, i), g_up_world);
        if (std::fabs(d) > ubest) { ubest = std::fabs(d); ui = i; us = d >= 0 ? 1 : -1; }
    }
    double up_now[3] = {basis(head, ui)[0] * us, basis(head, ui)[1] * us, basis(head, ui)[2] * us};
    std::memcpy(g_up_world, up_now, sizeof g_up_world);
    if (ui != g_up_axis_logged && g_up_logs < 8) {
        ++g_up_logs; g_up_axis_logged = ui;
        log(0, "steady camera: back = basis %d (%+.0f), up = basis %d (%+.0f); head basis v0 (%.2f %.2f %.2f) v1 (%.2f %.2f %.2f) v2 (%.2f %.2f %.2f)",
            bi, bd > 0 ? 1.0 : -1.0, ui, us, head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], head[8]);
    }
    double tumble = 1.0 - std::max(0.0, up_now[1]);   // 0 level, 1 on its side, 2 upside down
    double f[3] = {-back[0], -back[1], -back[2]};
    double yaw = std::atan2(f[0], f[2]), pitch = std::asin(std::clamp(f[1], -1.0, 1.0));
    if (!g_steady_active) { g_cam_yaw = yaw; g_cam_pitch = std::clamp(pitch, -1.2, 1.2); g_steady_active = true; }
    double ky = 1.0 - std::exp(-dt / (0.10 + 1.28 * tumble)), kp = 1.0 - std::exp(-dt / (0.21 + 1.28 * tumble));
    g_cam_yaw += angle_diff(yaw, g_cam_yaw) * ky;
    g_cam_pitch += (std::clamp(pitch, -1.2, 1.2) - g_cam_pitch) * kp;
    double nb[3] = {-std::cos(g_cam_pitch) * std::sin(g_cam_yaw), -std::sin(g_cam_pitch), -std::cos(g_cam_pitch) * std::cos(g_cam_yaw)};
    double nu[3] = {kWorldUp[0] - dot(kWorldUp, nb) * nb[0], kWorldUp[1] - dot(kWorldUp, nb) * nb[1], kWorldUp[2] - dot(kWorldUp, nb) * nb[2]};
    if (!normalize(nu)) return;
    int ri = 3 - bi - ui;
    double out[12];
    std::memcpy(out, head, sizeof out);
    double bs = bd > 0 ? 1 : -1;
    for (int i = 0; i < 3; ++i) { out[bi * 3 + i] = nb[i] * bs; out[ui * 3 + i] = nu[i] * us; }
    double r[3]; cross(basis(out, ui), basis(out, bi), r);
    for (int i = 0; i < 3; ++i) out[ri * 3 + i] = r[i];
    if ((det(out) > 0) != (det(head) > 0)) for (int i = 0; i < 3; ++i) out[ri * 3 + i] = -r[i];
    double raise = 0.35 * seated;
    for (int i = 0; i < 3; ++i) out[9 + i] = head[9 + i] + nb[i] * dist + kWorldUp[i] * raise;
    std::memcpy(ret, out, sizeof out);
}

// Works out how the game's offset behaves, against the local player's facing. Returns true once decided.
bool calibrate(double* ret, uint8_t* scene, double dist, transform_t original) {
    ULONGLONG yaw_time = g_yaw_time.load();
    if (!yaw_time || GetTickCount64() - yaw_time > 500) return false;
    double yaw = g_yaw.load(), fx = std::sin(yaw), fz = std::cos(yaw);   // 0 = facing +Z, +pi/2 = facing +X
    double fp[12];
    wr<bool>(scene, off::camera_first_person, true);
    original(fp, scene);
    wr<bool>(scene, off::camera_first_person, false);
    const double *a = translation(fp), *b = translation(ret);
    double ox = b[0] - a[0], oy = b[1] - a[1], oz = b[2] - a[2], len = std::sqrt(ox * ox + oy * oy + oz * oz);
    if (++g_calibration_tries <= 4)
        log(0, "calibration: offset (%.2f, %.2f, %.2f) length %.2f for distance %.2f; facing yaw %.2f; first-person pos (%.2f, %.2f, %.2f)",
            ox, oy, oz, len, dist, yaw, a[0], a[1], a[2]);
    if (len < 0.05 * dist) {
        // The game kept the camera in the head. Find the basis vector that points back from the view.
        double best = 0;
        for (int i = 0; i < 3; ++i) {
            const double* v = basis(ret, i);
            double h = std::sqrt(v[0] * v[0] + v[2] * v[2]);
            if (h < 0.5) continue;   // too vertical to compare against the facing
            double d = -(v[0] * fx + v[2] * fz) / h;
            if (std::fabs(d) > std::fabs(best)) { best = d; g_own_axis = i; }
        }
        if (std::fabs(best) < 0.8) return false;   // looking too far up or down to tell; try next frame
        g_own_sign = best > 0 ? 1 : -1;
        g_mode = Mode::Own;
        log(1, "the game did not move the camera; ThirdPerson offsets it itself along basis %d (sign %+.0f)", g_own_axis, g_own_sign);
        return true;
    }
    double h = std::sqrt(ox * ox + oz * oz);
    if (h < 0.5 * len) return false;   // looking too far up or down to tell
    double along = (ox * fx + oz * fz) / h;
    if (along > 0.5) { g_sign = -1; log(1, "the game's offset pointed forward; using a negative distance"); }
    else log(0, "the game's third-person camera is behind the player; using it as is");
    g_mode = Mode::Game;
    return true;
}

void hk_transform(double* ret, uint8_t* scene) {
    auto original = reinterpret_cast<transform_t>(h_transform.original);
    if (!ret || !scene || g_stop) { original(ret, scene); return; }
    std::lock_guard lock(g_cam_mutex);
    see_scene(scene);
    if (!g_want.load()) {
        g_steady_active = false;
        if (g_touched) restore_first_person(scene);
        original(ret, scene);
        return;
    }
    if (!g_touched) g_current = std::min(kMinDistance * 1.5, g_target.load());   // ease out from behind the head
    if (!g_touched) QueryPerformanceCounter(&g_last_qpc);
    double dist = smoothed_distance();
    write_third_person(scene, g_mode == Mode::Own ? 0.0 : g_sign * dist);
    original(ret, scene);
    if (g_mode == Mode::Unknown && calibrate(ret, scene, dist, original) && g_mode == Mode::Game && g_sign < 0) {
        wr<double>(scene, off::third_person_dist, g_sign * dist);
        original(ret, scene);
    }
    if (g_mode == Mode::Own) {
        const double* v = basis(ret, g_own_axis);
        double* t = ret + off::mat_t / 8;
        for (int i = 0; i < 3; ++i) t[i] += v[i] * g_own_sign * dist;
    }
    if (g_mode == Mode::Unknown || !g_steady) { g_steady_active = false; return; }
    double seated = seated_factor(scene);
    if (g_seat_logs < 6 && ((seated > 0.5) != (g_seat_logs % 2 == 1))) { ++g_seat_logs; log(0, "seated factor %.2f: %s", seated, seated > 0.5 ? "in a vehicle" : "on foot"); }
    // The head view (distance 0) and the game's own back direction, for learning the up axis and the steady camera.
    double head[12], probe[12], back[3];
    write_third_person(scene, 0.0);
    original(head, scene);
    if (g_mode == Mode::Own) { for (int i = 0; i < 3; ++i) back[i] = basis(head, g_own_axis)[i] * g_own_sign; }
    else {
        write_third_person(scene, g_sign);
        original(probe, scene);
        for (int i = 0; i < 3; ++i) back[i] = translation(probe)[i] - translation(head)[i];
    }
    write_third_person(scene, g_mode == Mode::Own ? 0.0 : g_sign * dist);
    if (!normalize(back)) return;
    steady_camera(ret, head, back, dist, seated);
}

// ------------------------------------------------------------------ settings, input, toast
void read_settings() {
    if (!settings) return;
    uint64_t revision = settings->revision();
    if (revision == g_settings_revision) return;
    bool first = g_settings_revision == ~0ull;
    g_settings_revision = revision;
    AnySettingValueV1 v;
    if (g_tokens[1] && settings->get(g_tokens[1], &v) && std::isfinite(v.number)) g_target = std::clamp(v.number, kMinDistance, kMaxDistance);
    if (first && g_tokens[0] && settings->get(g_tokens[0], &v)) g_want = v.number != 0;
    if (g_tokens[2] && settings->get(g_tokens[2], &v)) g_steady = v.number != 0;
}

uint32_t toggle_key() {
    uint32_t k = controls && g_action ? controls->key(g_action) : 0;
    return k ? k : (controls && g_action ? 0 : VK_F5);   // unbound in Controls means no key
}

void show_toast() { g_toast_until = GetTickCount64() + 1400; }

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_input_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_key_held = false; return 0; }
    if (!g_ready) return 0;
    uint32_t key = toggle_key();
    if (e->kind == ANY_KEY_UP && key && e->key == key) { bool held = g_key_held; g_key_held = false; return held; }
    if (e->kind == ANY_KEY_DOWN && key && e->key == key) {
        if (!gameplay()) return 0;
        if (!g_key_held) {
            g_key_held = true;
            g_want = !g_want;
            show_toast();
            log(0, "camera: %s person, distance %.1f m", g_want ? "third" : "first", g_target.load());
        }
        return 1;
    }
    if (e->kind == ANY_MOUSE_WHEEL && e->wheel && g_want && (GetKeyState(VK_MENU) & 0x8000) && gameplay()) {
        double notches = double(e->wheel) / WHEEL_DELTA;
        g_target = std::clamp(g_target.load() - notches * kWheelStep, kMinDistance, kMaxDistance);
        show_toast();
        return 1;
    }
    return 0;
}

void sample_facing() {
    if (!host.copy_players) return;
    static AnySessionPlayersV1 players;   // render thread only
    uint32_t flags = 0;
    players.struct_size = sizeof players;
    if (!host.copy_players(&players, &flags)) return;
    for (uint32_t i = 0; i < players.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = players.players[i];
        if ((p.valid_fields & PLAYER_LOCAL) && (p.valid_fields & PLAYER_FACING)) {
            g_yaw = p.yaw_radians; g_yaw_time = GetTickCount64();
            return;
        }
    }
}

void draw(const AnyFrameV1* f, void*) {
    read_settings();
    if (!f || !f->focused || !gameplay()) return;
    if (g_want && g_mode == Mode::Unknown) sample_facing();
    ULONGLONG now = GetTickCount64(), until = g_toast_until.load();
    if (now >= until || !gpu) return;
    float fade = std::min(1.f, float(until - now) / 300.f), s = std::max(1.f, float(f->height) / 1080.f);
    wchar_t text[64];
    int n = g_want ? swprintf(text, 64, L"3rd person  ·  %.1f m", g_target.load()) : swprintf(text, 64, L"1st person");
    float w = 190 * s, h = 34 * s, x = (float(f->width) - w) / 2, y = float(f->height) * 0.78f;
    AnyGpuCommandV1 bg; bg.kind = ANY_GPU_ROUND_RECT; bg.rect[0] = x; bg.rect[1] = y; bg.rect[2] = w; bg.rect[3] = h;
    bg.radius = 6 * s; bg.color = 0xff191c20; bg.opacity = 0.85f * fade; gpu->emit(&bg);
    AnyGpuCommandV1 t; t.kind = ANY_GPU_TEXT; t.rect[0] = x; t.rect[1] = y; t.rect[2] = w; t.rect[3] = h;
    t.font_size = 15 * s; t.color = 0xfff5f7fa; t.opacity = fade; t.text = text; t.text_length = uint32_t(std::max(n, 0));
    t.flags = ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD; gpu->emit(&t);
}

// ------------------------------------------------------------------ install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; ThirdPerson stays off"); return; }
        void** cell = hook_cell(bind::client_scene_get_camera_transform);
        if (!cell) { if (attempt == 179) log(2, "could not locate client_scene.get_camera_transform; ThirdPerson is off"); continue; }
        if (!(g_peer_actor = reinterpret_cast<peer_actor_t>(function(bind::actor_container_get_peer_actor))))
            log(1, "local player lookup not located: the steady camera treats vehicles like walking");
        if (!h_transform.install(cell, (void*)&hk_transform)) { log(2, "the camera hook cell was already taken or not writable; ThirdPerson is off"); return; }
        if (auto head = static_cast<bool*>(global(bind::g_render_first_person_head))) {
            std::lock_guard lock(g_cam_mutex);
            g_head = head; g_head_orig = *head;
        } else log(1, "head render flag not located: your character's head may be missing in third person");
        g_ready = true;
        log(0, "Ready after %d s. Press F5 (or your Controls binding) in game to switch camera.", attempt);
        return;
    }
}
}  // namespace
}  // namespace thirdperson

using namespace thirdperson;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input;
    out->shutdown = [](void*) { g_stop = true; h_transform.remove((void*)&hk_transform); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ui_state unavailable; ThirdPerson is off"); return; }
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        ModControlActionV1 d; d.mod_id = kModId; d.mod_name = "ThirdPerson"; d.action_id = "toggle_camera";
        d.label = "Switch first / third person camera"; d.default_key = VK_F5;
        g_action = controls->register_action(&d);
    } else controls = nullptr;
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "ThirdPerson";
        d.setting_id = "start_third_person"; d.label = "Start in third person"; d.kind = ANY_SETTING_BOOL; d.order = 0;
        d.description = "F5 switches camera in game (rebind it in Controls). Hold Alt and scroll to change the distance.";
        g_tokens[0] = settings->register_setting(&d);
        d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "ThirdPerson";
        d.setting_id = "follow_distance"; d.label = "Follow distance (m)"; d.kind = ANY_SETTING_NUMBER; d.order = 1;
        d.default_number = kDefaultDistance; d.minimum = kMinDistance; d.maximum = kMaxDistance; d.step = kWheelStep;
        g_tokens[1] = settings->register_setting(&d);
        d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "ThirdPerson";
        d.setting_id = "steady_vehicle_camera"; d.label = "Steady camera"; d.kind = ANY_SETTING_BOOL; d.order = 2;
        d.default_number = 1;
        d.description = "Keeps the horizon level and smooths bumps, hills and crashes while seated. Off follows the seat exactly.";
        g_tokens[2] = settings->register_setting(&d);
        read_settings();
    } else settings = nullptr;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(1, "GPU drawing unavailable: no on-screen camera popup, and the facing check falls back to the game's offset");
    }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
