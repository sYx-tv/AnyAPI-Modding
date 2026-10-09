// AnyMirror (prototype): symmetric building with the edge tools. While either edge tool is equipped and
// mirroring is on, every edge you place is also placed mirrored across a plane through the build. The plane
// is drawn in the world as a see-through wall that you can show or hide. Keybinds (rebindable in AnyHelpers):
// J mirror on/off, K show/hide wall, L cycle axis, [ and ] move the plane half a grid step, \ re-centre it.
//
// Co-op: the mirrored edge goes to the server through the same request the edge tool sends, so the host
// validates and replicates it like any edge. Only the player building needs the mod.
//
// EXPERIMENTAL. Hooks game functions directly through the experimental SDK (anymirror_bindings.h). They
// resolve only on Anymaker 0.1.23 / Steam build 25755694. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_experimental.hpp"
#include "anymirror_bindings.h"
#include "anymirror_logic.h"
#include "vehicle_reads.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace anymirror {
namespace {
using namespace vehicle_reads;
constexpr const char* kModId = "anymirror";
constexpr ptrdiff_t kStateVehicleId = 8;   // state_add_edge.m_hovered_vehicle_id / state_add_edge_drag.m_vehicle_id
constexpr int32_t kPaletteChoices[] = {23, 25, 24, 28};   // overlay_ui.palette: selectable, valid, hovered, group
const char* const kPaletteNames[] = {"Tool blue", "Valid green", "Hover", "Group"};
const char* const kAxisNames[] = {"X", "Y", "Z"};

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
const ModControlsV1* controls;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// ------------------------------------------------------------------ settings and keys
enum Setting { kShowHud, kWallDefault, kWallColour, kWallOpacity, kSettingCount };
uint64_t g_tokens[kSettingCount]{}, g_revision = UINT64_MAX;
double g_values[kSettingCount]{1, 1, 0, 35};
enum Action { kToggle, kWall, kAxis, kNudgeDown, kNudgeUp, kRecentre, kActionCount };
struct ActionDef { const char* id; const char* label; uint32_t key; };
const ActionDef kActions[kActionCount] = {
    {"toggle_mirror", "Mirror edges on / off (edge tool)", 'J'},
    {"toggle_wall", "Show / hide mirror wall", 'K'},
    {"cycle_axis", "Cycle mirror axis X / Y / Z", 'L'},
    {"plane_down", "Move mirror plane back half a step", VK_OEM_4},
    {"plane_up", "Move mirror plane forward half a step", VK_OEM_6},
    {"recentre", "Re-centre mirror plane on the build", VK_OEM_5},
};
uint64_t g_action_tokens[kActionCount]{};

// ------------------------------------------------------------------ shared state (mutex)
std::mutex g_mutex;
struct VehicleState { Plane plane; bool placed = false; Grid lo{}, hi{}; bool has_nodes = false; uint64_t bounds_at = 0; };
std::unordered_map<int32_t, VehicleState> g_vehicles;
bool g_enabled = false, g_wall = true, g_wall_from_settings = false;
int g_axis = 0;
int32_t g_vehicle_id = -1;      // vehicle the edge tool is working on
uint64_t g_tool_seen = 0;       // GetTickCount64 of the last edge tool overlay frame
uint64_t g_flash_at = 0; wchar_t g_flash[96]{};
bool g_held[256]{};
double g_grid_size = 0.25;

bool tool_active(uint64_t now) { return now - g_tool_seen < 400; }
void flash(const wchar_t* text) { wcsncpy_s(g_flash, text, _TRUNCATE); g_flash_at = GetTickCount64(); }

void refresh_settings() {
    if (!settings) return;
    uint64_t next = settings->revision();
    if (next == g_revision) return;
    g_revision = next;
    const double lo[] = {0, 0, 0, 10}, hi[] = {1, 1, 3, 90};
    for (int i = 0; i < kSettingCount; ++i) {
        AnySettingValueV1 v;
        if (settings->get(g_tokens[i], &v) && std::isfinite(v.number)) g_values[i] = std::clamp(v.number, lo[i], hi[i]);
    }
    if (!g_wall_from_settings) { g_wall = g_values[kWallDefault] != 0; g_wall_from_settings = true; }
}

// Plane for a vehicle: placed through the middle of the build on the current axis the first time it is needed.
VehicleState& vehicle_state(int32_t id) {
    auto& v = g_vehicles[id];
    if (!v.placed) {
        v.plane = v.has_nodes ? centred(g_axis, v.lo, v.hi) : Plane{g_axis, 0};
        v.placed = v.has_nodes;
    }
    return v;
}

// ------------------------------------------------------------------ hooks (main thread)
using push_edge_t = void (*)(uint8_t* peer_data, const int32_t* vehicle_id, const Grid* p0, const Grid* p1, const int32_t* size);
using overlay_t = void (*)(uint8_t* state, void* camera, void* client, uint8_t* scene, uint8_t* overlay);
using transform_t = void (*)(double* out, const uint8_t* vehicle);
using box_t = void (*)(uint8_t* overlay, const double* min, const double* max, const double* transform, const double* local, const int32_t* palette, const double* value);
using line_t = void (*)(uint8_t* overlay, const double* a, const double* b, const double* transform, const double* radius, const int32_t* palette, const double* value);

anymaker::cell_hook h_push, h_overlay[4];
void* g_box_fn; void* g_line_fn; void* g_transform_fn;
const double* g_grid_size_ptr;
std::atomic<int> g_edge_logs{12}, g_wall_logs{3};

void hk_push_edge(uint8_t* peer_data, const int32_t* vehicle_id, const Grid* p0, const Grid* p1, const int32_t* size) {
    auto original = reinterpret_cast<push_edge_t>(h_push.original);
    original(peer_data, vehicle_id, p0, p1, size);
    if (!vehicle_id || !p0 || !p1) return;
    Grid m0, m1; bool send = false;
    {
        std::lock_guard lock(g_mutex);
        if (!g_enabled || !tool_active(GetTickCount64())) return;
        auto& v = vehicle_state(*vehicle_id);
        if (!v.placed) { if (g_edge_logs.fetch_sub(1) > 0) log(1, "edge on vehicle %d before its build bounds were read; not mirrored", *vehicle_id); return; }
        send = mirrored_edge(*p0, *p1, v.plane, m0, m1);
        if (g_edge_logs.fetch_sub(1) > 0)
            log(0, "edge vehicle=%d (%d,%d,%d)-(%d,%d,%d) size=%d plane %s twice=%d -> %s (%d,%d,%d)-(%d,%d,%d)", *vehicle_id,
                p0->v[0], p0->v[1], p0->v[2], p1->v[0], p1->v[1], p1->v[2], size ? *size : -1, kAxisNames[v.plane.axis], v.plane.twice,
                send ? "mirror" : "on plane, skipped", m0.v[0], m0.v[1], m0.v[2], m1.v[0], m1.v[1], m1.v[2]);
    }
    if (send) original(peer_data, vehicle_id, &m0, &m1, size);   // straight to the game: never re-enters this hook
}

void draw_wall(uint8_t* overlay, const uint8_t* vehicle, const VehicleState& v, int palette_choice, double opacity) {
    if (!g_box_fn || !g_line_fn || !g_transform_fn || !overlay || !v.has_nodes) return;
    double transform[12]{};
    reinterpret_cast<transform_t>(g_transform_fn)(transform, vehicle);
    for (double t : transform) if (!std::isfinite(t)) return;
    static const double identity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    double grid = g_grid_size;
    Box box = wall(v.plane, v.lo, v.hi, grid, 2, grid * 0.08);
    int32_t palette = kPaletteChoices[std::clamp(palette_choice, 0, 3)];
    double value = std::clamp(opacity / 100.0, 0.05, 1.0);
    reinterpret_cast<box_t>(g_box_fn)(overlay, box.min, box.max, transform, identity, &palette, &value);
    // Outline and a centre cross so the plane reads clearly edge-on.
    int a = v.plane.axis, b = (a + 1) % 3, c = (a + 2) % 3;
    double mid = (box.min[a] + box.max[a]) * 0.5, radius = grid * 0.06;
    auto corner = [&](double pb, double pc, double* out) { out[a] = mid; out[b] = pb; out[c] = pc; };
    double p[4][3];
    corner(box.min[b], box.min[c], p[0]); corner(box.max[b], box.min[c], p[1]); corner(box.max[b], box.max[c], p[2]); corner(box.min[b], box.max[c], p[3]);
    for (int i = 0; i < 4; ++i) reinterpret_cast<line_t>(g_line_fn)(overlay, p[i], p[(i + 1) % 4], transform, &radius, &palette, &value);
    double m0[3], m1[3];
    corner((box.min[b] + box.max[b]) * 0.5, box.min[c], m0); corner((box.min[b] + box.max[b]) * 0.5, box.max[c], m1);
    reinterpret_cast<line_t>(g_line_fn)(overlay, m0, m1, transform, &radius, &palette, &value);
    corner(box.min[b], (box.min[c] + box.max[c]) * 0.5, m0); corner(box.max[b], (box.min[c] + box.max[c]) * 0.5, m1);
    reinterpret_cast<line_t>(g_line_fn)(overlay, m0, m1, transform, &radius, &palette, &value);
    if (g_wall_logs.fetch_sub(1) > 0)
        log(0, "wall vehicle axis %s twice=%d grid=%.4f box (%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f) transform t=(%.2f,%.2f,%.2f)", kAxisNames[a], v.plane.twice, grid,
            box.min[0], box.min[1], box.min[2], box.max[0], box.max[1], box.max[2], transform[9], transform[10], transform[11]);
}

void on_overlay(int which, uint8_t* state, void* camera, void* client, uint8_t* scene, uint8_t* overlay) {
    reinterpret_cast<overlay_t>(h_overlay[which].original)(state, camera, client, scene, overlay);
    if (!state || !scene || !anymaker::readable(state, 16)) return;
    uint64_t now = GetTickCount64();
    int32_t hovered = rd<int32_t>(state, kStateVehicleId);
    std::lock_guard lock(g_mutex);
    refresh_settings();
    if (g_grid_size_ptr && anymaker::readable(g_grid_size_ptr, 8) && std::isfinite(*g_grid_size_ptr) && *g_grid_size_ptr > 0.001 && *g_grid_size_ptr < 10)
        g_grid_size = *g_grid_size_ptr;
    g_tool_seen = now;
    if (hovered > 0) g_vehicle_id = hovered;
    if (g_vehicle_id <= 0) return;
    uint8_t* vehicle = find_vehicle(scene, g_vehicle_id);
    if (!vehicle) return;
    auto& v = g_vehicles[g_vehicle_id];
    if (now - v.bounds_at > 500) {
        Grid lo{}, hi{}; int count = 0;
        Vec3i l{}, h{};
        v.has_nodes = node_bounds(vehicle, l, h, &count);
        if (v.has_nodes) { lo = {{l.x, l.y, l.z}}; hi = {{h.x, h.y, h.z}}; v.lo = lo; v.hi = hi; }
        v.bounds_at = now;
        static bool logged;
        if (!logged && v.has_nodes) { logged = true; log(0, "edge tool on vehicle %d: %d nodes, bounds (%d,%d,%d)..(%d,%d,%d), grid %.4f m",
            g_vehicle_id, count, l.x, l.y, l.z, h.x, h.y, h.z, g_grid_size); }
    }
    auto& placed = vehicle_state(g_vehicle_id);
    if (g_enabled && g_wall && overlay) draw_wall(overlay, vehicle, placed, int(g_values[kWallColour]), g_values[kWallOpacity]);
}
void hk_overlay0(uint8_t* s, void* c, void* n, uint8_t* sc, uint8_t* o) { on_overlay(0, s, c, n, sc, o); }
void hk_overlay1(uint8_t* s, void* c, void* n, uint8_t* sc, uint8_t* o) { on_overlay(1, s, c, n, sc, o); }
void hk_overlay2(uint8_t* s, void* c, void* n, uint8_t* sc, uint8_t* o) { on_overlay(2, s, c, n, sc, o); }
void hk_overlay3(uint8_t* s, void* c, void* n, uint8_t* sc, uint8_t* o) { on_overlay(3, s, c, n, sc, o); }

// ------------------------------------------------------------------ install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    void* hooks[4] = {(void*)&hk_overlay0, (void*)&hk_overlay1, (void*)&hk_overlay2, (void*)&hk_overlay3};
    const anymaker::func_desc* overlays[4] = {&bind::edge_hover_overlay, &bind::edge_drag_overlay, &bind::edge2_hover_overlay, &bind::edge2_drag_overlay};
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyMirror stays off"); return; }
        void** push = hook_cell(bind::client_push_add_edge);
        void** cells[4]; bool all = push != nullptr;
        for (int i = 0; i < 4; ++i) { cells[i] = hook_cell(*overlays[i]); all = all && cells[i]; }
        void* box = function(bind::overlay_add_vehicle_box);
        void* line = function(bind::overlay_add_vehicle_line);
        void* transform = function(bind::client_vehicle_render_transform);
        if (!all) {
            if (attempt == 179) log(2, "could not locate the edge tool (push=%d hover=%d drag=%d hover2=%d drag2=%d)", !!push, !!cells[0], !!cells[1], !!cells[2], !!cells[3]);
            continue;
        }
        g_box_fn = box; g_line_fn = line; g_transform_fn = transform;
        g_grid_size_ptr = static_cast<const double*>(global(bind::g_vehicle_grid_size));
        if (!box || !line || !transform) log(1, "wall drawing unavailable (box=%d line=%d transform=%d); mirroring still works", !!box, !!line, !!transform);
        bool ok = h_push.install(push, (void*)&hk_push_edge);
        for (int i = 0; i < 4 && ok; ++i) ok = h_overlay[i].install(cells[i], hooks[i]);
        if (!ok) { log(2, "a hook cell was already taken or not writable; AnyMirror is off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: grid size %s, wall %s. Equip an edge tool and press the mirror key.", attempt,
            g_grid_size_ptr ? "found" : "default 0.25 m", g_box_fn ? "available" : "unavailable");
        return;
    }
}
void remove_hooks() {
    void* hooks[4] = {(void*)&hk_overlay0, (void*)&hk_overlay1, (void*)&hk_overlay2, (void*)&hk_overlay3};
    for (int i = 3; i >= 0; --i) h_overlay[i].remove(hooks[i]);
    h_push.remove((void*)&hk_push_edge);
}

// ------------------------------------------------------------------ HUD badge and keys
bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }
uint32_t key_for(int action) {
    if (controls && g_action_tokens[action]) return controls->key(g_action_tokens[action]);
    return kActions[action].key;
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_mutex);
    refresh_settings();
    uint64_t now = GetTickCount64();
    if (!g_ready || !frame->focused || !tool_active(now) || !g_values[kShowHud] || !gameplay()) return;
    float s = std::clamp(float(frame->height) / 1080.f, .7f, 1.8f);
    wchar_t line1[128], line2[160];
    auto plane_text = [&]() -> double { auto it = g_vehicles.find(g_vehicle_id); return it != g_vehicles.end() && it->second.placed ? it->second.plane.twice * 0.5 : 0.0; };
    if (g_enabled) swprintf_s(line1, L"MIRROR ON  ·  %hs axis  ·  plane %.1f  ·  wall %ls", kAxisNames[g_axis], plane_text(), g_wall ? L"shown" : L"hidden");
    else swprintf_s(line1, L"MIRROR OFF");
    char names[kActionCount][24];
    for (int i = 0; i < kActionCount; ++i) {
        names[i][0] = 0;
        uint32_t k = key_for(i);
        if (controls && g_action_tokens[i] && controls->key_name && controls->key_name(g_action_tokens[i], names[i], sizeof names[i])) continue;
        if (k >= '0' && k <= 'Z') snprintf(names[i], sizeof names[i], "%c", char(k));
        else snprintf(names[i], sizeof names[i], k == VK_OEM_4 ? "[" : k == VK_OEM_6 ? "]" : k == VK_OEM_5 ? "\\" : k ? "key %u" : "unbound", k);
    }
    swprintf_s(line2, L"%hs mirror   %hs wall   %hs axis   %hs %hs move   %hs centre", names[kToggle], names[kWall], names[kAxis], names[kNudgeDown], names[kNudgeUp], names[kRecentre]);
    float w = 520 * s, h = 52 * s, x = (float(frame->width) - w) * .5f, y = 70 * s;
    AnyGpuCommandV1 bg; bg.kind = ANY_GPU_ROUND_RECT; bg.rect[0] = x; bg.rect[1] = y; bg.rect[2] = w; bg.rect[3] = h; bg.radius = 6 * s; bg.color = 0xf0191c20; gpu->emit(&bg);
    auto text = [&](float ty, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = x + 12 * s; c.rect[1] = ty; c.rect[2] = w - 24 * s; c.rect[3] = th; c.text = t;
        c.text_length = uint32_t(wcslen(t)); c.font_size = size; c.color = color; c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER; gpu->emit(&c); };
    text(y + 6 * s, 22 * s, line1, 15 * s, g_enabled ? 0xff7dd3fc : 0xffaeb7c2, ANY_GPU_BOLD);
    text(y + 29 * s, 16 * s, line2, 11 * s, 0xffaeb7c2, 0);
    if (now - g_flash_at < 1500) {
        float fy = y + h + 6 * s;
        AnyGpuCommandV1 fb = bg; fb.rect[1] = fy; fb.rect[3] = 26 * s; fb.color = 0xe0222831; gpu->emit(&fb);
        text(fy + 2 * s, 22 * s, g_flash, 13 * s, 0xfff5f7fa, 0);
    }
}

void handle(int action) {
    auto it = g_vehicles.find(g_vehicle_id);
    VehicleState* v = it != g_vehicles.end() ? &it->second : nullptr;
    wchar_t msg[96];
    switch (action) {
    case kToggle: g_enabled = !g_enabled; flash(g_enabled ? L"Mirror on: edges are placed in pairs" : L"Mirror off"); break;
    case kWall: g_wall = !g_wall; flash(g_wall ? L"Mirror wall shown" : L"Mirror wall hidden"); break;
    case kAxis:
        g_axis = (g_axis + 1) % 3;
        for (auto& [id, vs] : g_vehicles) { vs.plane = vs.has_nodes ? centred(g_axis, vs.lo, vs.hi) : Plane{g_axis, 0}; vs.placed = vs.has_nodes; }
        swprintf_s(msg, L"Mirror axis %hs (plane re-centred)", kAxisNames[g_axis]); flash(msg); break;
    case kNudgeDown: case kNudgeUp:
        if (v && (v->placed || v->has_nodes)) {
            if (!v->placed) { v->plane = centred(g_axis, v->lo, v->hi); v->placed = true; }
            v->plane = nudged(v->plane, action == kNudgeUp ? 1 : -1);
            swprintf_s(msg, L"Mirror plane at %.1f", v->plane.twice * 0.5); flash(msg); }
        break;
    case kRecentre:
        if (v && v->has_nodes) { v->plane = centred(g_axis, v->lo, v->hi); v->placed = true; swprintf_s(msg, L"Mirror plane centred at %.1f", v->plane.twice * 0.5); flash(msg); }
        break;
    }
    log(0, "key action %s: mirror %s, axis %s, wall %s", kActions[action].id, g_enabled ? "on" : "off", kAxisNames[g_axis], g_wall ? "shown" : "hidden");
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_mutex);
    if (e->kind == ANY_FOCUS_LOST) { std::fill(std::begin(g_held), std::end(g_held), false); return 0; }
    if (e->key >= 256 || (e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP)) return 0;
    if (e->kind == ANY_KEY_UP) { bool captured = g_held[e->key]; g_held[e->key] = false; return captured; }
    if (!g_ready || !tool_active(GetTickCount64()) || !gameplay()) return 0;
    for (int i = 0; i < kActionCount; ++i) {
        uint32_t k = key_for(i);
        if (!k || k != e->key) continue;
        if (!g_held[e->key]) { g_held[e->key] = true; handle(i); }
        return 1;
    }
    return 0;
}
}  // namespace
}  // namespace anymirror

using namespace anymirror;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ui_state unavailable; AnyMirror is off"); return; }
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(1, "GPU drawing unavailable: the on-screen mirror status is hidden");
    }
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        for (int i = 0; i < kActionCount; ++i) {
            ModControlActionV1 d; d.mod_id = kModId; d.mod_name = "AnyMirror"; d.action_id = kActions[i].id; d.label = kActions[i].label; d.default_key = kActions[i].key;
            g_action_tokens[i] = controls->register_action(&d);
        }
    } else controls = nullptr;
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        const char* ids[] = {"show_status", "wall_default", "wall_colour", "wall_opacity"};
        const char* labels[] = {"Show mirror status while an edge tool is equipped", "Show the mirror wall by default", "Mirror wall colour", "Mirror wall strength (%)"};
        uint32_t kinds[] = {ANY_SETTING_BOOL, ANY_SETTING_BOOL, ANY_SETTING_CHOICE, ANY_SETTING_INTEGER};
        double lo[] = {0, 0, 0, 10}, hi[] = {1, 1, 3, 90}, step[] = {1, 1, 1, 5};
        for (int i = 0; i < kSettingCount; ++i) {
            AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyMirror"; d.setting_id = ids[i]; d.label = labels[i]; d.order = i; d.kind = kinds[i];
            d.default_number = g_values[i]; d.minimum = lo[i]; d.maximum = hi[i]; d.step = step[i];
            if (i == kWallColour) { d.choices = kPaletteNames; d.choice_count = 4; }
            if (i == 0) d.description = "Mirroring works only with the edge tools. Keys are in Controls under AnyMirror.";
            g_tokens[i] = settings->register_setting(&d);
        }
    } else settings = nullptr;
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
