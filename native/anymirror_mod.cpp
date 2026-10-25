// AnyMirror: symmetric building. While mirroring is on, every edge you place with either edge tool, every plate
// you add with the add plate tool, and every edge, node or plate you take off with the remove tool is mirrored
// across a plane through the build (plates need their mirrored edges to exist already). The plane
// is drawn in the world as a see-through wall that you can show or hide. Keybinds (rebindable in AnyHelpers):
// J mirror on/off, K show/hide wall, L cycle axis, [ and ] move the plane half a grid step, \ re-centre it.
//
// Co-op: each mirrored piece goes to the server through the same request the tool sends, so the host
// validates and replicates it like any other. Only the player building needs the mod.
//
// EXPERIMENTAL. Hooks game functions directly through the experimental SDK (anymirror_bindings.h). They
// resolve only on Anymaker 0.1.27 / Steam build 26113663. First runs are diagnostic: read anymaker_modding.log.
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
#include "anymirror_split.h"
#include "vehicle_reads.h"
#include "cell_scan.h"
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
    const double lo[] = {0, 0, 0, 10}, hi[] = {1, 1, 3, 100};
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
using remove_t = void (*)(uint8_t* peer_data, const int32_t* vehicle_id, const int32_t* id);
using add_plate_t = void (*)(uint8_t* peer_data, const int32_t* vehicle_id, anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type);
using plate_overlay_t = void (*)(uint8_t* state, void* item, void* camera, void* client, uint8_t* scene, uint8_t* overlay);

// Overlay hooks: 0-3 the two edge tools (hover, drag), 4-8 the remove tool states, 9-10 the add plate tool states.
constexpr int kEdgeOverlays = 4, kRemoveOverlays = 5, kPlateOverlays = 2, kOverlayCount = kEdgeOverlays + kRemoveOverlays + kPlateOverlays;
enum Remove { kRemoveEdge, kRemoveNode, kRemovePlate, kRemoveCount };
const char* const kRemoveNames[] = {"edge", "node", "plate"};
anymaker::cell_hook h_push, h_overlay[kOverlayCount], h_remove[kRemoveCount], h_add_plate;
void* g_box_fn; void* g_line_fn; void* g_transform_fn;
const double* g_grid_size_ptr;
uint8_t* g_scene;               // client scene from the last tool overlay frame; used only while a tool is active
std::atomic<int> g_edge_logs{12}, g_wall_logs{3}, g_remove_logs{12}, g_plate_logs{12};

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

// The vehicle and plane removals and plates are mirrored on: null when mirroring is off, no supported tool is
// in use, or the plane is not placed yet. Call with g_mutex held, on the main thread.
const uint8_t* mirror_target(int32_t vehicle_id, Plane& plane) {
    if (!g_enabled || !tool_active(GetTickCount64()) || !g_scene) return nullptr;
    auto it = g_vehicles.find(vehicle_id);
    if (it == g_vehicles.end() || !it->second.placed) return nullptr;
    plane = it->second.plane;
    return find_vehicle(g_scene, vehicle_id);
}
// Id of the node at the mirror image of node `id`, or -1 when there is none.
int32_t mirrored_node(const uint8_t* vehicle, int32_t id, const Plane& plane) {
    Vec3i p{};
    if (!node_pos(vehicle, id, p)) return -1;
    Grid g{{p.x, p.y, p.z}};
    if (!in_range(g)) return -1;
    Grid m = mirror(g, plane);
    return node_at(vehicle, Vec3i{m.v[0], m.v[1], m.v[2]});
}
// Id of the mirror twin of an edge, node or plate, or -1 when the twin is missing or is the same piece.
int32_t mirrored_piece(int kind, const uint8_t* vehicle, int32_t id, const Plane& plane) {
    int32_t twin = -1;
    if (kind == kRemoveNode) twin = mirrored_node(vehicle, id, plane);
    else if (kind == kRemoveEdge) {
        int32_t n0 = -1, n1 = -1;
        for_each_edge(vehicle, [&](int32_t e, int32_t a, int32_t b) { if (e != id) return true; n0 = a; n1 = b; return false; });
        int32_t m0 = n0 < 0 ? -1 : mirrored_node(vehicle, n0, plane), m1 = n1 < 0 ? -1 : mirrored_node(vehicle, n1, plane);
        if (m0 >= 0 && m1 >= 0)
            for_each_edge(vehicle, [&](int32_t e, int32_t a, int32_t b) { if ((a == m0 && b == m1) || (a == m1 && b == m0)) { twin = e; return false; } return true; });
    } else if (kind == kRemovePlate) {
        int32_t corners[64], twins[64]; int n = 0;
        for_each_plate(vehicle, 64, [&](int32_t p, const int32_t* ids, int count) { if (p != id) return true; n = count; std::copy(ids, ids + count, corners); return false; });
        bool all = n >= 3;
        for (int i = 0; i < n && all; ++i) all = (twins[i] = mirrored_node(vehicle, corners[i], plane)) >= 0;
        if (all)
            for_each_plate(vehicle, 64, [&](int32_t p, const int32_t* ids, int count) { if (!same_ids(twins, n, ids, count)) return true; twin = p; return false; });
    }
    return twin == id ? -1 : twin;
}

void on_remove(int kind, uint8_t* peer_data, const int32_t* vehicle_id, const int32_t* id) {
    auto original = reinterpret_cast<remove_t>(h_remove[kind].original);
    original(peer_data, vehicle_id, id);
    if (!vehicle_id || !id) return;
    int32_t twin = -1;
    {
        std::lock_guard lock(g_mutex);
        Plane plane;
        const uint8_t* vehicle = mirror_target(*vehicle_id, plane);
        if (!vehicle) return;
        twin = mirrored_piece(kind, vehicle, *id, plane);
        if (g_remove_logs.fetch_sub(1) > 0)
            log(0, "remove %s %d on vehicle %d, plane %s twice=%d -> %s %d", kRemoveNames[kind], *id, *vehicle_id, kAxisNames[plane.axis], plane.twice,
                twin >= 0 ? "mirror" : "no separate twin", twin);
    }
    if (twin >= 0) original(peer_data, vehicle_id, &twin);   // straight to the game: never re-enters this hook
}
void hk_remove_edge(uint8_t* p, const int32_t* v, const int32_t* id) { on_remove(kRemoveEdge, p, v, id); }
void hk_remove_node(uint8_t* p, const int32_t* v, const int32_t* id) { on_remove(kRemoveNode, p, v, id); }
void hk_remove_plate(uint8_t* p, const int32_t* v, const int32_t* id) { on_remove(kRemovePlate, p, v, id); }

// ------------------------------------------------------------------ bent windows (auto-split)
// The game draws, collides and saves every plate as one flat, convex panel, so a window loop that bends is refused.
// With "Split bent windows" on, the window tool accepts such a loop (client_scene.vehicle.can_add_plate answers yes
// when a split exists), and the request is replaced by: a strut along each cut (vetted with can_add_edge) and one
// window per flat piece, each through the normal requests, so mirroring and the host's checks apply as usual. The cut
// struts are then painted kHiddenColor, which AnyHelpers does not draw. The host needs AnyHelpers' "Any window shape"
// on, since pieces are often shapes the stock window rule refuses.
using can_add_plate_t = void (*)(bool* ret, const uint8_t* vehicle, const anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type, const void* blocking);
using can_add_edge_t = void (*)(bool* ret, const uint8_t* vehicle, const Grid* p0, const Grid* p1, const int32_t* size);
using paint_edge_t = void (*)(uint8_t* peer_data, const int32_t* color, const int32_t* vehicle_id, const int32_t* edge_id);
constexpr int32_t kWindowType = 1, kHiddenColor = 68;   // vehicle_plate_util.e_plate_type.window; paint tool C68
anymaker::cell_hook h_can_plate, h_is_planar, h_is_convex;
can_add_edge_t g_can_edge; paint_edge_t g_paint_edge;
uint64_t g_split_token;
std::atomic<int> g_split_logs{12};
thread_local bool g_in_split = false;
struct EdgeInfo { int32_t id, a, b, color, size; };
template <class F> void for_each_edge_info(const uint8_t* vehicle, F&& visit) {
    for_each_ref(vehicle + off::vehicle_edges, 1 << 20, [&](uint8_t* e) {
        if (!anymaker::readable(e, 80)) return true;
        return visit(EdgeInfo{rd<int32_t>(e, off::edge_id), rd<int32_t>(e, off::edge_node_id_0), rd<int32_t>(e, off::edge_node_id_1),
                              rd<int32_t>(e, off::edge_color), rd<int32_t>(e, off::edge_size)});
    });
}
struct Split { int32_t vehicle = -1; std::vector<int32_t> ids; uint64_t at = 0; bool ok = false; anymirror_split::Plan plan; std::vector<Grid> pos; std::vector<bool> exists; int32_t size = 0; };
Split g_split;                                       // last plan, main thread only (tool checks run every frame)
struct PendingPaint { int32_t vehicle; Grid p0, p1; uint64_t until; };
std::vector<PendingPaint> g_paints;                  // cut struts to paint hidden once the host has added them (g_mutex)
uint8_t* g_peer_data;

bool split_on() {
    if (!settings || !g_split_token) return true;
    AnySettingValueV1 v; return !settings->get(g_split_token, &v) || v.number != 0;
}
// Plan for a window loop, cached per loop. False when the loop is flat and convex already, is not a closed ring of
// struts, or cannot be cut.
const Split* plan_split(const uint8_t* vehicle, int32_t vehicle_id, const int32_t* ids, int n) {
    uint64_t now = GetTickCount64();
    if (g_split.vehicle == vehicle_id && int(g_split.ids.size()) == n && std::equal(ids, ids + n, g_split.ids.begin()) && now - g_split.at < 1000)
        return g_split.ok ? &g_split : nullptr;
    Split s; s.vehicle = vehicle_id; s.ids.assign(ids, ids + n); s.at = now;
    std::vector<anymirror_split::P> pts(n);
    bool ok = n >= 4 && n <= 64 && g_can_edge;
    for (int i = 0; i < n && ok; ++i) {
        Vec3i v{};
        ok = node_pos(vehicle, ids[i], v);
        s.pos.push_back(Grid{{v.x, v.y, v.z}});
        pts[i] = {v.x, v.y, v.z};
    }
    // Every side must already be a strut; the cut struts take the thinnest side's size.
    std::vector<EdgeInfo> edges;
    if (ok) for_each_edge_info(vehicle, [&](const EdgeInfo& e) { edges.push_back(e); return true; });
    auto edge_between = [&](int32_t a, int32_t b) -> const EdgeInfo* {
        for (auto& e : edges) if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return &e;
        return nullptr;
    };
    s.size = INT32_MAX;
    for (int i = 0; i < n && ok; ++i) {
        const EdgeInfo* e = edge_between(ids[i], ids[(i + 1) % n]);
        ok = e != nullptr;
        if (e) s.size = std::min(s.size, e->size);
    }
    if (ok) {
        s.plan = anymirror_split::split(pts, [&](int a, int b) {
            if (edge_between(ids[a], ids[b])) return true;
            bool can = false; g_can_edge(&can, vehicle, &s.pos[a], &s.pos[b], &s.size);
            return can;
        });
        ok = !s.plan.pieces.empty();
        for (auto& c : s.plan.cuts) s.exists.push_back(edge_between(ids[c.first], ids[c.second]) != nullptr);
    }
    s.ok = ok;
    g_split = std::move(s);
    return g_split.ok ? &g_split : nullptr;
}

void hk_can_plate(bool* ret, const uint8_t* vehicle, const anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type, const void* blocking) {
    reinterpret_cast<can_add_plate_t>(h_can_plate.original)(ret, vehicle, node_ids, is_reverse, type, blocking);
    if (!ret || *ret || !type || *type != kWindowType || !vehicle || !node_ids || !split_on() || !anymaker::readable(node_ids, sizeof *node_ids)) return;
    int n = node_ids->count; int32_t ids[64];
    if (n < 4 || n > 64) return;
    for (int i = 0; i < n; ++i) { const int32_t* c = node_ids->at<int32_t>(i); if (!c) return; ids[i] = *c; }
    const Split* plan = plan_split(vehicle, rd<int32_t>(vehicle, off::vehicle_id), ids, n);
    if (!plan) return;
    *ret = true;
    static bool logged;
    if (!logged) { logged = true; log(0, "bent window loop of %d corners accepted: %zu flat pieces, %zu cuts", n, plan->plan.pieces.size(), plan->plan.cuts.size()); }
}

// While edges are picked, the plate tools refuse an edge that leaves the plane of the edges picked so far, or makes the
// loop non-convex (state_add_plate_select_edges._get_is_planar / _get_is_convex). With splitting on both answer yes, so a
// bent loop can be picked; can_add_plate then decides when it closes (a split exists for windows, the stock rule otherwise).
using select_planar_t = void (*)(bool* ret, const void* state, const Grid* node_pos);
using select_convex_t = void (*)(bool* ret, const void* state, const Grid* node_pos, const void* vehicle);
void hk_is_planar(bool* ret, const void* state, const Grid* node_pos) {
    reinterpret_cast<select_planar_t>(h_is_planar.original)(ret, state, node_pos);
    if (ret && !*ret && split_on()) *ret = true;
}
void hk_is_convex(bool* ret, const void* state, const Grid* node_pos, const void* vehicle) {
    reinterpret_cast<select_convex_t>(h_is_convex.original)(ret, state, node_pos, vehicle);
    if (ret && !*ret && split_on()) *ret = true;
}
void hk_push_edge(uint8_t* peer_data, const int32_t* vehicle_id, const Grid* p0, const Grid* p1, const int32_t* size);
void hk_add_plate(uint8_t* peer_data, const int32_t* vehicle_id, anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type);
// Sends a bent window as cut struts plus one window per piece. False when the request is not a bent window.
bool send_split(uint8_t* peer_data, const int32_t* vehicle_id, anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type) {
    if (g_in_split || !type || *type != kWindowType || !vehicle_id || !node_ids || !split_on() || !anymaker::readable(node_ids, sizeof *node_ids)) return false;
    int n = node_ids->count; int32_t ids[64];
    if (n < 4 || n > 64) return false;
    for (int i = 0; i < n; ++i) { const int32_t* c = node_ids->at<int32_t>(i); if (!c) return false; ids[i] = *c; }
    Split plan;
    std::vector<PendingPaint> paints;
    {
        std::lock_guard lock(g_mutex);
        const uint8_t* vehicle = g_scene ? find_vehicle(g_scene, *vehicle_id) : nullptr;
        const Split* p = vehicle ? plan_split(vehicle, *vehicle_id, ids, n) : nullptr;
        if (!p) return false;
        plan = *p;
        g_peer_data = peer_data;
        uint64_t until = GetTickCount64() + 15000;
        Plane plane; bool mirrored = mirror_target(*vehicle_id, plane) != nullptr;
        for (size_t c = 0; c < plan.plan.cuts.size(); ++c) {
            if (plan.exists[c]) continue;
            Grid a = plan.pos[plan.plan.cuts[c].first], b = plan.pos[plan.plan.cuts[c].second], m0, m1;
            paints.push_back({*vehicle_id, a, b, until});
            if (mirrored && mirrored_edge(a, b, plane, m0, m1)) paints.push_back({*vehicle_id, m0, m1, until});
        }
        g_paints.insert(g_paints.end(), paints.begin(), paints.end());
    }
    if (g_split_logs.fetch_sub(1) > 0) log(0, "bent window on vehicle %d: %d corners -> %zu flat pieces, %zu cut struts (size %d)", *vehicle_id, n,
        plan.plan.pieces.size(), plan.plan.cuts.size(), plan.size);
    g_in_split = true;
    for (size_t c = 0; c < plan.plan.cuts.size(); ++c)
        if (!plan.exists[c]) hk_push_edge(peer_data, vehicle_id, &plan.pos[plan.plan.cuts[c].first], &plan.pos[plan.plan.cuts[c].second], &plan.size);
    // Each piece goes out through the game's own node list, cut down to the piece's corners and put back after.
    int32_t count = node_ids->count;
    for (auto& piece : plan.plan.pieces) {
        node_ids->count = int32_t(piece.size());
        for (size_t i = 0; i < piece.size(); ++i) *node_ids->at<int32_t>(int32_t(i)) = ids[piece[i]];
        hk_add_plate(peer_data, vehicle_id, node_ids, is_reverse, type);
    }
    node_ids->count = count;
    for (int i = 0; i < n; ++i) *node_ids->at<int32_t>(i) = ids[i];
    g_in_split = false;
    return true;
}
// Paints the cut struts hidden once they exist on this client. Call with g_mutex held, on the main thread.
void paint_cut_struts(uint8_t* scene, uint64_t now) {
    if (g_paints.empty() || !g_paint_edge || !g_peer_data || !scene) return;
    for (size_t i = 0; i < g_paints.size();) {
        auto& p = g_paints[i];
        uint8_t* vehicle = find_vehicle(scene, p.vehicle);
        int32_t a = vehicle ? node_at(vehicle, Vec3i{p.p0.v[0], p.p0.v[1], p.p0.v[2]}) : -1, b = vehicle ? node_at(vehicle, Vec3i{p.p1.v[0], p.p1.v[1], p.p1.v[2]}) : -1;
        int32_t edge = -1, colour = -1;
        if (a >= 0 && b >= 0) for_each_edge_info(vehicle, [&](const EdgeInfo& e) {
            if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) { edge = e.id; colour = e.color; return false; }
            return true;
        });
        if (edge >= 0 && colour != kHiddenColor) g_paint_edge(g_peer_data, &kHiddenColor, &p.vehicle, &edge);
        if (edge >= 0 || now > p.until) {
            if (edge < 0 && g_split_logs.fetch_sub(1) > 0) log(1, "cut strut on vehicle %d never appeared; not painted hidden", p.vehicle);
            g_paints.erase(g_paints.begin() + i);
        } else ++i;
    }
}

void hk_add_plate(uint8_t* peer_data, const int32_t* vehicle_id, anymaker::gc_vector_raw* node_ids, const bool* is_reverse, const int32_t* type) {
    if (send_split(peer_data, vehicle_id, node_ids, is_reverse, type)) return;
    auto original = reinterpret_cast<add_plate_t>(h_add_plate.original);
    original(peer_data, vehicle_id, node_ids, is_reverse, type);
    if (!vehicle_id || !node_ids || !anymaker::readable(node_ids, sizeof *node_ids)) return;
    int32_t corners[64], twins[64], sent[64]; int n = node_ids->count;
    {
        std::lock_guard lock(g_mutex);
        Plane plane;
        const uint8_t* vehicle = mirror_target(*vehicle_id, plane);
        if (!vehicle || n < 3 || n > 64) return;
        for (int i = 0; i < n; ++i) {
            const int32_t* c = node_ids->at<int32_t>(i);
            if (!c) return;
            corners[i] = *c;
            if ((twins[i] = mirrored_node(vehicle, corners[i], plane)) < 0) {
                flash(L"Plate not mirrored: place the mirrored edges first");
                if (g_plate_logs.fetch_sub(1) > 0) log(1, "plate on vehicle %d: corner node %d has no mirrored node; not mirrored", *vehicle_id, corners[i]);
                return;
            }
        }
        mirrored_corners(twins, n, sent);
        bool same = same_ids(corners, n, sent, n);
        if (g_plate_logs.fetch_sub(1) > 0)
            log(0, "plate on vehicle %d with %d corners (first node %d), plane %s twice=%d -> %s", *vehicle_id, n, corners[0], kAxisNames[plane.axis], plane.twice,
                same ? "its own mirror image, skipped" : "mirror");
        if (same) return;
    }
    // The event copies the node list while it is built, so the game's own list is swapped to the mirrored
    // corners for the second request and put straight back.
    for (int i = 0; i < n; ++i) *node_ids->at<int32_t>(i) = sent[i];
    original(peer_data, vehicle_id, node_ids, is_reverse, type);
    for (int i = 0; i < n; ++i) *node_ids->at<int32_t>(i) = corners[i];
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
    double value = 0;   // shade on gradient palettes only; the solid colours ignore it
    Box bars[kLatticeMax];
    int count = wall_lattice(box, v.plane.axis, grid, opacity / 100.0, bars);
    for (int i = 0; i < count; ++i) reinterpret_cast<box_t>(g_box_fn)(overlay, bars[i].min, bars[i].max, transform, identity, &palette, &value);
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

// Every supported tool state keeps the vehicle it hovers or works on at the same offset.
void after_overlay(uint8_t* state, uint8_t* scene, uint8_t* overlay) {
    if (!state || !scene || !anymaker::readable(state, 16)) return;
    uint64_t now = GetTickCount64();
    int32_t hovered = rd<int32_t>(state, kStateVehicleId);
    std::lock_guard lock(g_mutex);
    refresh_settings();
    // Vehicle ids are only unique within one world, so a new client scene drops the planes kept for the last one.
    if (scene != g_scene) { g_vehicles.clear(); g_vehicle_id = -1; g_scene = scene; }
    if (g_grid_size_ptr && anymaker::readable(g_grid_size_ptr, 8) && std::isfinite(*g_grid_size_ptr) && *g_grid_size_ptr > 0.001 && *g_grid_size_ptr < 10)
        g_grid_size = *g_grid_size_ptr;
    g_tool_seen = now;
    paint_cut_struts(scene, now);
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
template <int I> void hk_overlay(uint8_t* s, void* c, void* n, uint8_t* sc, uint8_t* o) {
    reinterpret_cast<overlay_t>(h_overlay[I].original)(s, c, n, sc, o);
    after_overlay(s, sc, o);
}
template <int I> void hk_plate_overlay(uint8_t* s, void* item, void* c, void* n, uint8_t* sc, uint8_t* o) {
    reinterpret_cast<plate_overlay_t>(h_overlay[I].original)(s, item, c, n, sc, o);
    after_overlay(s, sc, o);
}
void* const kOverlayHooks[kOverlayCount] = {(void*)&hk_overlay<0>, (void*)&hk_overlay<1>, (void*)&hk_overlay<2>, (void*)&hk_overlay<3>,
    (void*)&hk_overlay<4>, (void*)&hk_overlay<5>, (void*)&hk_overlay<6>, (void*)&hk_overlay<7>, (void*)&hk_overlay<8>,
    (void*)&hk_plate_overlay<9>, (void*)&hk_plate_overlay<10>};
void* const kRemoveHooks[kRemoveCount] = {(void*)&hk_remove_edge, (void*)&hk_remove_node, (void*)&hk_remove_plate};

// ------------------------------------------------------------------ install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    const anymaker::func_desc* overlays[kOverlayCount] = {&bind::edge_hover_overlay, &bind::edge_drag_overlay, &bind::edge2_hover_overlay, &bind::edge2_drag_overlay,
        &bind::remove_hover_overlay, &bind::remove_component_overlay, &bind::remove_edge_overlay, &bind::remove_node_overlay, &bind::remove_plate_overlay,
        &bind::plate_hover_overlay, &bind::plate_select_overlay};
    const anymaker::func_desc* removes[kRemoveCount] = {&bind::client_push_remove_edge, &bind::client_push_remove_node, &bind::client_push_remove_plate};
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyMirror stays off"); return; }
        void** push = hook_cell(bind::client_push_add_edge);
        void** cells[kOverlayCount]; bool all = push != nullptr;
        for (int i = 0; i < kOverlayCount; ++i) cells[i] = hook_cell(*overlays[i]);
        // The hover states have no SDK route to their cell: find it by its entry point next to the drag state's.
        for (int i : {0, 2}) {
            if (cells[i] || !cells[i + 1]) continue;
            void* entry = function(*overlays[i]);
            cells[i] = cell_scan::find_cell(entry, cells[i + 1]);
            log(cells[i] ? 0 : 1, "edge tool %d hover overlay: entry %s, cell %s", i / 2 + 1, entry ? "found" : "missing", cells[i] ? "found by scan" : "not found");
        }
        all = all && cells[1] && cells[3];   // hover overlays are optional: without them the wall and keys work while dragging only
        // The remove and add plate tools are optional too; cells without an SDK route are found the same way, next to
        // the edge tool's (both are still unhooked here).
        auto locate = [&](const anymaker::func_desc& d) { void** c = hook_cell(d); return c || !all ? c : cell_scan::find_cell(function(d), push); };
        void* box = function(bind::overlay_add_vehicle_box);
        void* line = function(bind::overlay_add_vehicle_line);
        void* transform = function(bind::client_vehicle_render_transform);
        if (!all) {
            if (attempt == 179) log(2, "could not locate the edge tool (push=%d hover=%d drag=%d hover2=%d drag2=%d)", !!push, !!cells[0], !!cells[1], !!cells[2], !!cells[3]);
            continue;
        }
        for (int i = kEdgeOverlays; i < kOverlayCount; ++i) if (!cells[i]) cells[i] = locate(*overlays[i]);
        void** remove_cells[kRemoveCount];
        for (int i = 0; i < kRemoveCount; ++i) remove_cells[i] = locate(*removes[i]);
        void** plate_cell = locate(bind::client_push_add_plate);
        bool remove_ok = remove_cells[0] && remove_cells[1] && remove_cells[2] && cells[4], plate_ok = plate_cell && cells[9] && cells[10];
        log(remove_ok && plate_ok ? 0 : 1, "remove tool: requests %d/%d/%d, states %d%d%d%d%d; add plate tool: request %d, states %d%d",
            !!remove_cells[0], !!remove_cells[1], !!remove_cells[2], !!cells[4], !!cells[5], !!cells[6], !!cells[7], !!cells[8], !!plate_cell, !!cells[9], !!cells[10]);
        g_box_fn = box; g_line_fn = line; g_transform_fn = transform;
        g_grid_size_ptr = static_cast<const double*>(global(bind::g_vehicle_grid_size));
        if (!box || !line || !transform) log(1, "wall drawing unavailable (box=%d line=%d transform=%d); mirroring still works", !!box, !!line, !!transform);
        bool ok = h_push.install(push, (void*)&hk_push_edge);
        for (int i = 0; i < kEdgeOverlays && ok; ++i) ok = !cells[i] || h_overlay[i].install(cells[i], kOverlayHooks[i]);
        if (!ok) { log(2, "a hook cell was already taken or not writable; AnyMirror is off"); return; }
        // Optional tools: a cell that will not hook only turns that piece off.
        for (int i = kEdgeOverlays; i < kOverlayCount; ++i)
            if (cells[i] && !h_overlay[i].install(cells[i], kOverlayHooks[i])) log(1, "tool overlay %d could not be hooked", i);
        if (remove_ok) for (int i = 0; i < kRemoveCount; ++i)
            if (!h_remove[i].install(remove_cells[i], kRemoveHooks[i])) log(1, "remove %s request could not be hooked; not mirrored", kRemoveNames[i]);
        if (plate_ok && !h_add_plate.install(plate_cell, (void*)&hk_add_plate)) log(1, "add plate request could not be hooked; plates are not mirrored");
        // Bent windows: optional, needs the add plate request hook above.
        void** can_plate = locate(bind::client_can_add_plate);
        g_can_edge = reinterpret_cast<can_add_edge_t>(function(bind::client_can_add_edge));
        g_paint_edge = reinterpret_cast<paint_edge_t>(function(bind::client_push_paint_edge));
        void** is_planar = locate(bind::select_is_planar), ** is_convex = locate(bind::select_is_convex);
        bool split_ok = plate_ok && can_plate && is_planar && is_convex && g_can_edge && g_paint_edge && h_can_plate.install(can_plate, (void*)&hk_can_plate) &&
            h_is_planar.install(is_planar, (void*)&hk_is_planar) && h_is_convex.install(is_convex, (void*)&hk_is_convex);
        log(split_ok ? 0 : 1, "bent window split %s (check %d, pick %d%d, strut check %d, paint %d)", split_ok ? "ready" : "unavailable", !!can_plate,
            !!is_planar, !!is_convex, !!g_can_edge, !!g_paint_edge);
        g_ready = true;
        log(0, "Ready after %d s: grid size %s, wall %s. Equip an edge tool and press the mirror key.", attempt,
            g_grid_size_ptr ? "found" : "default 0.25 m", g_box_fn ? "available" : "unavailable");
        return;
    }
}
void remove_hooks() {
    h_is_convex.remove((void*)&hk_is_convex);
    h_is_planar.remove((void*)&hk_is_planar);
    h_can_plate.remove((void*)&hk_can_plate);
    h_add_plate.remove((void*)&hk_add_plate);
    for (int i = kRemoveCount - 1; i >= 0; --i) h_remove[i].remove(kRemoveHooks[i]);
    for (int i = kOverlayCount - 1; i >= 0; --i) h_overlay[i].remove(kOverlayHooks[i]);
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
    swprintf_s(line1, L"MIRROR %ls  ·  WALL %ls  ·  %hs AXIS  ·  plane %.1f", g_enabled ? L"ON" : L"OFF", g_wall ? L"ON" : L"OFF", kAxisNames[g_axis], plane_text());
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
    case kToggle: g_enabled = !g_enabled; flash(g_enabled ? L"Mirror on: building and removing in pairs" : L"Mirror off"); break;
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
        const char* labels[] = {"Show mirror status while a building tool is equipped", "Show the mirror wall by default", "Mirror wall colour", "Mirror wall strength (%)"};
        uint32_t kinds[] = {ANY_SETTING_BOOL, ANY_SETTING_BOOL, ANY_SETTING_CHOICE, ANY_SETTING_INTEGER};
        double lo[] = {0, 0, 0, 10}, hi[] = {1, 1, 3, 100}, step[] = {1, 1, 1, 5};
        for (int i = 0; i < kSettingCount; ++i) {
            AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyMirror"; d.setting_id = ids[i]; d.label = labels[i]; d.order = i; d.kind = kinds[i];
            d.default_number = g_values[i]; d.minimum = lo[i]; d.maximum = hi[i]; d.step = step[i];
            if (i == kWallColour) { d.choices = kPaletteNames; d.choice_count = 4; }
            if (i == 0) d.description = "Mirroring works with the edge, add plate and remove tools. Keys are in Controls under AnyMirror.";
            g_tokens[i] = settings->register_setting(&d);
        }
        AnyModSettingV1 split; split.mod_id = kModId; split.mod_name = "AnyMirror"; split.setting_id = "split_bent_windows"; split.order = kSettingCount;
        split.label = "Split bent windows"; split.default_number = 1;
        split.description = "Lets the window tool take a loop of struts that bends: it is cut into flat windows joined by hidden struts (paint C68).";
        g_split_token = settings->register_setting(&split);
    } else settings = nullptr;
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
