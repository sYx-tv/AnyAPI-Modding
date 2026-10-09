// EngineSound (prototype): extra engine sound presets and a deep Custom tuner (engine type, layers, cam
// lope, turbo/supercharger, lift-off crackle, throttle response) in the Properties tool's engine window. Co-op: the choice rides on the engine's existing replicated sound index, so the
// host stores it, saves it with the vehicle and replicates it; every player with the mod hears the same.
//
// EXPERIMENTAL. This mod hooks game functions directly through the experimental SDK (see
// enginesound_bindings.h). The hooks resolve only on Anymaker 0.1.24 / Steam build 25826614 and are not
// reviewed as an AnyAPI service yet. The first in-game runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_experimental.hpp"
#include "enginesound_bindings.h"
#include "enginesound_params.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace enginesound {
namespace {
constexpr const char* kModId = "enginesound";

// Field offsets from the reference layouts (metadata). Client objects are main-thread only.
namespace off {
constexpr ptrdiff_t engine_factor_speed = 464 + 8;   // replication.slave.property_f64_compressed_u8 _m_value
constexpr ptrdiff_t engine_factor_power = 488 + 8;   // replication.slave.property_f64_compressed_u8 _m_value
constexpr ptrdiff_t engine_sound_index = 512 + 8;    // replication.slave.property_s32 _m_value
constexpr ptrdiff_t engine_is_damage = 528 + 8;      // replication.slave.property_bool _m_value
constexpr ptrdiff_t voice_operate = 576, voice_power = 592, voice_knock = 608, voice_leak = 624;
constexpr ptrdiff_t client_peer_data = 40 + 104;     // client.m_peers (client_peer_container).m_data
// client_scene.peer.property_data.property_s32_range
constexpr ptrdiff_t row_id = 8, row_name = 16 + 8, row_value = 32 + 8, row_prev = 48, row_edit = 52;
constexpr ptrdiff_t row_min = 64 + 8, row_max = 80 + 8;
// Server side (server thread only)
constexpr ptrdiff_t server_engine_sound = 576;       // replication.master.property_s32
constexpr ptrdiff_t pd_elements = 72 + 24;           // property_data._m_properties._m_elements (vector<ref<property_base>>)
// server_scene.peer.property_data.property_replication_s32_range (168 bytes)
constexpr ptrdiff_t srow_name = 32 + 24, srow_value_ptr = 96, srow_min_modified = 104 + 16, srow_min = 104 + 24;
constexpr ptrdiff_t srow_max_modified = 136 + 16, srow_max = 136 + 24;
constexpr size_t srow_size = 168;
}  // namespace off
constexpr int32_t kSoundLabel = 324;             // e_localization_string.property_sound_effect (static)
constexpr int32_t kServerMax = INT32_MAX;        // marks a modded row to clients
constexpr int32_t kServerMin = INT32_MIN;        // accepts packed (negative) Custom tunes

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
std::atomic<bool> g_ready{false}, g_stop{false};

// ------------------------------------------------------------------ logging (capped, diagnostic build)
void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }
template <class T> void wr(uint8_t* p, ptrdiff_t o, T v) { std::memcpy(p + o, &v, sizeof v); }

// ------------------------------------------------------------------ hooks
using selected_t = void (*)(int32_t* ret, uint8_t* engine);
using engine_tick_t = void (*)(uint8_t* engine, void* client, void* vehicle, void* scene, const double* dt);
using vol_speed_t = void (*)(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos);
using play_t = void (*)(void* manager, const int32_t* effect, const int32_t* group, const void* pos, const void* vel);
using props_t = void (*)(uint8_t* engine, uint8_t* property_data);
using row_ui_t = void (*)(uint8_t* row, void* frontend_ui, uint8_t* client, const int32_t* a, const int32_t* b);
using push_s32_t = void (*)(uint8_t* peer_data, const int32_t* id, const int32_t* value);

anymaker::cell_hook h_selected, h_tick, h_vol_speed, h_play, h_props, h_row;
void** g_push_cell;

thread_local uint8_t* t_engine;   // engine whose client tick is running on this thread
thread_local double t_dt;

// Per-engine sound state (main thread). Keyed by pointer; stale entries are simply reused.
struct EngineState {
    uint8_t* engine; uint64_t used;
    double speed = NAN, spool = 0, time = 0, load_peak = 0, power_peak = 0, power_volume = 0;
    double crackle_until = -1, blowoff_ready = 0, prev_load = 0;
    double pos[3]{}; bool has_pos = false; int32_t group = 4; uint32_t rng = 0x9e3779b9u;
};
EngineState g_states[64];
uint64_t g_state_clock;
EngineState& state_for(uint8_t* e) {
    EngineState* oldest = &g_states[0];
    for (auto& s : g_states) {
        if (s.engine == e) { s.used = ++g_state_clock; return s; }
        if (s.used < oldest->used) oldest = &s;
    }
    *oldest = EngineState{};
    oldest->engine = e; oldest->used = ++g_state_clock; oldest->rng ^= uint32_t(uintptr_t(e) >> 4);
    return *oldest;
}
double random01(EngineState& st) { st.rng ^= st.rng << 13; st.rng ^= st.rng >> 17; st.rng ^= st.rng << 5; return (st.rng & 0xffffff) / double(0x1000000); }

std::atomic<void*> g_audio_manager{nullptr};
std::atomic<int> g_oneshot_logs{8};
void play_oneshot(EngineState& st, int32_t effect) {
    void* manager = g_audio_manager.load();
    if (!manager || !st.has_pos || !h_play.original) return;
    double vel[3]{};
    int32_t group = st.group;
    reinterpret_cast<play_t>(h_play.original)(manager, &effect, &group, st.pos, vel);
    if (g_oneshot_logs.fetch_sub(1) > 0) log(0, "one-shot effect %d group %d", effect, group);
}

// Diagnostics: what each sound index maps to and how each voice is driven.
// Aggregated per voice and sound index; only audible calls count, so stopped engines don't drown the log.
struct VoiceDiag { uint32_t calls; double vmin, vmax, smin, smax, out_vmin, out_vmax, out_smin, out_smax; int32_t effect, group; };
VoiceDiag g_voice_diag[4][17];   // index 0..15, 16 = packed Custom
std::atomic<int> g_voice_budget{120};
bool g_index_logged[16];

void diag_voice(int slot, int32_t index, double v, double s, double out_v, double out_s, int32_t effect, int32_t group) {
    if (!(v > 0.001)) return;
    int bucket = is_packed(index) ? 16 : (index >= 0 && index < 16 ? index : -1);
    if (bucket < 0) return;
    auto& d = g_voice_diag[slot][bucket];
    if (!d.calls) d = {0, v, v, s, s, out_v, out_v, out_s, out_s, effect, group};
    ++d.calls; d.vmin = std::min(d.vmin, v); d.vmax = std::max(d.vmax, v); d.smin = std::min(d.smin, s); d.smax = std::max(d.smax, s);
    d.out_vmin = std::min(d.out_vmin, out_v); d.out_vmax = std::max(d.out_vmax, out_v); d.out_smin = std::min(d.out_smin, out_s); d.out_smax = std::max(d.out_smax, out_s);
    d.effect = effect; d.group = group;
    if ((d.calls == 1 || d.calls == 300 || d.calls % 3000 == 0) && g_voice_budget.fetch_sub(1) > 0) {
        static const char* names[4] = {"operate", "power", "knock", "leak"};
        log(0, "voice %s idx=%d effect=%d group=%d audible calls=%u game volume %.3f..%.3f speed %.3f..%.3f -> sent volume %.3f..%.3f speed %.3f..%.3f",
            names[slot], index, effect, group, d.calls, d.vmin, d.vmax, d.smin, d.smax, d.out_vmin, d.out_vmax, d.out_smin, d.out_smax);
    }
}

void hk_selected(int32_t* ret, uint8_t* engine) {
    reinterpret_cast<selected_t>(h_selected.original)(ret, engine);
    if (!ret || !engine) return;
    int32_t index = rd<int32_t>(engine, off::engine_sound_index);
    if (index >= 0 && index < 16 && !g_index_logged[index]) { g_index_logged[index] = true; log(0, "sound index %d -> vanilla effect %d", index, *ret); }
    static bool packed_logged; if (is_packed(index) && !packed_logged) { packed_logged = true; log(0, "sound index packed 0x%08x -> vanilla effect %d", unsigned(index), *ret); }
    Choice c = decode(index);
    if (c.mode != Mode::Vanilla) *ret = main_effect(c.params);
}

void hk_tick(uint8_t* engine, void* client, void* vehicle, void* scene, const double* dt) {
    uint8_t* prev = t_engine; double prev_dt = t_dt;
    t_engine = engine; t_dt = dt ? *dt : 0;
    reinterpret_cast<engine_tick_t>(h_tick.original)(engine, client, vehicle, scene, dt);
    t_engine = prev; t_dt = prev_dt;
}

void hk_vol_speed(uint8_t* single, const double* volume_in, const double* speed_in, const int32_t* effect, const int32_t* group, const void* pos) {
    auto original = reinterpret_cast<vol_speed_t>(h_vol_speed.original);
    uint8_t* e = t_engine;
    if (!e || !single || !volume_in || !speed_in) return original(single, volume_in, speed_in, effect, group, pos);
    ptrdiff_t o = single - e;
    int slot = o == off::voice_operate ? 0 : o == off::voice_power ? 1 : o == off::voice_knock ? 2 : o == off::voice_leak ? 3 : -1;
    if (slot < 0) return original(single, volume_in, speed_in, effect, group, pos);
    int32_t index = rd<int32_t>(e, off::engine_sound_index);
    Choice c = decode(index);
    bool damaged = rd<uint8_t>(e, off::engine_is_damage) != 0;
    if (c.mode == Mode::Vanilla || slot == 3 || (slot == 2 && (damaged || induction(c.params) == 0))) {
        diag_voice(slot, index, *volume_in, *speed_in, *volume_in, *speed_in, effect ? *effect : -1, group ? *group : -1);
        return original(single, volume_in, speed_in, effect, group, pos);
    }
    const Params& p = c.params;
    double rpm = clamp01(rd<double>(e, off::engine_factor_speed)), load = clamp01(rd<double>(e, off::engine_factor_power));
    auto& st = state_for(e);
    double volume_out = *volume_in, speed_out = *speed_in;
    int32_t effect_out = effect ? *effect : 0;
    if (slot == 1) {
        // Main voice. Also advances per-engine time and fires one-shots, once per engine tick.
        double dt = std::isfinite(t_dt) ? std::clamp(t_dt, 0.0, 0.25) : 0.0;
        st.time += dt;
        if (pos) { std::memcpy(st.pos, pos, sizeof st.pos); st.has_pos = true; }
        if (group) st.group = *group;
        st.power_volume = *volume_in; st.power_peak = std::max(st.power_peak * (1 - 0.02 * dt), *volume_in);
        int kind = induction(p);
        st.spool = smooth(st.spool, kind ? spool_target(rpm, load) : 0, spool_seconds(kind), dt);
        Lope l = lope(p, rpm, st.time);
        st.speed = smooth(st.speed, *speed_in * pitch_multiplier(p, rpm, load) * l.pitch, smoothing_seconds(p), dt);
        speed_out = clamp_speed(st.speed);
        volume_out = clamp_volume(*volume_in * volume(p) * load_gain(p, load) * l.volume);
        effect_out = main_effect(p);
        bool running = *volume_in > 0.001;
        // Lift-off: load falls away after being high. Starts a crackle window and (turbo) a blow-off.
        st.load_peak = std::max(load, st.load_peak - 1.5 * dt);
        if (running && load < 0.15 && st.load_peak > 0.5 && rpm > 0.3) {
            if (crackle_rate(p) > 0 && st.crackle_until < st.time) st.crackle_until = st.time + 1.5;
            if ((kind == 1 || kind == 3) && st.spool > 0.45 && st.time >= st.blowoff_ready) { play_oneshot(st, kFluidGasVent); st.blowoff_ready = st.time + 1.2; }
            st.load_peak = 0;
        }
        if (running && st.time < st.crackle_until && random01(st) < crackle_rate(p) * rpm * dt) play_oneshot(st, kEnginePop);
        st.prev_load = load;
    } else if (slot == 0) {
        Lope l = lope(p, rpm, st.time);
        speed_out = clamp_speed(*speed_in * body_pitch_multiplier(p, rpm));
        volume_out = clamp_volume(*volume_in * body_layer(p) * l.volume * std::sqrt(load_gain(p, load)));
        effect_out = low_effect(p);
    } else {
        // Spare knock voice carries the induction layer while the engine is healthy.
        Layer layer;
        induction_layer(p, rpm, load, st.spool, layer);
        double on = st.power_peak > 1e-6 ? clamp01(st.power_volume / st.power_peak) : 0.0;
        effect_out = layer.effect;
        speed_out = clamp_speed(layer.speed);
        volume_out = clamp_volume(layer.volume * on);
    }
    diag_voice(slot, index, *volume_in, *speed_in, volume_out, speed_out, effect_out, group ? *group : -1);
    original(single, &volume_out, &speed_out, &effect_out, group, pos);
}

void hk_play(void* manager, const int32_t* effect, const int32_t* group, const void* pos, const void* vel) {
    if (manager && !g_audio_manager.load()) g_audio_manager.store(manager);
    reinterpret_cast<play_t>(h_play.original)(manager, effect, group, pos, vel);
}

// Host: widen the engine's sound row so preset, Custom and packed values are accepted and saved.
std::atomic<int> g_range_logs{6};
void hk_props(uint8_t* engine, uint8_t* pd) {
    reinterpret_cast<props_t>(h_props.original)(engine, pd);
    if (!engine || !pd || !anymaker::readable(pd + off::pd_elements, sizeof(anymaker::gc_vector_raw))) return;
    auto* vec = reinterpret_cast<const anymaker::gc_vector_raw*>(pd + off::pd_elements);
    for (int32_t i = 0; i < vec->count && i < 64; ++i) {
        auto* ref = vec->at<anymaker::gc_ref_raw<uint8_t>>(i);
        uint8_t* row = ref ? ref->ptr : nullptr;
        if (!row || !anymaker::readable(row, off::srow_size)) continue;
        if (rd<uint8_t*>(row, off::srow_value_ptr) != engine + off::server_engine_sound) continue;
        int32_t lo = rd<int32_t>(row, off::srow_min), hi = rd<int32_t>(row, off::srow_max), name = rd<int32_t>(row, off::srow_name);
        if (g_range_logs.fetch_sub(1) > 0) log(0, "host engine sound row: label=%d range %d..%d value=%d", name, lo, hi, rd<int32_t>(engine, off::server_engine_sound + 24));
        if (lo != kVanillaMin || hi != kVanillaMax || name != kSoundLabel) {
            log(1, "engine sound row differs from the expected %d..%d (label %d); leaving it vanilla", kVanillaMin, kVanillaMax, kSoundLabel);
            return;
        }
        wr<int32_t>(row, off::srow_min, kServerMin);
        wr<uint8_t>(row, off::srow_min_modified, 1);
        wr<int32_t>(row, off::srow_max, kServerMax);
        wr<uint8_t>(row, off::srow_max_modified, 1);
        return;
    }
}

// ------------------------------------------------------------------ panel state (shared by threads)
std::mutex g_mutex;
struct Panel {
    uint64_t seen = 0;            // GetTickCount64 when the modded row last drew
    int32_t row_id = 0, value = 0;
    Params draft = neutral();
    int dragging = -1;
    uint64_t last_commit = 0;
    bool has_pending = false; int32_t pending = 0, pending_row = 0;
    float x = 0, y = 0, w = 0, row_h = 0, scale = 1, track_x = 0, track_w = 0; // last layout, for input
    float button_x = 0, button_y = 0, button_w = 0, button_h = 0;              // "Customize" (preset/stock only)
    bool visible = false;
} g_panel;

// Slider values a sound index starts from: a preset's own values, a stock sound as its engine type, or the tune.
Params starting_params(int32_t value) {
    Choice c = decode(value);
    if (c.mode == Mode::Vanilla && value >= kVanillaMin && value <= kVanillaMax) c.params.v[kType] = uint8_t(value - kVanillaMin);
    return c.params;
}
void queue_send(int32_t value) {
    g_panel.pending = value; g_panel.pending_row = g_panel.row_id; g_panel.has_pending = true; g_panel.last_commit = GetTickCount64();
}

void hk_row(uint8_t* row, void* fui, uint8_t* client, const int32_t* a, const int32_t* b) {
    auto original = reinterpret_cast<row_ui_t>(h_row.original);
    if (!row || rd<int32_t>(row, off::row_name) != kSoundLabel || rd<int32_t>(row, off::row_max) != kServerMax)
        return original(row, fui, client, a, b);
    int32_t id = rd<int32_t>(row, off::row_id), value = rd<int32_t>(row, off::row_value);
    bool send = false; int32_t to_send = 0;
    {
        std::lock_guard lock(g_mutex);
        uint64_t now = GetTickCount64();
        if (g_panel.row_id != id || now - g_panel.seen > 1000) { g_panel.dragging = -1; g_panel.has_pending = false; }
        g_panel.seen = now; g_panel.row_id = id;
        if (g_panel.value != value && g_panel.dragging < 0 && now - g_panel.last_commit > 1000) g_panel.draft = starting_params(value);
        g_panel.value = value;
        if (g_panel.has_pending && g_panel.pending_row == id) { send = true; to_send = g_panel.pending; g_panel.has_pending = false; }
    }
    if (send && client && g_push_cell && *g_push_cell)
        reinterpret_cast<push_s32_t>(*g_push_cell)(client + off::client_peer_data, &id, &to_send);
    // Show the native slider as 0..Custom; a packed value displays at the Custom position.
    int32_t shown = is_packed(value) ? kCustomIndex : value;
    const ptrdiff_t fields[] = {off::row_value, off::row_prev, off::row_edit};
    bool swapped[3]{};
    for (int i = 0; i < 3; ++i) if (is_packed(value) && rd<int32_t>(row, fields[i]) == value) { wr<int32_t>(row, fields[i], shown); swapped[i] = true; }
    int32_t real_min = rd<int32_t>(row, off::row_min);
    wr<int32_t>(row, off::row_min, kVanillaMin);
    wr<int32_t>(row, off::row_max, kCustomIndex);
    original(row, fui, client, a, b);
    if (rd<int32_t>(row, off::row_max) == kCustomIndex) wr<int32_t>(row, off::row_max, kServerMax);
    if (rd<int32_t>(row, off::row_min) == kVanillaMin) wr<int32_t>(row, off::row_min, real_min);
    for (int i = 0; i < 3; ++i) if (swapped[i] && rd<int32_t>(row, fields[i]) == shown) wr<int32_t>(row, fields[i], value);
}

// ------------------------------------------------------------------ install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; EngineSound stays off"); return; }
        void** selected = hook_cell(bind::client_engine_get_selected_sound_effect);
        void** tick = hook_cell(bind::client_engine_tick);
        void** vol = hook_cell(bind::effect_update_play_volume_speed);
        void** play = hook_cell(bind::audio_manager_play);
        void** props = hook_cell(bind::server_engine_get_properties);
        void** row = hook_cell(bind::client_property_s32_range_update_ui);
        void** push = hook_cell(bind::client_push_set_property_s32);
        if (!(selected && tick && vol && play && props && row && push)) {
            if (attempt == 179) log(2, "could not locate game functions (selected=%d tick=%d volume=%d play=%d props=%d row=%d push=%d)",
                !!selected, !!tick, !!vol, !!play, !!props, !!row, !!push);
            continue;
        }
        g_push_cell = push;
        if (void* manager = global(anymaker::sym::g_audio_manager)) g_audio_manager.store(manager);
        else log(1, "g_audio_manager not resolved yet; crackle and blow-off start after the game plays its first engine pop");
        bool ok = h_selected.install(selected, (void*)&hk_selected) && h_tick.install(tick, (void*)&hk_tick) &&
                  h_vol_speed.install(vol, (void*)&hk_vol_speed) && h_play.install(play, (void*)&hk_play) &&
                  h_props.install(props, (void*)&hk_props) && h_row.install(row, (void*)&hk_row);
        if (!ok) { log(2, "a hook cell was already taken or not writable; EngineSound is off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: presets %d..%d, Custom %d, audio manager %s (diagnostic logging on)", attempt, kFirstPreset, kCustomIndex - 1, kCustomIndex, g_audio_manager.load() ? "found" : "pending");
        return;
    }
}
void remove_hooks() {
    h_row.remove((void*)&hk_row); h_props.remove((void*)&hk_props); h_play.remove((void*)&hk_play);
    h_vol_speed.remove((void*)&hk_vol_speed); h_tick.remove((void*)&hk_tick); h_selected.remove((void*)&hk_selected);
}

// ------------------------------------------------------------------ panel drawing and input
const wchar_t* mode_name(const Choice& c, wchar_t* buf, size_t n, int32_t value) {
    if (c.mode == Mode::Preset) swprintf(buf, n, L"Preset: %hs", kPresets[c.preset].name);
    else if (c.mode == Mode::Custom) swprintf(buf, n, L"Custom: %hs", engine_type(c.params).name);
    else swprintf(buf, n, L"Stock sound %d", int(value - kVanillaMin + 1));
    return buf;
}
void value_text(int s, const Params& p, wchar_t* buf, size_t n) {
    switch (s) {
    case kType: swprintf(buf, n, L"%hs", engine_type(p).name); break;
    case kPitch: swprintf(buf, n, L"%.2fx", idle_pitch(p)); break;
    case kRange: swprintf(buf, n, L"%.1fx", rev_range(p)); break;
    case kCurve: { static const wchar_t* c[4] = {L"Early", L"Linear", L"Late", L"Very late"}; swprintf(buf, n, L"%ls", c[p.v[kCurve] & 3]); break; }
    case kVolume: swprintf(buf, n, L"%d%%", int(std::lround(enginesound::volume(p) * 100))); break;
    case kLow: swprintf(buf, n, L"%d%%", int(std::lround(body_layer(p) * 100))); break;
    case kLope: swprintf(buf, n, p.v[kLope] ? L"%d / 7" : L"Off", int(p.v[kLope])); break;
    case kInduction: swprintf(buf, n, L"%hs", kInductionNames[induction(p)]); break;
    case kBoost: swprintf(buf, n, L"%hs", induction(p) ? kLevelNames[p.v[kBoost] & 3] : "(no induction)"); break;
    case kCrackle: swprintf(buf, n, L"%hs", kLevelNames[p.v[kCrackle] & 3]); break;
    case kLoad: swprintf(buf, n, L"%hs", kLevelNames[p.v[kLoad] & 3]); break;
    default: swprintf(buf, n, L"%ls", p.v[kSmoothing] ? L"On" : L"Off"); break;
    }
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_mutex);
    g_panel.visible = g_ready && frame->focused && GetTickCount64() - g_panel.seen < 300;
    if (!g_panel.visible) { g_panel.dragging = -1; return; }
    static bool shown_logged;
    if (!shown_logged) { shown_logged = true; log(0, "panel shown: row %d value %d, frame %ux%u", g_panel.row_id, g_panel.value, frame->width, frame->height); }
    Choice c = decode(g_panel.value);
    bool custom = c.mode == Mode::Custom;
    float s = std::clamp(float(frame->height) / 1080.f, .7f, 1.8f);
    float w = 360 * s, row_h = 26 * s, head = 56 * s, h = head + (custom ? row_h * kSliderCount + 12 * s : 44 * s);
    float x = std::max(0.f, float(frame->width) - w - 24 * s), y = std::max(8 * s, float(frame->height) * .5f - h * .5f);
    g_panel.x = x; g_panel.y = y + head; g_panel.w = w; g_panel.row_h = row_h; g_panel.scale = s;
    g_panel.track_x = x + 150 * s; g_panel.track_w = w - 165 * s;
    g_panel.button_x = x + 14 * s; g_panel.button_y = y + head; g_panel.button_w = w - 28 * s; g_panel.button_h = custom ? 0 : 30 * s;
    auto rect = [&](uint32_t kind, float rx, float ry, float rw, float rh, uint32_t color, float radius = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = kind; cmd.rect[0] = rx; cmd.rect[1] = ry; cmd.rect[2] = rw; cmd.rect[3] = rh; cmd.color = color; cmd.radius = radius; gpu->emit(&cmd); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = ANY_GPU_TEXT; cmd.rect[0] = tx; cmd.rect[1] = ty; cmd.rect[2] = tw; cmd.rect[3] = th; cmd.text = t;
        cmd.text_length = uint32_t(wcslen(t)); cmd.font_size = size; cmd.color = color; cmd.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&cmd); };
    rect(ANY_GPU_ROUND_RECT, x, y, w, h, 0xf0191c20, 6 * s);
    text(x + 14 * s, y + 8 * s, w - 28 * s, 18 * s, L"ENGINE SOUND", 11 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    wchar_t buf[96];
    text(x + 14 * s, y + 26 * s, w - 28 * s, 24 * s, mode_name(c, buf, 96, g_panel.value), 17 * s, 0xfff5f7fa);
    if (!custom) {
        // Copies this preset or stock sound into Custom so it can be tuned from there.
        rect(ANY_GPU_ROUND_RECT, g_panel.button_x, g_panel.button_y, g_panel.button_w, g_panel.button_h, 0xff3a4048, 4 * s);
        text(g_panel.button_x, g_panel.button_y, g_panel.button_w, g_panel.button_h, L"Customize this sound", 13 * s, 0xffedc56c, ANY_GPU_CENTER | ANY_GPU_BOLD);
        return;
    }
    const Params& p = g_panel.draft;
    for (int i = 0; i < kSliderCount; ++i) {
        float ry = y + head + i * row_h;
        swprintf(buf, 96, L"%hs", kSliders[i].label);
        text(x + 14 * s, ry, 130 * s, row_h, buf, 12 * s, 0xffdfe4ea);
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .6f - 2 * s, g_panel.track_w, 4 * s, 0xff3a4048, 2 * s);
        float t = float(p.v[i]) / float(max_step(i));
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .6f - 2 * s, g_panel.track_w * t, 4 * s, 0xffedc56c, 2 * s);
        rect(ANY_GPU_ELLIPSE, g_panel.track_x + g_panel.track_w * t - 6 * s, ry + row_h * .6f - 6 * s, 12 * s, 12 * s, g_panel.dragging == i ? 0xffffffff : 0xffedc56c);
        value_text(i, p, buf, 96);
        text(g_panel.track_x, ry - 1 * s, g_panel.track_w, 12 * s, buf, 10 * s, 0xffaeb7c2, ANY_GPU_CENTER);
    }
}

void set_from_mouse(int slider, int32_t mx) {
    float t = std::clamp((float(mx) - g_panel.track_x) / std::max(1.f, g_panel.track_w), 0.f, 1.f);
    int step = int(std::lround(t * max_step(slider)));
    if (g_panel.draft.v[slider] == step) return;
    g_panel.draft.v[slider] = uint8_t(step);
    if (GetTickCount64() - g_panel.last_commit >= 150) queue_send(pack(g_panel.draft));
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_panel.dragging = -1; return 0; }
    if (!g_panel.visible) return 0;
    static int click_logs = 6;
    if (e->kind == ANY_MOUSE_DOWN && click_logs > 0) { --click_logs; log(0, "panel click button=%u at %d,%d; panel x %.0f..%.0f y %.0f",
        e->button, e->x, e->y, g_panel.x, g_panel.x + g_panel.w, g_panel.y); }
    if (decode(g_panel.value).mode != Mode::Custom) {
        bool on_button = e->x >= g_panel.button_x && e->x < g_panel.button_x + g_panel.button_w && e->y >= g_panel.button_y && e->y < g_panel.button_y + g_panel.button_h;
        if (e->kind == ANY_MOUSE_DOWN && e->button == 1 && on_button) {
            g_panel.draft = starting_params(g_panel.value);
            queue_send(pack(g_panel.draft));
            log(0, "Customize: value %d -> tune %d", g_panel.value, g_panel.pending);
            return 1;
        }
        return (e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP) && on_button;
    }
    bool inside = e->x >= g_panel.x && e->x < g_panel.x + g_panel.w && e->y >= g_panel.y && e->y < g_panel.y + g_panel.row_h * kSliderCount;
    if (e->kind == ANY_MOUSE_DOWN && e->button == 1 && inside) {
        g_panel.dragging = std::clamp(int((e->y - g_panel.y) / g_panel.row_h), 0, kSliderCount - 1);
        set_from_mouse(g_panel.dragging, e->x);
        return 1;
    }
    if (e->kind == ANY_MOUSE_MOVE && g_panel.dragging >= 0) { set_from_mouse(g_panel.dragging, e->x); return 1; }
    if (e->kind == ANY_MOUSE_UP && g_panel.dragging >= 0) {
        g_panel.dragging = -1;
        queue_send(pack(g_panel.draft));
        return 1;
    }
    return (e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP || e->kind == ANY_MOUSE_WHEEL) && inside;
}
}  // namespace
}  // namespace enginesound

using namespace enginesound;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(1, "GPU drawing unavailable: presets work, the Custom panel is hidden");
    }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
