#pragma once
// AnyGraphics photo mode: free camera and photo controls on top of the hidden HUD.
//
// Free camera: hooks client_scene.get_camera_transform through the experimental SDK (the same binding ThirdPerson
// uses, so one regeneration after a game update covers both). The game still turns the view with the mouse, so the
// camera keeps the game's rotation and only its position is replaced; it starts exactly where the view was. Movement
// keys and mouse buttons are consumed while it flies, so the character stays put and cannot act at a distance. The
// camera stays within kRange of where it started, because the world is only loaded around the player.
//
// Photo controls change the scene effects only while photo mode is on and are not saved. Depth of field focuses on
// the middle of the screen, so aim at the subject.
//
// EXPERIMENTAL (camera only): Anymaker 0.1.25 / Steam build 25904830. On any other build the camera is unavailable
// and photo mode still hides the HUD and applies the photo controls.
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_experimental.hpp"
#include "thirdperson_bindings.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <thread>
namespace graphics::photocam {
inline constexpr double kRange = 200, kMinSpeed = .25, kMaxSpeed = 40, kDefaultSpeed = 4;
inline constexpr const wchar_t* kLooks[] = {L"None", L"Warm", L"Cool", L"Teal & orange", L"Moody", L"Vivid"};
inline constexpr const wchar_t* kCorners[] = {L"Off", L"Light", L"Medium", L"Strong"};
inline constexpr const wchar_t* kGlows[] = {L"Off", L"Subtle", L"Normal", L"Strong"};

// -1 keeps the preset's own value. dof is in half steps of depth_of_field (0..8 = 0..4).
struct Tweaks { int dof = -1, look = -1, vignette = -1, bloom = -1; float exposure = 0; };
inline std::mutex mutex;            // tweaks, free, pinned, shown_until
inline Tweaks tweaks;
inline bool free_camera = true, pinned;
inline ULONGLONG shown_until;
inline std::atomic<uint32_t> revision{0};
inline std::atomic<bool> active{false};       // photo mode on and in gameplay: input and panel
inline std::atomic<bool> flying{false};       // photo mode on with the free camera: camera hook
inline std::atomic<float> speed{float(kDefaultSpeed)};
inline AnyModHostV1 host;
inline const AnyGpuDrawV1* gpu;
inline void log(const char* text) { if (host.log) host.log(0, "anygraphics", text); }
inline void show(ULONGLONG ms = 4000) { shown_until = GetTickCount64() + ms; }
inline void changed() { show(); revision.fetch_add(1); }

// Applies the photo controls to the parameters about to be sent, after photo mode's own setup.
inline void apply(AnySceneEffectsParametersV1& e) {
    std::lock_guard lock(mutex);
    const Tweaks& t = tweaks;
    bool finish = t.look >= 0 || t.vignette >= 0 || t.bloom >= 0 || t.exposure != 0;
    if (finish && !e.finish) { e.finish = 1; e.tonemap = 0; e.eye_adaptation = 0; e.look = 0; e.vignette = 0; }
    if (t.dof >= 0) e.depth_of_field = float(t.dof) * .5f;
    if (t.look >= 0) { e.look = uint32_t(t.look); if (e.look_strength < .5f) e.look_strength = 1; }
    if (t.vignette >= 0) e.vignette = graphics_options::vignette_levels[t.vignette];
    if (t.bloom >= 0) e.bloom = graphics_options::scene_bloom_levels[t.bloom];
    e.exposure = std::clamp(e.exposure + t.exposure, -3.f, 3.f);
}

// ------------------------------------------------------------------ camera (main and worker threads)
using transform_t = void (*)(double* ret, uint8_t* scene);
inline anymaker::cell_hook hook;
inline std::atomic<bool> hook_ready{false}, stop{false}, have{false};
// g_is_debug_render_first_person_head: the game hides the local player's head unless it is set, which looks
// headless from a free camera. Set while flying, then put back to the game's value from before any mod set it
// (ThirdPerson sets it the same way and keeps setting it each frame while in third person).
inline bool* head;
inline bool head_set, head_original;
enum : uint32_t { Forward = 1, Back = 2, Left = 4, Right = 8, Up = 16, Down = 32, Fast = 64 };
inline std::atomic<uint32_t> keys{0};
inline std::atomic<double> facing{0};
inline std::atomic<ULONGLONG> facing_time{0};
inline std::mutex cam_mutex;
inline uint8_t* camera_scene;   // the client scene the camera was placed in
inline double position[3], origin[3];
inline LARGE_INTEGER last_move;
inline int forward_axis = -1;
inline ULONGLONG calibration_start;
inline double forward_sign = 1;

inline double dot3(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Which basis vector of the camera transform points where the player looks, from the local player's facing.
inline void calibrate(const double* m) {
    ULONGLONG at = facing_time.load(), now = GetTickCount64();
    if (!calibration_start) calibration_start = now;
    if (now - calibration_start > 3000) {
        forward_axis = 0; forward_sign = 1;
        log("PHOTO_CAMERA forward=basis0 calibrated=0 (no facing; using the renderer's layout)");
        return;
    }
    if (!at || now - at > 500) return;
    double fx = std::sin(facing.load()), fz = std::cos(facing.load()), best = 0;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        const double* v = m + i * 3;
        double h = std::sqrt(v[0] * v[0] + v[2] * v[2]);
        if (h < .5) continue;   // too vertical to compare
        double d = (v[0] * fx + v[2] * fz) / h;
        if (std::fabs(d) > std::fabs(best)) { best = d; axis = i; }
    }
    if (axis < 0 || std::fabs(best) < .8) return;   // looking steeply up or down, or the view is turned away
    forward_axis = axis; forward_sign = best > 0 ? 1 : -1;
    char line[96]; snprintf(line, sizeof line, "PHOTO_CAMERA forward=basis%d sign=%+.0f match=%.2f", axis, forward_sign, std::fabs(best));
    log(line);
}

inline void hk_transform(double* ret, uint8_t* scene) {
    reinterpret_cast<transform_t>(hook.original)(ret, scene);
    if (!ret || stop.load()) return;
    if (!flying.load()) { if (have.load()) { std::lock_guard lock(cam_mutex); have = false; } return; }
    std::lock_guard lock(cam_mutex);
    double* t = ret + 9;
    LARGE_INTEGER now, rate; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&rate);
    if (!have.load() || scene != camera_scene) {   // a new world starts the camera at its own view
        for (int i = 0; i < 3; ++i) position[i] = origin[i] = t[i];
        last_move = now; have = true; camera_scene = scene;
    }
    if (forward_axis < 0) calibrate(ret);
    // The position only moves when at least 2 ms have passed, so every call within one frame returns the same
    // camera. ThirdPerson's steady camera calls this several times per frame and takes the difference between
    // calls as its "back" direction; a camera that crept between those calls spun its view like a tornado.
    double dt = std::clamp(double(now.QuadPart - last_move.QuadPart) / double(rate.QuadPart), 0.0, 0.1);
    uint32_t k = keys.load();
    if (dt >= .002) last_move = now; else k = 0;
    if (k && forward_axis >= 0) {
        double f[3], r[3];
        for (int i = 0; i < 3; ++i) f[i] = ret[forward_axis * 3 + i] * forward_sign;
        // Screen right. On 0.1.25 forward calibrates to basis 2 and right = up x forward (tested in game). The
        // basis-0 path is the renderer's forward/up/right layout with the sign flipped to match; it is unverified.
        if (forward_axis == 0 && forward_sign > 0) for (int i = 0; i < 3; ++i) r[i] = -ret[6 + i];
        else { r[0] = f[2]; r[1] = 0; r[2] = -f[0]; }
        double rl = std::sqrt(dot3(r, r)); if (rl > 1e-6) for (double& x : r) x /= rl;
        double a = double(!!(k & Forward)) - double(!!(k & Back)), s = double(!!(k & Right)) - double(!!(k & Left)), u = double(!!(k & Up)) - double(!!(k & Down));
        double v[3] = {f[0] * a + r[0] * s, f[1] * a + r[1] * s + u, f[2] * a + r[2] * s};
        double len = std::sqrt(dot3(v, v));
        if (len > 1e-6) {
            double step = speed.load() * (k & Fast ? 4.0 : 1.0) * dt / len;
            for (int i = 0; i < 3; ++i) position[i] += v[i] * step;
            double d[3] = {position[0] - origin[0], position[1] - origin[1], position[2] - origin[2]}, dl = std::sqrt(dot3(d, d));
            if (dl > kRange) for (int i = 0; i < 3; ++i) position[i] = origin[i] + d[i] * (kRange / dl);
        }
    }
    for (int i = 0; i < 3; ++i) t[i] = position[i];
}

inline void install() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log("PHOTO_CAMERA unavailable=1 reason=game_build"); return; }
        void** cell = hook_cell(thirdperson::bind::client_scene_get_camera_transform);
        if (!cell) { if (attempt == 179) log("PHOTO_CAMERA unavailable=1 reason=not_located"); continue; }
        if (!hook.install(cell, (void*)&hk_transform)) { log("PHOTO_CAMERA unavailable=1 reason=hook_taken"); return; }
        if (auto flag = static_cast<bool*>(global(thirdperson::bind::g_render_first_person_head))) { head_original = *flag; head = flag; }
        else log("PHOTO_CAMERA head_flag=0 (your character may look headless from the camera)");
        hook_ready = true;
        log("PHOTO_CAMERA ready=1");
        return;
    }
}
inline void shutdown() { stop = true; hook.remove((void*)&hk_transform); }

// Local player facing for calibration (render thread).
inline void sample_facing() {
    if (!host.copy_players || forward_axis >= 0) return;
    static AnySessionPlayersV1 players;
    uint32_t flags = 0; players.struct_size = sizeof players;
    if (!host.copy_players(&players, &flags)) return;
    for (uint32_t i = 0; i < players.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = players.players[i];
        if ((p.valid_fields & PLAYER_LOCAL) && (p.valid_fields & PLAYER_FACING)) { facing = p.yaw_radians; facing_time = GetTickCount64(); return; }
    }
}

// Called once a frame from the render callback.
inline void update(bool photo, bool gameplay) {
    bool was = active.load();
    active = photo && gameplay;
    bool fly;
    { std::lock_guard lock(mutex); fly = photo && free_camera && hook_ready.load(); if (active && !was) show(); }
    flying = fly;
    // Leaving puts back the value read at install, not the one from when flying started: ThirdPerson may have set
    // it then and switched to first person since, which left the head showing in first person.
    if (head && fly != head_set) {
        *head = fly ? true : head_original;
        head_set = fly;
    } else if (head && fly) *head = true;   // ThirdPerson may have restored it when switching to first person
    if (!photo) keys = 0;
    if (fly && forward_axis < 0) sample_facing();
}

// ------------------------------------------------------------------ input (window thread)
inline uint32_t movement_bit(uint32_t key) {
    switch (key) {
        case 'W': return Forward; case 'S': return Back; case 'A': return Left; case 'D': return Right;
        case VK_SPACE: return Up; case 'C': case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return Down;
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return Fast;
        default: return 0;
    }
}
inline bool photo_key(uint32_t key) { return (key >= '1' && key <= '7') || key == 'R' || key == 'H' || key == 'V'; }
inline bool held[256];

inline void press(uint32_t key) {
    std::lock_guard lock(mutex);
    Tweaks& t = tweaks;
    auto cycle = [](int v, int n) { return v + 1 >= n ? -1 : v + 1; };   // preset, then each choice
    switch (key) {
        case '1': t.dof = t.dof < 0 ? 0 : std::max(t.dof - 1, 0); break;
        case '2': t.dof = t.dof < 0 ? 2 : std::min(t.dof + 1, 8); break;
        case '3': t.exposure = std::max(t.exposure - .25f, -3.f); break;
        case '4': t.exposure = std::min(t.exposure + .25f, 3.f); break;
        case '5': t.look = cycle(t.look, 6); break;
        case '6': t.vignette = cycle(t.vignette, 4); break;
        case '7': t.bloom = cycle(t.bloom, 4); break;
        case 'R': t = Tweaks{}; speed = float(kDefaultSpeed); break;
        case 'H': pinned = !pinned; if (!pinned) shown_until = 0; revision.fetch_add(1); return;
        case 'V': free_camera = !free_camera; keys = 0; log(free_camera ? "PHOTO_CAMERA free=1" : "PHOTO_CAMERA free=0"); show(); return;
    }
    changed();
}

inline uint32_t input(const AnyInputV1* e) {
    if (!e) return 0;
    if (e->kind == ANY_FOCUS_LOST) { keys = 0; std::fill(std::begin(held), std::end(held), false); return 0; }
    if (e->kind == ANY_KEY_UP) {
        if (uint32_t bit = movement_bit(e->key)) keys.fetch_and(~bit);
        if (e->key < 256) held[e->key] = false;
        return 0;
    }
    if (!active.load()) return 0;
    bool fly = flying.load();
    if (e->kind == ANY_KEY_DOWN) {
        if (uint32_t bit = movement_bit(e->key); bit && fly) { keys.fetch_or(bit); return 1; }
        if (photo_key(e->key)) { if (!held[e->key]) { held[e->key] = true; press(e->key); } return 1; }
        return 0;
    }
    if (!fly) return 0;
    if (e->kind == ANY_MOUSE_WHEEL && e->wheel) {
        speed = float(std::clamp(double(speed.load()) * std::pow(1.25, double(e->wheel) / WHEEL_DELTA), kMinSpeed, kMaxSpeed));
        std::lock_guard lock(mutex); show(); return 1;
    }
    return e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP;   // no shooting or building from the camera
}

// ------------------------------------------------------------------ help panel (render thread)
inline void text(float x, float y, float w, float h, float size, uint32_t color, const wchar_t* s, uint32_t flags = 0) {
    AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h; c.font_size = size;
    c.color = color; c.text = s; c.text_length = uint32_t(wcslen(s)); c.flags = ANY_GPU_NOWRAP | ANY_GPU_VCENTER | flags; gpu->emit(&c);
}
inline void draw(const AnyFrameV1* f, void*) {
    if (!gpu || !f || !f->focused || !active.load()) return;
    Tweaks t; bool free, open; ULONGLONG now = GetTickCount64();
    { std::lock_guard lock(mutex); t = tweaks; free = free_camera; open = pinned || now < shown_until; }
    if (!open) return;
    bool camera = hook_ready.load();
    float s = std::max(1.f, float(f->height) / 1080.f), line = 22 * s, w = 430 * s, x = 24 * s;
    wchar_t rows[10][96]; int n = 0;
    swprintf(rows[n++], 96, L"PHOTO MODE");
    if (!camera) swprintf(rows[n++], 96, L"Free camera needs Anymaker 0.1.25 (this game build is different)");
    else if (free) swprintf(rows[n++], 96, L"WASD move · Space / C up, down · Shift fast · Wheel speed %.1f m/s", speed.load());
    if (camera) swprintf(rows[n++], 96, L"V  free camera: %ls", free ? L"on" : L"off (the camera follows you)");
    if (t.dof < 0) swprintf(rows[n++], 96, L"1 / 2  depth of field: preset (focuses on the middle)"); else swprintf(rows[n++], 96, L"1 / 2  depth of field: %.1f (focuses on the middle)", t.dof * .5);
    swprintf(rows[n++], 96, L"3 / 4  exposure: %+.2f", t.exposure);
    swprintf(rows[n++], 96, L"5  colour look: %ls", t.look < 0 ? L"preset" : kLooks[t.look]);
    swprintf(rows[n++], 96, L"6  vignette: %ls", t.vignette < 0 ? L"preset" : kCorners[t.vignette]);
    swprintf(rows[n++], 96, L"7  bloom: %ls", t.bloom < 0 ? L"preset" : kGlows[t.bloom]);
    swprintf(rows[n++], 96, L"R reset · H %ls this help · photo key exits", pinned ? L"hide" : L"keep");
    float h = line * n + 16 * s, y = float(f->height) - h - 24 * s;
    AnyGpuCommandV1 bg; bg.kind = ANY_GPU_ROUND_RECT; bg.rect[0] = x; bg.rect[1] = y; bg.rect[2] = w; bg.rect[3] = h;
    bg.radius = 8 * s; bg.color = 0xff15181c; bg.opacity = .82f; gpu->emit(&bg);
    for (int i = 0; i < n; ++i) text(x + 14 * s, y + 8 * s + line * i, w - 28 * s, line, (i ? 13.5f : 15.f) * s, i ? 0xffe8ecf1 : 0xff7fd6ff, rows[i], i ? 0 : ANY_GPU_BOLD);
}

inline void ready(const AnyServicesV1* services) {
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log("PHOTO_PANEL unavailable=1");
    }
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
}  // namespace graphics::photocam
