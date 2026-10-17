// CameraTablet (test build): proves a second camera can be drawn on screen.
//
// This first build has no tablet, camera block or antenna yet. It drives AnyAPI's experimental scene view
// (anyapi.scene_view, version 1), which records the game's own gbuffer pass a second time from another camera and
// draws the result in the top-right corner of the screen.
//
// Keys (in gameplay): F10 turns the picture on and off. F11 cycles the camera: chase (behind and above you), rear view,
// zoom, and a fixed camera. F12 drops the fixed camera where you stand, looking where you look.
//
// Known limit: the game only prepares what your own camera sees, so things behind you can be missing from the chase,
// rear and fixed views. That is the next thing to solve; this build shows how much of it shows up.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_scene_view_v1.h"
#include <cmath>
#include <cstdio>
#include <mutex>

namespace cameratablet {
namespace {
constexpr const char* kModId = "cameratablet";
constexpr const char* kModeNames[] = {"chase", "rear view", "zoom", "fixed camera"};
constexpr int kModes = 4;

AnyModHostV1 host;
const AnyUiStateV1* ui;
const AnySceneViewV1* view;
std::mutex lock;
bool on;
int mode;
bool placed;
double placed_position[3];
float placed_yaw;

void log(uint32_t level, const char* text) { if (host.log) host.log(level, kModId, text); }

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// Remembers where the local player stands and looks, for the fixed camera.
bool place() {
    static AnySessionPlayersV1 players;
    players = {};
    if (!host.copy_players || !host.copy_players(&players, nullptr)) return false;
    for (uint32_t i = 0; i < players.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        auto& p = players.players[i];
        constexpr uint32_t need = PLAYER_LOCAL | PLAYER_POSITION | PLAYER_FACING;
        if ((p.valid_fields & need) != need) continue;
        placed_position[0] = p.position[0];
        placed_position[1] = p.position[1] + 1.7;  // about eye height
        placed_position[2] = p.position[2];
        placed_yaw = float(p.yaw_radians);
        return placed = true;
    }
    return false;
}

void apply() {
    if (!view) return;
    AnySceneViewParametersV1 p;
    p.enabled = on ? 1 : 0;
    p.interval = 1;
    p.display[0] = .66f; p.display[1] = .03f; p.display[2] = .32f; p.display[3] = .32f;
    switch (mode) {
    case 0: p.mode = 1; p.position[1] = 3; p.position[2] = -8; p.pitch = -.25f; break;  // chase
    case 1: p.mode = 1; p.position[1] = .3; p.yaw = 3.14159265f; break;                  // rear view
    case 2: p.mode = 1; p.fov_degrees = 15; break;                                       // zoom
    default:
        if (!placed && !place()) { p.mode = 1; break; }
        p.mode = 2; p.position[0] = placed_position[0]; p.position[1] = placed_position[1]; p.position[2] = placed_position[2];
        p.yaw = placed_yaw; p.pitch = -.1f; break;
    }
    if (!view->set(&p)) log(1, "The scene view refused the settings");
    char text[96];
    std::snprintf(text, sizeof text, "View %s, camera: %s", on ? "on" : "off", kModeNames[mode]);
    log(0, text);
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e || e->kind != ANY_KEY_DOWN || !view) return 0;
    if (e->key != VK_F10 && e->key != VK_F11 && e->key != VK_F12) return 0;
    if (!gameplay()) return 0;
    std::lock_guard guard(lock);
    if (e->key == VK_F10) on = !on;
    else if (e->key == VK_F11) { mode = (mode + 1) % kModes; on = true; }
    else { place(); mode = 3; on = true; }
    apply();
    return 1;
}

template <class T> const T* query(const AnyServicesV1* s, const char* id, uint32_t v) {
    auto t = static_cast<const T*>(s->query(id, v));
    return t && t->struct_size == sizeof(T) && t->version == v ? t : nullptr;
}
}  // namespace
}  // namespace cameratablet

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* cb) {
    using namespace cameratablet;
    if (!h || !cb || h->abi != ANYAPI_MOD_ABI || h->struct_size != sizeof(AnyModHostV1)) return false;
    host = *h; *cb = {}; cb->id = kModId; cb->input = input;
    return true;
}

extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    using namespace cameratablet;
    auto s = AnyAPI_Services(); if (!s) return;
    ui = query<AnyUiStateV1>(s, "anyapi.ui_state", 1);
    view = query<AnySceneViewV1>(s, "anyapi.scene_view", 1);
    if (!view || !view->set || !view->status) { view = nullptr; log(2, "AnyAPI has no scene view (needs the camera tablet AnyAPI test build)"); return; }
    log(0, "Ready: F10 picture on/off, F11 next camera, F12 drop the fixed camera here");
}
