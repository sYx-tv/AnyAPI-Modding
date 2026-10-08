// EngineSound (prototype): extra engine sound presets and a Custom slider panel for the Properties
// tool's engine window. Co-op: the choice rides on the engine's existing replicated sound index, so the
// host stores it, saves it with the vehicle and replicates it; every player with the mod hears the same.
//
// EXPERIMENTAL. This mod hooks game functions directly through the experimental SDK (see
// enginesound_bindings.h). The hooks resolve only on Anymaker 0.1.23 / Steam build 25755694 and are not
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
#include <mutex>
#include <thread>

namespace enginesound {
namespace {
constexpr const char* kModId = "enginesound";

// Field offsets from the reference layouts (metadata). Client objects are main-thread only.
namespace off {
constexpr ptrdiff_t engine_factor_speed = 464 + 8;   // replication.slave.property_f64_compressed_u8 _m_value
constexpr ptrdiff_t engine_sound_index = 512 + 8;    // replication.slave.property_s32 _m_value
constexpr ptrdiff_t voice_operate = 576, voice_power = 592, voice_knock = 608, voice_leak = 624;
constexpr ptrdiff_t client_peer_data = 40 + 104;     // client.m_peers (client_peer_container).m_data
// client_scene.peer.property_data.property_s32_range
constexpr ptrdiff_t row_id = 8, row_name = 16 + 8, row_value = 32 + 8, row_prev = 48, row_edit = 52;
constexpr ptrdiff_t row_max = 80 + 8;
// Server side (server thread only)
constexpr ptrdiff_t server_engine_sound = 576;       // replication.master.property_s32
constexpr ptrdiff_t pd_elements = 72 + 24;           // property_data._m_properties._m_elements (vector<ref<property_base>>)
// server_scene.peer.property_data.property_replication_s32_range (168 bytes)
constexpr ptrdiff_t srow_name = 32 + 24, srow_value_ptr = 96, srow_min = 104 + 24;
constexpr ptrdiff_t srow_max_modified = 136 + 16, srow_max = 136 + 24;
constexpr size_t srow_size = 168;
}  // namespace off
constexpr int32_t kSoundLabel = 324;             // e_localization_string.property_sound_effect (static)
constexpr int32_t kServerMax = 0x7fffffff;       // marks a modded row to clients and accepts packed values

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
std::atomic<bool> g_ready{false}, g_stop{false};

// ------------------------------------------------------------------ logging (capped, diagnostic build)
std::atomic<int> g_log_budget{400};
void log(int level, const char* fmt, ...) {
    if (!host.log || (level == 0 && g_log_budget.fetch_sub(1) <= 0)) return;
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

// Per-engine smoothing state (main thread). Keyed by pointer; stale entries are simply reused.
struct EngineState { uint8_t* engine; double speed; uint64_t used; };
EngineState g_states[64];
uint64_t g_state_clock;
EngineState& state_for(uint8_t* e) {
    EngineState* oldest = &g_states[0];
    for (auto& s : g_states) {
        if (s.engine == e) { s.used = ++g_state_clock; return s; }
        if (s.used < oldest->used) oldest = &s;
    }
    *oldest = {e, NAN, ++g_state_clock};
    return *oldest;
}

// Diagnostics: what each sound index maps to and how each voice is driven.
struct VoiceDiag { int32_t index = INT32_MIN; uint32_t calls; double vmin, vmax, smin, smax; int32_t effect, group; };
VoiceDiag g_voice_diag[4];
bool g_index_logged[16];

void diag_voice(int slot, int32_t index, double v, double s, int32_t effect, int32_t group) {
    auto& d = g_voice_diag[slot];
    static const char* names[4] = {"operate", "power", "knock", "leak"};
    if (d.index != index) {
        if (d.index != INT32_MIN && d.calls) log(0, "voice %s idx=%d effect=%d group=%d calls=%u volume %.3f..%.3f speed %.3f..%.3f",
            names[slot], d.index, d.effect, d.group, d.calls, d.vmin, d.vmax, d.smin, d.smax);
        d = {index, 0, v, v, s, s, effect, group};
    }
    ++d.calls; d.vmin = std::min(d.vmin, v); d.vmax = std::max(d.vmax, v); d.smin = std::min(d.smin, s); d.smax = std::max(d.smax, s);
    d.effect = effect; d.group = group;
    if (d.calls == 1 || d.calls == 1200) log(0, "voice %s idx=%d effect=%d group=%d volume %.3f..%.3f speed %.3f..%.3f (calls %u)",
        names[slot], index, effect, group, d.vmin, d.vmax, d.smin, d.smax, d.calls);
}

void hk_selected(int32_t* ret, uint8_t* engine) {
    reinterpret_cast<selected_t>(h_selected.original)(ret, engine);
    if (!ret || !engine) return;
    int32_t index = rd<int32_t>(engine, off::engine_sound_index);
    if (index >= 0 && index < 16 && !g_index_logged[index]) { g_index_logged[index] = true; log(0, "sound index %d -> vanilla effect %d", index, *ret); }
    Choice c = decode(index);
    if (c.mode != Mode::Vanilla) *ret = base_effect(c.params);
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
    diag_voice(slot, index, *volume_in, *speed_in, effect ? *effect : -1, group ? *group : -1);
    Choice c = decode(index);
    if (c.mode == Mode::Vanilla || slot > 1) return original(single, volume_in, speed_in, effect, group, pos);
    double volume_out = *volume_in, speed_out = *speed_in;
    if (slot == 1) {
        double rpm = rd<double>(e, off::engine_factor_speed);
        auto& st = state_for(e);
        st.speed = smooth(st.speed, *speed_in * pitch_multiplier(c.params, rpm), smoothing_seconds(c.params), t_dt);
        speed_out = clamp_speed(st.speed);
        volume_out = clamp_volume(*volume_in * enginesound::volume(c.params));
    } else {
        speed_out = clamp_speed(*speed_in * idle_pitch(c.params));
        volume_out = clamp_volume(*volume_in * idle_layer(c.params));
    }
    original(single, &volume_out, &speed_out, effect, group, pos);
}

void hk_play(void* manager, const int32_t* effect, const int32_t* group, const void* pos, const void* vel) {
    uint8_t* e = t_engine;
    if (e && effect && *effect == kEnginePop) {
        Choice c = decode(rd<int32_t>(e, off::engine_sound_index));
        if (c.mode != Mode::Vanilla && !pops(c.params)) return;
    }
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
    bool visible = false;
} g_panel;

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
        if (g_panel.value != value && g_panel.dragging < 0 && now - g_panel.last_commit > 1000) g_panel.draft = decode(value).params;
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
    wr<int32_t>(row, off::row_max, kCustomIndex);
    original(row, fui, client, a, b);
    if (rd<int32_t>(row, off::row_max) == kCustomIndex) wr<int32_t>(row, off::row_max, kServerMax);
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
        bool ok = h_selected.install(selected, (void*)&hk_selected) && h_tick.install(tick, (void*)&hk_tick) &&
                  h_vol_speed.install(vol, (void*)&hk_vol_speed) && h_play.install(play, (void*)&hk_play) &&
                  h_props.install(props, (void*)&hk_props) && h_row.install(row, (void*)&hk_row);
        if (!ok) { log(2, "a hook cell was already taken or not writable; EngineSound is off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: presets %d..%d, Custom %d (diagnostic logging on)", attempt, kFirstPreset, kCustomIndex - 1, kCustomIndex);
        return;
    }
}
void remove_hooks() {
    h_row.remove((void*)&hk_row); h_props.remove((void*)&hk_props); h_play.remove((void*)&hk_play);
    h_vol_speed.remove((void*)&hk_vol_speed); h_tick.remove((void*)&hk_tick); h_selected.remove((void*)&hk_selected);
}

// ------------------------------------------------------------------ panel drawing and input
const wchar_t* utf16_label(int slider) {
    static const wchar_t* labels[kSliderCount] = {L"Base sound", L"Idle pitch", L"Rev range", L"Rev curve", L"Volume", L"Idle layer", L"Smoothing", L"Backfire pops"};
    return labels[slider];
}
const wchar_t* mode_name(const Choice& c, wchar_t* buf, size_t n, int32_t value) {
    if (c.mode == Mode::Preset) swprintf(buf, n, L"Preset: %hs", kPresets[c.preset].name);
    else if (c.mode == Mode::Custom) swprintf(buf, n, L"Custom");
    else swprintf(buf, n, L"Stock sound %d", int(value - kVanillaMin + 1));
    return buf;
}
void value_text(int s, const Params& p, wchar_t* buf, size_t n) {
    switch (s) {
    case kBase: swprintf(buf, n, L"%hs", kBaseNames[p.v[kBase] & 7]); break;
    case kPitch: swprintf(buf, n, L"%.2fx", idle_pitch(p)); break;
    case kRange: swprintf(buf, n, L"%.1fx", rev_range(p)); break;
    case kCurve: swprintf(buf, n, L"%.2f", rev_curve(p)); break;
    case kVolume: swprintf(buf, n, L"%d%%", int(std::lround(enginesound::volume(p) * 100))); break;
    case kIdleLayer: swprintf(buf, n, L"%d%%", int(std::lround(idle_layer(p) * 100))); break;
    case kSmoothing: swprintf(buf, n, L"%.2f s", smoothing_seconds(p)); break;
    default: swprintf(buf, n, L"%ls", pops(p) ? L"On" : L"Off"); break;
    }
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_mutex);
    g_panel.visible = g_ready && frame->focused && GetTickCount64() - g_panel.seen < 300;
    if (!g_panel.visible) { g_panel.dragging = -1; return; }
    Choice c = decode(g_panel.value);
    bool custom = c.mode == Mode::Custom;
    float s = std::clamp(float(frame->height) / 1080.f, .7f, 1.8f);
    float w = 330 * s, row_h = 30 * s, head = 56 * s, h = head + (custom ? row_h * kSliderCount + 12 * s : 10 * s);
    float x = std::max(0.f, float(frame->width) - w - 24 * s), y = float(frame->height) * .22f;
    g_panel.x = x; g_panel.y = y + head; g_panel.w = w; g_panel.row_h = row_h; g_panel.scale = s;
    g_panel.track_x = x + 150 * s; g_panel.track_w = w - 165 * s;
    auto rect = [&](uint32_t kind, float rx, float ry, float rw, float rh, uint32_t color, float radius = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = kind; cmd.rect[0] = rx; cmd.rect[1] = ry; cmd.rect[2] = rw; cmd.rect[3] = rh; cmd.color = color; cmd.radius = radius; gpu->emit(&cmd); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = ANY_GPU_TEXT; cmd.rect[0] = tx; cmd.rect[1] = ty; cmd.rect[2] = tw; cmd.rect[3] = th; cmd.text = t;
        cmd.text_length = uint32_t(wcslen(t)); cmd.font_size = size; cmd.color = color; cmd.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&cmd); };
    rect(ANY_GPU_ROUND_RECT, x, y, w, h, 0xf0191c20, 6 * s);
    text(x + 14 * s, y + 8 * s, w - 28 * s, 18 * s, L"ENGINE SOUND", 11 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    wchar_t buf[96];
    text(x + 14 * s, y + 26 * s, w - 28 * s, 24 * s, mode_name(c, buf, 96, g_panel.value), 17 * s, 0xfff5f7fa);
    if (!custom) return;
    const Params& p = g_panel.draft;
    for (int i = 0; i < kSliderCount; ++i) {
        float ry = y + head + i * row_h;
        text(x + 14 * s, ry, 130 * s, row_h, utf16_label(i), 12 * s, 0xffdfe4ea);
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .5f - 2 * s, g_panel.track_w, 4 * s, 0xff3a4048, 2 * s);
        float t = float(p.v[i]) / float(max_step(i));
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .5f - 2 * s, g_panel.track_w * t, 4 * s, 0xffedc56c, 2 * s);
        rect(ANY_GPU_ELLIPSE, g_panel.track_x + g_panel.track_w * t - 6 * s, ry + row_h * .5f - 6 * s, 12 * s, 12 * s, g_panel.dragging == i ? 0xffffffff : 0xffedc56c);
        value_text(i, p, buf, 96);
        text(g_panel.track_x, ry - 9 * s, g_panel.track_w, 14 * s, buf, 10 * s, 0xffaeb7c2, ANY_GPU_CENTER);
    }
}

void set_from_mouse(int slider, int32_t mx) {
    float t = std::clamp((float(mx) - g_panel.track_x) / std::max(1.f, g_panel.track_w), 0.f, 1.f);
    int step = int(std::lround(t * max_step(slider)));
    if (g_panel.draft.v[slider] == step) return;
    g_panel.draft.v[slider] = uint8_t(step);
    uint64_t now = GetTickCount64();
    if (now - g_panel.last_commit >= 150) { g_panel.pending = pack(g_panel.draft); g_panel.pending_row = g_panel.row_id; g_panel.has_pending = true; g_panel.last_commit = now; }
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_panel.dragging = -1; return 0; }
    if (!g_panel.visible || decode(g_panel.value).mode != Mode::Custom) return 0;
    bool inside = e->x >= g_panel.x && e->x < g_panel.x + g_panel.w && e->y >= g_panel.y && e->y < g_panel.y + g_panel.row_h * kSliderCount;
    if (e->kind == ANY_MOUSE_DOWN && e->button == 0 && inside) {
        g_panel.dragging = std::clamp(int((e->y - g_panel.y) / g_panel.row_h), 0, kSliderCount - 1);
        set_from_mouse(g_panel.dragging, e->x);
        return 1;
    }
    if (e->kind == ANY_MOUSE_MOVE && g_panel.dragging >= 0) { set_from_mouse(g_panel.dragging, e->x); return 1; }
    if (e->kind == ANY_MOUSE_UP && g_panel.dragging >= 0) {
        g_panel.dragging = -1;
        g_panel.pending = pack(g_panel.draft); g_panel.pending_row = g_panel.row_id; g_panel.has_pending = true; g_panel.last_commit = GetTickCount64();
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
