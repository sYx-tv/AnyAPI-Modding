// AnyLights (prototype): colour and flash patterns for spot, omni and rotating lights.
//
// Properties tool: aim at a light and a panel opens beside the Properties window with colour swatches, RGB
// sliders, eight patterns (steady, blink, strobe, double flash, pulse, alternate A/B, colour cycle) and a
// speed. The choice is saved in the light's name as a short tag (e.g. "Beacon ALff220032"), which the game
// already saves with the vehicle and lets every player edit.
//
// Data port and microcontroller: on the host every light gains data channels. Inputs colour_r, colour_g,
// colour_b (0-1, or 0-255), pattern (0-7) and pattern_speed (Hz) override the Properties choice while they
// are connected. Output pattern_lit is 1 while the pattern is in its on phase, so logic can follow it.
//
// Co-op: the host turns the name tag and the data inputs into one 32-bit word per light and sends it to
// every player with the game's component event, re-sending it every few seconds for late joiners. Players
// with AnyLights see the colours and patterns; players without it see stock lights. The host needs the mod.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anylights_bindings.h): Anymaker 0.1.24 /
// Steam build 25826614 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_experimental.hpp"
#include "anylights_bindings.h"
#include "anylights_logic.h"
#include "cell_scan.h"
#include "vehicle_reads.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anylights {
namespace {
using namespace vehicle_reads;
constexpr const char* kModId = "anylights";

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
double now_seconds() { return double(GetTickCount64()) / 1000.0; }

enum Setting { kEnabled, kChannels, kPanel, kSettingCount };
uint64_t g_tokens[kSettingCount]{}, g_revision = UINT64_MAX;
std::atomic<bool> g_enabled{true}, g_channels{true}, g_panel_enabled{true};
void refresh_settings() {
    if (!settings) return;
    uint64_t next = settings->revision();
    if (next == g_revision) return;
    g_revision = next;
    std::atomic<bool>* targets[kSettingCount] = {&g_enabled, &g_channels, &g_panel_enabled};
    for (int i = 0; i < kSettingCount; ++i) { AnySettingValueV1 v; if (settings->get(g_tokens[i], &v)) targets[i]->store(v.number != 0); }
}

// Light kinds and their field offsets (reference layouts, metadata).
enum Kind { kSpot, kOmni, kRotate, kKindCount };
const char* const kKindNames[kKindCount] = {"spot", "omni", "rotating"};
namespace off {
constexpr ptrdiff_t server_alias[kKindCount] = {824, 664, 712};       // m_user_defined_alias (string)
constexpr ptrdiff_t client_factor = 224 + 8;                         // m_light_factor _m_value (f64), all kinds
constexpr ptrdiff_t client_colour[kKindCount] = {336, 264, 288};      // m_color (color8)
// client_scene.peer.property_data
constexpr ptrdiff_t pd_vehicle = 8 + 8, pd_component = 24 + 8, pd_rows = 40 + 8;
constexpr ptrdiff_t row_id = 8, row_name = 16 + 8, row_value = 32, row_value_prev = 56;   // _m_value (replicated wrapper), _m_value_prev (string)
constexpr ptrdiff_t client_peer_data = 40 + 104;                      // client.m_peers.m_data
constexpr ptrdiff_t client_peers = 3920 + 64 + 8;                     // client.m_scene.m_peers._m_elements (vector<ref<peer>>)
constexpr ptrdiff_t peer_property_data = 24 + 2848;                   // peer.m_private.m_property_data
}  // namespace off
constexpr int32_t kAliasLabel = 307;                                  // e_localization_string.property_component_alias

const uint8_t* g_server_types[kKindCount];
const uint8_t* g_client_types[kKindCount];
int kind_of(const uint8_t* object, const uint8_t* const* types) {
    if (!object || !anymaker::readable(object, 8)) return -1;
    auto ti = anymaker::typeinfo_of(object);
    for (int k = 0; k < kKindCount; ++k) if (types[k] && anymaker::typeinfo_is(ti, types[k])) return k;
    return -1;
}
const uint8_t* typeinfo_for(const anymaker::func_desc& d) {
    if (!d.ti_anchor_sig || !*d.ti_anchor_sig) return nullptr;
    uint8_t* anchor = anymaker::find_gcl_function(d.ti_anchor_sig);
    auto* ti = anchor ? reinterpret_cast<const uint8_t*>(anymaker::read_slot(anchor, d.ti_slot_offset)) : nullptr;
    return ti && anymaker::readable(ti, 0x88) ? ti : nullptr;
}

// Game strings built once with the game's own constructor and never freed (descriptor names).
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using string_dtor_t = void (*)(anymaker::gc_string_view*);
string_ctor_t g_string_ctor; string_dtor_t g_string_dtor;

// ================================================================== host side (server thread)
struct Channel { const char* name; int32_t type; const char* doc; };
constexpr int32_t kF64Input = 2, kF64Output = 6;
const Channel kChannelsDef[] = {
    {"colour_r", kF64Input, "AnyLights: red 0-1 (or 0-255). Overrides the Properties colour while connected."},
    {"colour_g", kF64Input, "AnyLights: green 0-1 (or 0-255)."},
    {"colour_b", kF64Input, "AnyLights: blue 0-1 (or 0-255)."},
    {"pattern", kF64Input, "AnyLights: 0 steady, 1 blink, 2 strobe, 3 double flash, 4 pulse, 5 alternate A, 6 alternate B, 7 colour cycle."},
    {"pattern_speed", kF64Input, "AnyLights: pattern speed in Hz (0.25-6)."},
    {"pattern_lit", kF64Output, "AnyLights: 1 while the light's pattern is in its on phase, else 0."},
};
constexpr int kChannelCount = int(sizeof kChannelsDef / sizeof kChannelsDef[0]);

struct HostLight {
    double slots[kChannelCount] = {NAN, NAN, NAN, NAN, NAN, 0};
    std::string alias; Config chosen; bool parsed = false;
    int32_t sent = 0; bool has_sent = false; double sent_at = -1e9;
};
std::unordered_map<const uint8_t*, HostLight> g_host;   // keyed by server component (stable while it exists)
std::mutex g_host_mutex;                                // server tick and data lookups may differ in thread

// Descriptor vectors extended with our channels, keyed by the definition's own vector.
struct DescriptorRecord { int32_t type; uint32_t pad; anymaker::gc_string_view name, doc; };
static_assert(sizeof(DescriptorRecord) == 40, "vehicle_data_descriptor");
struct Extended { anymaker::gc_vector_raw vector; std::vector<DescriptorRecord> records; int32_t source_count; };
std::unordered_map<const void*, Extended*> g_extended;
DescriptorRecord g_ours[kChannelCount];
bool g_ours_built = false;

using server_tick_t = void (*)(uint8_t* light, void* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, uint8_t* light, const anymaker::gc_string_view* name);
using descriptors_t = void (*)(const anymaker::gc_vector_raw** ret, uint8_t* component);
using push_ability_t = void (*)(uint8_t* component, const int32_t* value);
anymaker::cell_hook h_server_tick[kKindCount], h_get_f64[kKindCount], h_descriptors;
void* g_push_ability;
std::atomic<int> g_send_logs{12}, g_descriptor_logs{4}, g_lookup_logs{8};

void server_tick(int kind, uint8_t* light, void* vehicle, void* scene) {
    reinterpret_cast<server_tick_t>(h_server_tick[kind].original)(light, vehicle, scene);
    if (!light || !g_enabled.load() || !g_push_ability) return;
    char alias[300];
    copy_string(light + off::server_alias[kind], alias, sizeof alias);
    std::lock_guard lock(g_host_mutex);
    auto& h = g_host[light];
    if (!h.parsed || h.alias != alias) {
        static std::atomic<int> alias_logs{8};
        if (h.parsed && alias_logs.fetch_sub(1) > 0) log(0, "host %s light renamed to \"%s\"", kKindNames[kind], alias);
        h.alias = alias; h.parsed = true;
        std::string base;
        split_name(h.alias, base, h.chosen);
    }
    Inputs in{h.slots[0], h.slots[1], h.slots[2], h.slots[3], h.slots[4]};
    Config eff = effective(h.chosen, in);
    double t = now_seconds();
    h.slots[5] = lit(eff.pattern, eff.speed, t) ? 1.0 : 0.0;
    if (!eff.modded() && !h.has_sent) return;                       // never touched: stay vanilla, send nothing
    int32_t word = eff.modded() ? pack(eff) : int32_t(kMarker);
    bool changed = !h.has_sent || word != h.sent;
    if (!(changed ? t - h.sent_at >= 0.05 : t - h.sent_at >= 3.0)) return;
    if (!changed && !eff.modded()) return;                           // stock again and already told everyone
    reinterpret_cast<push_ability_t>(g_push_ability)(light, &word);
    if (changed && g_send_logs.fetch_sub(1) > 0)
        log(0, "host %s light: name \"%s\" -> word 0x%08x (pattern %s, colour %s #%02x%02x%02x)", kKindNames[kind], alias, unsigned(word),
            kPatternNames[eff.pattern], eff.has_colour ? "custom" : "stock", eff.colour.r, eff.colour.g, eff.colour.b);
    h.sent = word; h.has_sent = true; h.sent_at = t;
}
void hk_server_spot(uint8_t* l, void* v, void* s) { server_tick(kSpot, l, v, s); }
void hk_server_omni(uint8_t* l, void* v, void* s) { server_tick(kOmni, l, v, s); }
void hk_server_rotate(uint8_t* l, void* v, void* s) { server_tick(kRotate, l, v, s); }

void get_f64(int kind, double** ret, uint8_t* light, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64[kind].original)(ret, light, name);
    if (!ret || *ret || !light || !name || !g_enabled.load() || !g_channels.load()) return;
    auto view = name->view();
    for (int i = 0; i < kChannelCount; ++i) {
        if (view != kChannelsDef[i].name) continue;
        std::lock_guard lock(g_host_mutex);
        *ret = &g_host[light].slots[i];   // unordered_map nodes keep their address
        if (g_lookup_logs.fetch_sub(1) > 0) log(0, "data channel %s connected on a %s light", kChannelsDef[i].name, kKindNames[kind]);
        return;
    }
}
void hk_get_spot(double** r, uint8_t* l, const anymaker::gc_string_view* n) { get_f64(kSpot, r, l, n); }
void hk_get_omni(double** r, uint8_t* l, const anymaker::gc_string_view* n) { get_f64(kOmni, r, l, n); }
void hk_get_rotate(double** r, uint8_t* l, const anymaker::gc_string_view* n) { get_f64(kRotate, r, l, n); }

bool build_ours() {
    if (g_ours_built) return true;
    if (!g_string_ctor) return false;
    for (int i = 0; i < kChannelCount; ++i) {
        g_ours[i] = {};
        g_ours[i].type = kChannelsDef[i].type;
        g_string_ctor(&g_ours[i].name, kChannelsDef[i].name);
        g_string_ctor(&g_ours[i].doc, kChannelsDef[i].doc);
        if (g_ours[i].name.view() != kChannelsDef[i].name) { log(2, "game string constructor misbehaved; data channels disabled"); return false; }
    }
    return g_ours_built = true;
}

void hk_descriptors(const anymaker::gc_vector_raw** ret, uint8_t* component) {
    reinterpret_cast<descriptors_t>(h_descriptors.original)(ret, component);
    if (!ret || !*ret || !g_enabled.load() || !g_channels.load()) return;
    int kind = kind_of(component, g_server_types);
    static std::atomic<int> calls{3};
    if (calls.fetch_sub(1) > 0) log(0, "data descriptors requested (%s)", kind < 0 ? "not a light" : kKindNames[kind]);
    if (kind < 0) return;
    const anymaker::gc_vector_raw* source = *ret;
    if (!anymaker::readable(source, sizeof *source) || source->element_size != sizeof(DescriptorRecord) || source->count < 0 || source->count > 256) return;
    std::lock_guard lock(g_host_mutex);
    if (!build_ours()) return;
    auto& ext = g_extended[source];
    if (!ext || ext->source_count != source->count) {
        auto* e = new Extended{};   // lives for the process, like the definition it extends
        for (int32_t i = 0; i < source->count; ++i) if (auto* r = source->at<DescriptorRecord>(i)) e->records.push_back(*r);
        if (g_descriptor_logs.fetch_sub(1) > 0) {
            std::string names;
            for (auto& r : e->records) names += std::string(r.name.view()) + "(" + std::to_string(r.type) + ") ";
            log(0, "%s light data channels: %s+ %d AnyLights channels", kKindNames[kind], names.c_str(), kChannelCount);
        }
        for (auto& r : g_ours) e->records.push_back(r);
        e->vector = *source;   // keeps the game's unknown trailing fields
        e->vector.buffer = reinterpret_cast<uint8_t*>(e->records.data());
        e->vector.offset = 0;
        e->vector.count = e->vector.capacity = int32_t(e->records.size());
        e->source_count = source->count;
        ext = e;
    }
    *ret = &ext->vector;
}

// ================================================================== player side (main thread)
struct ClientLight { Config config; bool modded = false; double received = 0; };
std::unordered_map<const uint8_t*, ClientLight> g_client;   // keyed by client component
struct Known { const uint8_t* component; int kind; uint64_t seen; };
std::unordered_map<uint64_t, Known> g_known;                // (vehicle id, component id) -> light
uint64_t key_of(int32_t vehicle, int32_t component) { return (uint64_t(uint32_t(vehicle)) << 32) | uint32_t(component); }

using client_tick_t = void (*)(uint8_t* light, void* client, uint8_t* vehicle, void* scene, const double* dt);
using client_render_t = void (*)(uint8_t* light, void* renderer, const void* mask, void* scene, void* constants, const void* transform);
using ability_t = void (*)(uint8_t* component, const int32_t* value);
anymaker::cell_hook h_client_tick[kKindCount], h_client_render[kKindCount], h_ability;
std::atomic<int> g_receive_logs{12};

void hk_ability(uint8_t* component, const int32_t* value) {
    reinterpret_cast<ability_t>(h_ability.original)(component, value);
    if (!component || !value || !is_word(*value) || kind_of(component, g_client_types) < 0) return;
    auto& c = g_client[component];
    c.config = unpack(*value); c.modded = c.config.modded(); c.received = now_seconds();
    if (g_receive_logs.fetch_sub(1) > 0) log(0, "received light word 0x%08x (pattern %s)", unsigned(*value), kPatternNames[c.config.pattern]);
}

// Applies a light's colour and pattern for the duration of one game call, then restores the replicated values.
struct Override {
    uint8_t* light = nullptr; int kind = -1; uint32_t colour = 0; double factor = 0; bool active = false;
    Override(uint8_t* l, int k) : light(l), kind(k) {
        if (!light || !g_enabled.load()) return;
        auto it = g_client.find(light);
        if (it == g_client.end() || !it->second.modded) return;
        const Config& c = it->second.config;
        double t = now_seconds();
        colour = rd<uint32_t>(light, off::client_colour[kind]);
        factor = rd<double>(light, off::client_factor);
        Colour col;
        if (colour_at(c, t, col)) wr<uint32_t>(light, off::client_colour[kind], uint32_t(col.r) | uint32_t(col.g) << 8 | uint32_t(col.b) << 16 | (colour & 0xff000000u));
        if (std::isfinite(factor)) wr<double>(light, off::client_factor, factor * level(c.pattern, c.speed, t));
        active = true;
    }
    ~Override() {
        if (!active) return;
        wr<uint32_t>(light, off::client_colour[kind], colour);
        wr<double>(light, off::client_factor, factor);
    }
};

void client_tick(int kind, uint8_t* light, void* client, uint8_t* vehicle, void* scene, const double* dt) {
    if (light && vehicle && anymaker::readable(vehicle, 16)) {
        int32_t vid = rd<int32_t>(vehicle, vehicle_reads::off::vehicle_id), cid = rd<int32_t>(light, vehicle_reads::off::component_id);
        g_known[key_of(vid, cid)] = {light, kind, GetTickCount64()};
        if (g_known.size() > 4096) { auto now = GetTickCount64(); for (auto it = g_known.begin(); it != g_known.end();) it = now - it->second.seen > 5000 ? g_known.erase(it) : std::next(it); }
    }
    Override o(light, kind);
    reinterpret_cast<client_tick_t>(h_client_tick[kind].original)(light, client, vehicle, scene, dt);
}
void hk_client_spot(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { client_tick(kSpot, l, c, v, s, d); }
void hk_client_omni(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { client_tick(kOmni, l, c, v, s, d); }
void hk_client_rotate(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { client_tick(kRotate, l, c, v, s, d); }
void client_render(int kind, uint8_t* l, void* r, const void* m, void* s, void* cb, const void* t) {
    Override o(l, kind);
    reinterpret_cast<client_render_t>(h_client_render[kind].original)(l, r, m, s, cb, t);
}
void hk_render_spot(uint8_t* l, void* r, const void* m, void* s, void* cb, const void* t) { client_render(kSpot, l, r, m, s, cb, t); }
void hk_render_omni(uint8_t* l, void* r, const void* m, void* s, void* cb, const void* t) { client_render(kOmni, l, r, m, s, cb, t); }
void hk_render_rotate(uint8_t* l, void* r, const void* m, void* s, void* cb, const void* t) { client_render(kRotate, l, r, m, s, cb, t); }

// ------------------------------------------------------------------ Properties panel state (mutex)
std::mutex g_panel_mutex;
struct Panel {
    uint64_t seen = 0; int32_t row_id = 0; int kind = 0;
    std::string base; Config saved, draft, live; bool live_known = false;
    bool pending = false; uint64_t last_change = 0, last_send = 0;
    int dragging = -1;
    bool visible = false;
    // last layout (render thread) for input hit tests
    float x = 0, y = 0, w = 0, s = 1;
} g_panel;

using property_ui_t = void (*)(uint8_t* pd, void* frontend, const void* request, uint8_t* client, void* ui_data);
using push_string_t = void (*)(uint8_t* peer_data, const int32_t* id, const anymaker::gc_string_view* value);
anymaker::cell_hook h_property_ui;
void** g_push_string_cell;
std::atomic<int> g_panel_logs{6};

void panel_update(uint8_t* pd, uint8_t* client);
void hk_property_ui(uint8_t* pd, void* frontend, const void* request, uint8_t* client, void* ui_data) {
    reinterpret_cast<property_ui_t>(h_property_ui.original)(pd, frontend, request, client, ui_data);
    panel_update(pd, client);
}

// Fallback when the window-level hook is unavailable: the name row's own update runs every frame the
// Properties window shows it. Find the property data that owns the row among the client's peers.
using string_row_t = void (*)(uint8_t* row, void* frontend, uint8_t* client, const int32_t* a, const int32_t* b);
anymaker::cell_hook h_string_row;
std::atomic<int> g_row_logs{3};
void hk_string_row(uint8_t* row, void* frontend, uint8_t* client, const int32_t* a, const int32_t* b) {
    reinterpret_cast<string_row_t>(h_string_row.original)(row, frontend, client, a, b);
    if (!row || !client || !anymaker::readable(row, 64) || rd<int32_t>(row, off::row_name) != kAliasLabel) return;
    uint8_t* owner = nullptr;
    for_each_ref(client + off::client_peers, 256, [&](uint8_t* peer) {
        uint8_t* pd = peer + off::peer_property_data;
        if (!anymaker::readable(pd, 96)) return true;
        for_each_ref(pd + off::pd_rows, 128, [&](uint8_t* r) { if (r == row) owner = pd; return !owner; });
        return !owner;
    });
    if (g_row_logs.fetch_sub(1) > 0) log(0, "name row seen: owner property data %s", owner ? "found" : "not found");
    if (owner) panel_update(owner, client);
}

void panel_update(uint8_t* pd, uint8_t* client) {
    if (!pd || !g_enabled.load() || !g_panel_enabled.load() || !anymaker::readable(pd, 96)) return;
    auto it = g_known.find(key_of(rd<int32_t>(pd, off::pd_vehicle), rd<int32_t>(pd, off::pd_component)));
    if (it == g_known.end() || GetTickCount64() - it->second.seen > 2000) return;
    int32_t row_id = 0; char alias[300] = {}; bool found = false;
    for_each_ref(pd + off::pd_rows, 128, [&](uint8_t* row) {
        if (!anymaker::readable(row, 96) || rd<int32_t>(row, off::row_name) != kAliasLabel) return true;
        row_id = rd<int32_t>(row, off::row_id);
        // The replicated wrapper's layout is unverified; the plain previous-value string tracks it every frame.
        copy_string(row + off::row_value_prev, alias, sizeof alias);
        static std::atomic<int> dumps{2};
        if (dumps.fetch_sub(1) > 0) {
            char hex[3 * 24 + 1] = {};
            for (int i = 0; i < 24; ++i) snprintf(hex + i * 3, 4, "%02x ", rd<uint8_t>(row, off::row_value + i));
            log(0, "name row %d: value_prev \"%s\", value wrapper bytes %s", rd<int32_t>(row, off::row_id), alias, hex);
        }
        found = true;
        return false;
    });
    if (!found) return;
    std::string send_text; bool send = false;
    {
        std::lock_guard lock(g_panel_mutex);
        uint64_t now = GetTickCount64();
        bool fresh = g_panel.row_id != row_id || now - g_panel.seen > 1000;
        g_panel.seen = now; g_panel.row_id = row_id; g_panel.kind = it->second.kind;
        Config saved; std::string base;
        split_name(alias, base, saved);
        base = clean_text(base);
        if (fresh || (!g_panel.pending && g_panel.dragging < 0 && now - g_panel.last_send > 1500)) { g_panel.draft = saved; }
        if (fresh) g_panel.pending = false;
        g_panel.base = base; g_panel.saved = saved;
        auto cl = g_client.find(it->second.component);
        g_panel.live_known = cl != g_client.end() && now_seconds() - cl->second.received < 10;
        if (g_panel.live_known) g_panel.live = cl->second.config;
        if (g_panel.pending && now - g_panel.last_change >= 150 && now - g_panel.last_send >= 150) {
            send_text = join_name(g_panel.base, g_panel.draft); send = true;
            g_panel.pending = false; g_panel.last_send = now;
        }
        if (fresh && g_panel_logs.fetch_sub(1) > 0) log(0, "panel: %s light, name row %d \"%s\"", kKindNames[g_panel.kind], row_id, alias);
    }
    if (send && client && g_push_string_cell && *g_push_string_cell && g_string_ctor && g_string_dtor) {
        // $string_ctor_cstr builds a non-owning view of the caller's characters, the event keeps that view, and the
        // game sends it after this returns. So the characters (not just the view) must outlive the call: keep the
        // last few names sent in a ring, reused only long after their event went out (sends are 150 ms apart).
        static std::string kept[32];
        static anymaker::gc_string_view sent[32];
        static int next;
        int slot = next;
        next = (next + 1) % 32;
        if (sent[slot].data) g_string_dtor(&sent[slot]);
        sent[slot] = {};
        kept[slot] = send_text;
        g_string_ctor(&sent[slot], kept[slot].c_str());
        reinterpret_cast<push_string_t>(*g_push_string_cell)(client + off::client_peer_data, &row_id, &sent[slot]);
        log(0, "light name -> \"%s\"", send_text.c_str());
    }
}

// ================================================================== install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    const anymaker::func_desc* server_ticks[kKindCount] = {&bind::server_spot_tick, &bind::server_omni_tick, &bind::server_rotate_tick};
    const anymaker::func_desc* get_f64s[kKindCount] = {&bind::server_spot_get_data_f64, &bind::server_omni_get_data_f64, &bind::server_rotate_get_data_f64};
    const anymaker::func_desc* client_ticks[kKindCount] = {&bind::client_spot_tick, &bind::client_omni_tick, &bind::client_rotate_tick};
    const anymaker::func_desc* client_renders[kKindCount] = {&bind::client_spot_render, &bind::client_omni_render, &bind::client_rotate_render};
    void* server_hooks[kKindCount] = {(void*)&hk_server_spot, (void*)&hk_server_omni, (void*)&hk_server_rotate};
    void* get_hooks[kKindCount] = {(void*)&hk_get_spot, (void*)&hk_get_omni, (void*)&hk_get_rotate};
    void* client_hooks[kKindCount] = {(void*)&hk_client_spot, (void*)&hk_client_omni, (void*)&hk_client_rotate};
    void* render_hooks[kKindCount] = {(void*)&hk_render_spot, (void*)&hk_render_omni, (void*)&hk_render_rotate};
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyLights stays off"); return; }
        void** st[kKindCount]; void** gf[kKindCount]; void** ct[kKindCount]; void** cr[kKindCount];
        bool all = true;
        for (int k = 0; k < kKindCount; ++k) {
            st[k] = hook_cell(*server_ticks[k]); gf[k] = hook_cell(*get_f64s[k]); ct[k] = hook_cell(*client_ticks[k]); cr[k] = hook_cell(*client_renders[k]);
            g_server_types[k] = typeinfo_for(*server_ticks[k]); g_client_types[k] = typeinfo_for(*client_ticks[k]);
            all = all && st[k] && gf[k] && ct[k] && cr[k] && g_server_types[k] && g_client_types[k];
        }
        void** descriptors = hook_cell(bind::server_component_get_data_descriptors);
        void** ability = hook_cell(bind::client_component_on_event_ability);
        void** property_ui = hook_cell(bind::client_property_window_ui);
        void** push_string = hook_cell(bind::client_push_set_property_string);
        void* push_ability = function(bind::server_push_component_ability);
        g_string_ctor = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        g_string_dtor = reinterpret_cast<string_dtor_t>(native(anymaker::sym::string_dtor_rva));
        all = all && descriptors && ability && push_string && push_ability && g_string_ctor && g_string_dtor;
        if (!all) {
            if (attempt == 179) {
                log(2, "could not locate the light functions (descriptors=%d ability=%d property_ui=%d push_string=%d push_ability=%d strings=%d)",
                    !!descriptors, !!ability, !!property_ui, !!push_string, !!push_ability, g_string_ctor && g_string_dtor);
                for (int k = 0; k < kKindCount; ++k) log(2, "  %s: server tick %d, get_data_f64 %d, client tick %d, render %d, types %d/%d", kKindNames[k],
                    !!st[k], !!gf[k], !!ct[k], !!cr[k], !!g_server_types[k], !!g_client_types[k]);
            }
            continue;
        }
        void** string_row = hook_cell(bind::client_property_string_row_ui);
        // The Properties window update has no SDK route to its cell: find it by its entry point near another client cell.
        if (!property_ui) {
            void* entry = function(bind::client_property_window_ui);
            property_ui = cell_scan::find_cell(entry, push_string);
            log(property_ui ? 0 : 1, "Properties window hook: entry %s, cell %s", entry ? "found" : "missing", property_ui ? "found by scan" : "not found");
        }
        g_push_ability = push_ability; g_push_string_cell = push_string;
        bool ok = h_descriptors.install(descriptors, (void*)&hk_descriptors) && h_ability.install(ability, (void*)&hk_ability) &&
                  (!property_ui || h_property_ui.install(property_ui, (void*)&hk_property_ui));
        if (!property_ui && string_row) {
            ok = ok && h_string_row.install(string_row, (void*)&hk_string_row);
            log(0, "Properties panel follows the light's name row instead");
        }
        if (!property_ui && !string_row) log(1, "the Properties panel is unavailable; name tags and data channels still work");
        for (int k = 0; k < kKindCount && ok; ++k)
            ok = h_server_tick[k].install(st[k], server_hooks[k]) && h_get_f64[k].install(gf[k], get_hooks[k]) &&
                 h_client_tick[k].install(ct[k], client_hooks[k]) && h_client_render[k].install(cr[k], render_hooks[k]);
        if (!ok) { log(2, "a hook cell was already taken or not writable; AnyLights is off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: aim the Properties tool at a light. Data channels: colour_r/g/b, pattern, pattern_speed, pattern_lit.", attempt);
        return;
    }
}
void remove_hooks() {
    void* server_hooks[kKindCount] = {(void*)&hk_server_spot, (void*)&hk_server_omni, (void*)&hk_server_rotate};
    void* get_hooks[kKindCount] = {(void*)&hk_get_spot, (void*)&hk_get_omni, (void*)&hk_get_rotate};
    void* client_hooks[kKindCount] = {(void*)&hk_client_spot, (void*)&hk_client_omni, (void*)&hk_client_rotate};
    void* render_hooks[kKindCount] = {(void*)&hk_render_spot, (void*)&hk_render_omni, (void*)&hk_render_rotate};
    for (int k = kKindCount - 1; k >= 0; --k) {
        h_client_render[k].remove(render_hooks[k]); h_client_tick[k].remove(client_hooks[k]);
        h_get_f64[k].remove(get_hooks[k]); h_server_tick[k].remove(server_hooks[k]);
    }
    h_string_row.remove((void*)&hk_string_row); h_property_ui.remove((void*)&hk_property_ui); h_ability.remove((void*)&hk_ability); h_descriptors.remove((void*)&hk_descriptors);
}

// ================================================================== panel drawing and input (render / input threads)
struct Swatch { Colour c; const wchar_t* name; bool stock; };
const Swatch kSwatches[] = {
    {{}, L"Stock", true}, {{255, 255, 255}, L"White", false}, {{255, 214, 160}, L"Warm", false}, {{255, 32, 24}, L"Red", false},
    {{255, 110, 20}, L"Orange", false}, {{255, 176, 0}, L"Amber", false}, {{255, 236, 60}, L"Yellow", false}, {{40, 255, 80}, L"Green", false},
    {{0, 230, 255}, L"Cyan", false}, {{30, 90, 255}, L"Blue", false}, {{150, 60, 255}, L"Purple", false}, {{255, 70, 200}, L"Pink", false},
};
constexpr int kSwatchCount = int(sizeof kSwatches / sizeof kSwatches[0]);
constexpr int kSliderR = 0, kSliderG = 1, kSliderB = 2, kSliderSpeed = 3;

struct Layout {
    float x, y, w, s, pad;
    float swatch_y, swatch_size, slider_y, slider_h, track_x, track_w, pattern_y, button_w, button_h, speed_y, foot_y, h;
};
Layout layout(float frame_w, float frame_h) {
    Layout l{};
    l.s = std::clamp(frame_h / 1080.f, .7f, 1.8f);
    float s = l.s;
    l.w = 380 * s; l.pad = 14 * s;
    l.swatch_y = 56 * s; l.swatch_size = 24 * s;
    l.slider_y = l.swatch_y + l.swatch_size * 2 + 14 * s; l.slider_h = 24 * s;
    l.track_x = 70 * s; l.track_w = l.w - l.track_x - 60 * s;
    l.pattern_y = l.slider_y + l.slider_h * 3 + 18 * s; l.button_h = 26 * s; l.button_w = (l.w - l.pad * 2 - 3 * 6 * s) / 4;
    l.speed_y = l.pattern_y + l.button_h * 2 + 6 * s + 12 * s;
    l.foot_y = l.speed_y + l.slider_h + 12 * s;
    l.h = l.foot_y + 40 * s;
    l.x = std::max(0.f, frame_w - l.w - 24 * s);
    l.y = std::max(8 * s, frame_h * .5f - l.h * .5f);
    return l;
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    refresh_settings();
    std::lock_guard lock(g_panel_mutex);
    g_panel.visible = g_ready && frame->focused && g_enabled.load() && g_panel_enabled.load() && GetTickCount64() - g_panel.seen < 300;
    if (!g_panel.visible) { g_panel.dragging = -1; return; }
    Layout l = layout(float(frame->width), float(frame->height));
    g_panel.x = l.x; g_panel.y = l.y; g_panel.w = l.w; g_panel.s = l.s;
    float s = l.s;
    auto rect = [&](uint32_t kind, float rx, float ry, float rw, float rh, uint32_t color, float radius = 0, uint32_t flags = 0, float stroke = 1) {
        AnyGpuCommandV1 c; c.kind = kind; c.rect[0] = rx; c.rect[1] = ry; c.rect[2] = rw; c.rect[3] = rh; c.color = color; c.radius = radius; c.flags = flags; c.stroke = stroke; gpu->emit(&c); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = tx; c.rect[1] = ty; c.rect[2] = tw; c.rect[3] = th; c.text = t; c.text_length = uint32_t(wcslen(t));
        c.font_size = size; c.color = color; c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&c); };
    const Config& d = g_panel.draft;
    rect(ANY_GPU_ROUND_RECT, l.x, l.y, l.w, l.h, 0xf0191c20, 6 * s);
    text(l.x + l.pad, l.y + 8 * s, l.w - l.pad * 2, 18 * s, L"ANYLIGHTS", 11 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    wchar_t buf[160];
    Colour shown; bool custom = colour_at(d, now_seconds(), shown);
    swprintf_s(buf, L"%hs light · %hs%ls", kKindNames[g_panel.kind], kPatternNames[d.pattern], g_panel.pending ? L" (saving)" : L"");
    text(l.x + l.pad, l.y + 26 * s, l.w - l.pad * 2 - 40 * s, 24 * s, buf, 16 * s, 0xfff5f7fa);
    // live preview dot
    uint32_t preview = custom ? 0xff000000u | uint32_t(shown.r) << 16 | uint32_t(shown.g) << 8 | shown.b : 0xffd8d2c0;
    float lvl = float(level(d.pattern, d.speed, now_seconds()));
    rect(ANY_GPU_ELLIPSE, l.x + l.w - l.pad - 24 * s, l.y + 28 * s, 22 * s, 22 * s, 0xff2a2f36);
    AnyGpuCommandV1 dot; dot.kind = ANY_GPU_ELLIPSE; dot.rect[0] = l.x + l.w - l.pad - 21 * s; dot.rect[1] = l.y + 31 * s; dot.rect[2] = dot.rect[3] = 16 * s; dot.color = preview; dot.opacity = .15f + .85f * lvl; gpu->emit(&dot);
    // swatches: two rows of six
    for (int i = 0; i < kSwatchCount; ++i) {
        float sx = l.x + l.pad + float(i % 6) * ((l.w - l.pad * 2) / 6), sy = l.y + l.swatch_y + float(i / 6) * (l.swatch_size + 4 * s);
        float sw = (l.w - l.pad * 2) / 6 - 6 * s;
        bool selected = kSwatches[i].stock ? !d.has_colour : (d.has_colour && d.colour == kSwatches[i].c);
        uint32_t fill = kSwatches[i].stock ? 0xff3a4048 : 0xff000000u | uint32_t(kSwatches[i].c.r) << 16 | uint32_t(kSwatches[i].c.g) << 8 | kSwatches[i].c.b;
        rect(ANY_GPU_ROUND_RECT, sx, sy, sw, l.swatch_size, fill, 4 * s);
        if (kSwatches[i].stock) text(sx, sy, sw, l.swatch_size, L"Stock", 10 * s, 0xffdfe4ea, ANY_GPU_CENTER);
        if (selected) rect(ANY_GPU_ROUND_RECT, sx - 2 * s, sy - 2 * s, sw + 4 * s, l.swatch_size + 4 * s, 0xffffffff, 5 * s, ANY_GPU_STROKE, 2 * s);
    }
    // RGB and speed sliders
    auto slider = [&](float sy, const wchar_t* label, float t, uint32_t color, const wchar_t* value, bool dim) {
        text(l.x + l.pad, sy, l.track_x - l.pad, l.slider_h, label, 12 * s, dim ? 0xff6b7480 : 0xffdfe4ea);
        float ty = sy + l.slider_h * .5f - 2 * s;
        rect(ANY_GPU_ROUND_RECT, l.x + l.track_x, ty, l.track_w, 4 * s, 0xff3a4048, 2 * s);
        rect(ANY_GPU_ROUND_RECT, l.x + l.track_x, ty, l.track_w * t, 4 * s, dim ? 0xff4a525c : color, 2 * s);
        rect(ANY_GPU_ELLIPSE, l.x + l.track_x + l.track_w * t - 6 * s, ty - 4 * s, 12 * s, 12 * s, dim ? 0xff4a525c : color);
        text(l.x + l.track_x + l.track_w + 8 * s, sy, 50 * s, l.slider_h, value, 11 * s, 0xffaeb7c2);
    };
    const uint8_t rgb[3] = {d.colour.r, d.colour.g, d.colour.b};
    const wchar_t* names[3] = {L"Red", L"Green", L"Blue"};
    const uint32_t colours[3] = {0xffff5c5c, 0xff5cff7a, 0xff5c9dff};
    for (int i = 0; i < 3; ++i) {
        swprintf_s(buf, L"%d", d.has_colour ? int(rgb[i]) : 0);
        slider(l.y + l.slider_y + i * l.slider_h, names[i], d.has_colour ? rgb[i] / 255.f : 0.f, colours[i], d.has_colour ? buf : L"stock", !d.has_colour);
    }
    // pattern buttons
    for (int i = 0; i < kPatternCount; ++i) {
        float bx = l.x + l.pad + float(i % 4) * (l.button_w + 6 * s), by = l.y + l.pattern_y + float(i / 4) * (l.button_h + 6 * s);
        bool on = d.pattern == i;
        rect(ANY_GPU_ROUND_RECT, bx, by, l.button_w, l.button_h, on ? 0xffedc56c : 0xff3a4048, 4 * s);
        swprintf_s(buf, L"%hs", kPatternNames[i]);
        text(bx, by, l.button_w, l.button_h, buf, 11 * s, on ? 0xff191c20 : 0xffdfe4ea, ANY_GPU_CENTER | (on ? ANY_GPU_BOLD : 0));
    }
    swprintf_s(buf, L"%.2g Hz", kSpeedHz[d.speed < kSpeedCount ? d.speed : kDefaultSpeed]);
    slider(l.y + l.speed_y, L"Speed", float(d.speed) / (kSpeedCount - 1), 0xffedc56c, buf, d.pattern == kSteady);
    // footer: logic hint and override notice
    bool driven = g_panel.live_known && !(g_panel.live == effective(g_panel.saved, Inputs{}));
    text(l.x + l.pad, l.y + l.foot_y, l.w - l.pad * 2, 16 * s, driven ? L"Logic is driving this light right now (data inputs override)" : L"Data: colour_r/g/b, pattern, pattern_speed in · pattern_lit out",
         10 * s, driven ? 0xffedc56c : 0xff929fab);
    text(l.x + l.pad, l.y + l.foot_y + 16 * s, l.w - l.pad * 2, 16 * s, L"Saved in the light's name; the host needs AnyLights", 10 * s, 0xff929fab);
}

void changed() { g_panel.pending = true; g_panel.last_change = GetTickCount64(); }
void set_slider(int slider, int32_t mx) {
    float track_x = g_panel.x + 70 * g_panel.s, track_w = g_panel.w - 70 * g_panel.s - 60 * g_panel.s;
    float t = std::clamp((float(mx) - track_x) / std::max(1.f, track_w), 0.f, 1.f);
    Config& d = g_panel.draft;
    if (slider == kSliderSpeed) { d.speed = uint8_t(std::lround(t * (kSpeedCount - 1))); }
    else {
        if (!d.has_colour) { d.has_colour = true; d.colour = {255, 255, 255}; }
        uint8_t v = uint8_t(std::lround(t * 255));
        (slider == kSliderR ? d.colour.r : slider == kSliderG ? d.colour.g : d.colour.b) = v;
        if (d.colour == Colour{}) d.colour.r = 1;   // pure black would read as "stock"
    }
    changed();
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_panel_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_panel.dragging = -1; return 0; }
    if (!g_panel.visible) return 0;
    float s = g_panel.s, x = g_panel.x, y = g_panel.y, w = g_panel.w, pad = 14 * s;
    float swatch_y = y + 56 * s, swatch_size = 24 * s, slider_y = swatch_y + swatch_size * 2 + 14 * s, slider_h = 24 * s;
    float pattern_y = slider_y + slider_h * 3 + 18 * s, button_h = 26 * s, button_w = (w - pad * 2 - 3 * 6 * s) / 4;
    float speed_y = pattern_y + button_h * 2 + 6 * s + 12 * s, h = speed_y + slider_h + 12 * s + 40 * s - y;
    bool inside = e->x >= x && e->x < x + w && e->y >= y && e->y < y + h;
    if (e->kind == ANY_MOUSE_MOVE && g_panel.dragging >= 0) { set_slider(g_panel.dragging, e->x); return 1; }
    if (e->kind == ANY_MOUSE_UP && g_panel.dragging >= 0) { g_panel.dragging = -1; changed(); return 1; }
    if (e->kind != ANY_MOUSE_DOWN || e->button != 1) return (e->kind == ANY_MOUSE_UP || e->kind == ANY_MOUSE_WHEEL) && inside;
    if (!inside) return 0;
    float mx = float(e->x), my = float(e->y);
    Config& d = g_panel.draft;
    if (my >= swatch_y && my < swatch_y + swatch_size * 2 + 4 * s) {
        int col = std::clamp(int((mx - x - pad) / ((w - pad * 2) / 6)), 0, 5), row = my >= swatch_y + swatch_size + 4 * s ? 1 : 0;
        const Swatch& sw = kSwatches[row * 6 + col];
        d.has_colour = !sw.stock; if (!sw.stock) d.colour = sw.c;
        changed(); return 1;
    }
    if (my >= slider_y && my < slider_y + slider_h * 3) {
        g_panel.dragging = std::clamp(int((my - slider_y) / slider_h), 0, 2);
        set_slider(g_panel.dragging, e->x); return 1;
    }
    if (my >= pattern_y && my < pattern_y + button_h * 2 + 6 * s) {
        int col = std::clamp(int((mx - x - pad) / (button_w + 6 * s)), 0, 3), row = my >= pattern_y + button_h + 6 * s ? 1 : 0;
        d.pattern = uint8_t(row * 4 + col);
        changed(); return 1;
    }
    if (my >= speed_y && my < speed_y + slider_h) { g_panel.dragging = kSliderSpeed; set_slider(kSliderSpeed, e->x); return 1; }
    return 1;
}
}  // namespace
}  // namespace anylights

using namespace anylights;
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
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(1, "GPU drawing unavailable: the Properties panel is hidden; name tags and data channels still work");
    }
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        const char* ids[] = {"enabled", "data_channels", "panel"};
        const char* labels[] = {"Enable AnyLights colours and patterns", "Add data channels to lights (host)", "Show the panel with the Properties tool"};
        const char* descriptions[] = {"Colours and patterns show for every player who has AnyLights, when the host has it too.",
            "colour_r, colour_g, colour_b, pattern and pattern_speed inputs and a pattern_lit output for data links and microcontrollers. Takes effect for vehicles built or loaded afterwards.",
            nullptr};
        for (int i = 0; i < kSettingCount; ++i) {
            AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyLights"; d.setting_id = ids[i]; d.label = labels[i]; d.order = i; d.kind = ANY_SETTING_BOOL;
            d.default_number = 1; d.minimum = 0; d.maximum = 1; d.description = descriptions[i];
            g_tokens[i] = settings->register_setting(&d);
        }
    } else settings = nullptr;
    std::thread(install_hooks).detach();
}
