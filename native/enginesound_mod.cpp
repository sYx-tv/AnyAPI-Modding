// EngineSound: real, generated engine sounds. Every inline, V and boxer engine gets its own sound, made sample by
// sample from its cylinders firing in their real order (enginesound_synth.h), with the cylinder count taken from how
// far the engine is stretched. The Properties tool's sound row offers that layout's five sounds, the game's own
// three and Custom; the Engine sound panel names them and holds the Custom sliders. Co-op: the choice rides on the
// engine's existing replicated sound index, so the host stores it, saves it with the vehicle and replicates it;
// every player with the mod hears the same.
//
// The sound streams like the AnyEngineParts rotary: 48 kHz mono PCM into a positional game voice, with the engine's
// stock running and power voices silenced. Rotary engines are left to AnyEngineParts.
//
// EXPERIMENTAL. This mod hooks game functions directly through the experimental SDK (see
// enginesound_bindings.h). The hooks resolve only on Anymaker 0.1.27 / Steam build 26113663 and are not
// reviewed as an AnyAPI service yet. The first in-game runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_experimental.hpp"
#include "enginesound_bindings.h"
#include "enginesound_params.h"
#include "enginesound_save.h"
#include "route_cell.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

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
constexpr ptrdiff_t client_definition = 176;         // client_scene.vehicle_component.m_definition
constexpr ptrdiff_t client_extend_z = 152 + 8 + 8;   // m_extend_size (property_vec3_s32): value at +8, z at +8
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t mat34_t = 72;                    // mat34.t (vec3)
constexpr ptrdiff_t client_peer_data = 40 + 104;     // client.m_peers (client_peer_container).m_data
// client_scene.peer.property_data.property_s32_range
constexpr ptrdiff_t row_id = 8, row_name = 16 + 8, row_value = 32 + 8, row_prev = 48, row_edit = 52;
constexpr ptrdiff_t row_min = 64 + 8, row_max = 80 + 8;
// Server side (server thread only)
constexpr ptrdiff_t server_engine_sound = 576;       // replication.master.property_s32
constexpr ptrdiff_t server_definition = 312;         // server_scene.vehicle_component.m_definition
constexpr ptrdiff_t server_extend_z = 272 + 24 + 8;  // m_extend_size (master property_vec3_s32 at 0x110): value at +24, z at +8
constexpr ptrdiff_t pd_elements = 72 + 24;           // property_data._m_properties._m_elements (vector<ref<property_base>>)
// server_scene.peer.property_data.property_replication_s32_range (168 bytes)
constexpr ptrdiff_t srow_name = 32 + 24, srow_value_ptr = 96, srow_min_modified = 104 + 16, srow_min = 104 + 24;
constexpr ptrdiff_t srow_max_modified = 136 + 16, srow_max = 136 + 24;
constexpr size_t srow_size = 168;
// Saves (enginesound_save.h). The sound row's value is the engine's master property_s32 (modified flag, value).
constexpr ptrdiff_t server_sound_modified = server_engine_sound + 16, server_sound_value = server_engine_sound + 24;
constexpr ptrdiff_t server_component_id = 64;        // server_scene.vehicle_component.m_id (s32, saved as "id")
constexpr ptrdiff_t save_vehicle_id = 0;             // server_scene.save_data.vehicle.m_id (saved as "id", read first)
constexpr ptrdiff_t parser_format = 0, parser_mode = 4;   // server_scene.save_data.parser._m_format, _m_mode
}  // namespace off
constexpr int32_t kParserJson = 0;               // server_scene.save_data.e_parser_format.json (binary = 1)
constexpr int32_t kParseWrite = 1;               // e_parse_mode.write (read = 0)
constexpr int32_t kSoundLabel = 327;             // e_localization_string.property_sound_effect (static)

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
using engine_tick_t = void (*)(uint8_t* engine, void* client, void* vehicle, void* scene, const double* dt);
using vol_speed_t = void (*)(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos);
using props_t = void (*)(uint8_t* engine, uint8_t* property_data);
using row_ui_t = void (*)(uint8_t* row, void* frontend_ui, uint8_t* client, const int32_t* a, const int32_t* b);
using push_s32_t = void (*)(uint8_t* peer_data, const int32_t* id, const int32_t* value);
anymaker::cell_hook h_tick, h_vol_speed, h_props, h_row;
void** g_push_cell;

// ------------------------------------------------------------------ which engine (any thread)
// Keyed by definition pointer, with the id the kind was worked out from. Every world loads its part definitions anew
// (client_scene.create and server_scene.create), and one can land where the last world's definition of another engine
// was: an entry counts only while the definition there still has the same id.
struct KnownKind { const char* id = nullptr; int32_t length = 0; char text[64]{}; EngineKind kind{}; };
std::mutex g_kind_mutex;
std::unordered_map<const void*, KnownKind> g_kinds;

EngineKind kind_of(const void* definition) {
    if (!definition) return engine_kind(nullptr, 0);
    const auto* base = static_cast<const uint8_t*>(definition);
    anymaker::gc_string_view id{};
    if (anymaker::readable(base + off::definition_id, sizeof id)) std::memcpy(&id, base + off::definition_id, sizeof id);
    const bool usable = id.data && id.length > 0 && id.length < 64;
    std::lock_guard<std::mutex> lock(g_kind_mutex);
    auto found = g_kinds.find(definition);
    if (found != g_kinds.end() && found->second.id == id.data && found->second.length == id.length &&
        (!usable || std::memcmp(found->second.text, id.data, size_t(id.length)) == 0)) return found->second.kind;
    KnownKind known;
    known.id = id.data; known.length = id.length;
    known.kind = engine_kind(nullptr, 0);
    if (usable && anymaker::readable(id.data, size_t(id.length))) {
        std::memcpy(known.text, id.data, size_t(id.length));
        known.kind = engine_kind(id.data, size_t(id.length));
        if (!known.kind.known) {
            static int logs = 6;
            if (logs-- > 0) log(1, "engine part '%.*s' is not one EngineSound knows; it sounds like an inline", int(id.length), id.data);
        }
    } else known.id = nullptr;                       // not read: never matches, so it is tried again next time
    if (g_kinds.size() > 4096) g_kinds.clear();
    g_kinds[definition] = known;
    return known.kind;
}
const void* pointer_at(const uint8_t* base, ptrdiff_t offset) {
    if (!base || !anymaker::readable(base + offset, 8)) return nullptr;
    return rd<const void*>(base, offset);
}
int32_t extend_at(const uint8_t* base, ptrdiff_t offset) {
    if (!base || !anymaker::readable(base + offset, 4)) return 0;
    return rd<int32_t>(base, offset);
}

// ------------------------------------------------------------------ generated sound (client, main thread)
namespace audio {
constexpr uint32_t play_voice_rva = 0x13f240;       // (effect_3d) application.audio.play_voice (s32 group, vec3, vec3, f64)
constexpr uint32_t append_rva = 0x13f9c0;           // effect_3d.append_audio_data (takes the buffer's memory)
constexpr uint32_t set_position_velocity_rva = 0x13f9a0;
constexpr uint32_t stop_rva = 0x13f970;
constexpr uint32_t reset_rva = 0x13f8b0;            // effect_3d.reset (releases the handle)
constexpr uint32_t buffer_reset_rva = 0x41a00;      // binary.buffer.reset (allocates with the game's allocator)
constexpr int32_t kGroupVehicle = 4;                // e_audio_group.vehicle: follows the vehicle volume slider
constexpr double kRange = 160.0;                    // metres
constexpr double kLead = 0.08;                      // seconds of sound queued ahead
// The voice object keeps the PCM still to play: a byte buffer whose size is at +0x78 and a read cursor in samples
// at +0x80 (the same as the AnyEngineParts sounds read).
constexpr int kVoiceBytes = 0x78, kVoiceCursor = 0x80;
}  // namespace audio
struct GameBuffer { void* data; int32_t size; uint32_t pad; };
static_assert(sizeof(GameBuffer) == 16, "binary.buffer");
using play_voice_t = void (*)(void** effect, const int32_t* group, const double* position, const double* velocity, const double* range);
using effect_fn_t = void (*)(void** effect);
using effect_append_t = void (*)(void** effect, GameBuffer* buffer);
using effect_place_t = void (*)(void** effect, const double* position, const double* velocity);
using buffer_reset_t = void (*)(GameBuffer* buffer, const int32_t* size);
using world_transform_t = void (*)(double* ret, const void* component);
play_voice_t g_play_voice;
effect_fn_t g_effect_stop, g_effect_reset;
effect_append_t g_effect_append;
effect_place_t g_effect_place;
buffer_reset_t g_buffer_reset;
world_transform_t g_world_transform;
bool g_sound_ok = false;                            // streaming found: generated sounds play

struct ClientEngine {
    void* effect = nullptr;                          // application.audio.effect_3d (a handle)
    synth::Synth synth;
    double start = 0, last_tick = 0, retry_at = 0;
    long long written = 0;                           // samples queued since start
    double position[3]{}, velocity[3]{};
    bool placed = false;
    double game_volume = 0;                          // the stock power voice's volume this tick (> 0 while it runs)
    bool game_volume_seen = false;
    double quiet_since = -1;
    int32_t logged_value = INT32_MAX; int logged_cylinders = 0;
    bool was_running = false;                        // last tick's firing state, for the start and stop log lines
};
std::unordered_map<const void*, ClientEngine> g_engines;   // keyed by client engine
std::vector<int16_t> g_pcm;
double g_last_sweep = 0;
thread_local uint8_t* t_engine;   // engine whose client tick is running on this thread
thread_local bool t_generated;    // ... and it plays a generated sound

double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}
void release_voice(ClientEngine& e) {
    if (!e.effect) return;
    g_effect_stop(&e.effect);
    g_effect_reset(&e.effect);
    e.effect = nullptr;
}
// Samples the voice still has to play, or -1 when its fields don't look right.
long long voice_queued(void* effect) {
    auto* base = static_cast<uint8_t*>(effect);
    if (!effect || !anymaker::readable(base + audio::kVoiceBytes, 0x10)) return -1;
    int32_t bytes = rd<int32_t>(base, audio::kVoiceBytes), cursor = rd<int32_t>(base, audio::kVoiceCursor);
    if (bytes < 0 || cursor < 0 || bytes > 64 * synth::kSampleRate) return -1;
    return std::max<long long>(0, bytes / 2 - cursor);
}

void stream_sound(uint8_t* self, const EngineKind& kind, int32_t value, const double* dt) {
    double now = now_seconds();
    ClientEngine& v = g_engines[self];
    v.last_tick = now;
    synth::Input in;
    in.rps = synth::sound_rps(synth::clamp01(rd<double>(self, off::engine_factor_speed)) * synth::kMaxRps);
    in.load = synth::clamp01(rd<double>(self, off::engine_factor_power));
    in.layout = kind.layout;
    in.cylinders = cylinders(kind, extend_at(self, off::client_extend_z));
    in.tune = tune_of(decode(value, kind.layout), kind);
    // Firing: the game's power voice is audible.
    in.running = v.game_volume_seen ? v.game_volume > 0.001 : in.rps >= synth::kIdleRps;
    if (in.running != v.was_running) {
        static std::atomic<int> logs{16};
        if (logs.fetch_sub(1) > 0)
            log(0, "engine %s at %.0f rpm (game %.0f rpm), load %.2f, stock power volume %s%.3f, gain %.2f, starter %.2f", in.running ? "firing" : "stopped firing",
                in.rps * 60, synth::clamp01(rd<double>(self, off::engine_factor_speed)) * synth::kMaxRps * 60, in.load, v.game_volume_seen ? "" : "(not seen, by speed) ",
                v.game_volume, in.tune.gain, v.synth.starter_env);
        v.was_running = in.running;
    }
    v.game_volume_seen = false;
    if (v.logged_value != value || v.logged_cylinders != in.cylinders) {
        static std::atomic<int> logs{24};
        char name[32];
        engine_name(kind.layout, in.cylinders, name, sizeof name);
        if (logs.fetch_sub(1) > 0) log(0, "%s %s (extend z %d): %s", size_name(kind.size), name, extend_at(self, off::client_extend_z), choice_name(value, kind.layout));
        v.logged_value = value; v.logged_cylinders = in.cylinders;
    }
    double matrix[12]{};
    g_world_transform(matrix, self);
    const double* p = matrix + off::mat34_t / 8;
    double step = dt && std::isfinite(*dt) && *dt > 1e-4 ? *dt : 1.0 / 60.0;
    if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) {
        for (int i = 0; i < 3; ++i) {
            double vel = v.placed ? (p[i] - v.position[i]) / step : 0.0;
            vel = std::max(-150.0, std::min(150.0, vel));
            v.velocity[i] += (vel - v.velocity[i]) * 0.2;   // frame-to-frame jitter would wobble the pitch
            v.position[i] = p[i];
        }
        v.placed = true;
    }
    bool audible = in.rps > 0.3 || v.synth.rps > 0.3 || v.synth.time < v.synth.crackle_until;
    if (audible) v.quiet_since = -1;
    else if (v.effect) {                             // a parked engine hands its voice back after 2 s of silence
        if (v.quiet_since < 0) v.quiet_since = now;
        else if (now - v.quiet_since > 2.0) { release_voice(v); v.quiet_since = -1; return; }
    }
    if (!v.effect) {
        if (!audible || now < v.retry_at || !v.placed) return;
        int32_t group = audio::kGroupVehicle; double range = audio::kRange;
        g_play_voice(&v.effect, &group, v.position, v.velocity, &range);
        if (!v.effect) {
            v.retry_at = now + 1.0;
            static std::atomic<int> logs{3};
            if (logs.fetch_sub(1) > 0) log(1, "the game gave no voice for an engine's sound; retrying");
            return;
        }
        v.start = now; v.written = 0;
    }
    g_effect_place(&v.effect, v.position, v.velocity);
    // Keep about kLead seconds queued, by the voice's own queue when it can be read, else by the clock.
    long long played = (long long)((now - v.start) * synth::kSampleRate);
    long long queued = voice_queued(v.effect);
    if (queued >= 0) played = v.written - queued;
    if (v.written < played) v.written = played;
    long long want = played + (long long)(audio::kLead * synth::kSampleRate) - v.written;
    if (want <= 0) return;
    int n = int(std::min<long long>(want, synth::kSampleRate / 5));
    g_pcm.resize(size_t(n));
    synth::render(v.synth, in, g_pcm.data(), n);
    GameBuffer buffer{nullptr, 0, 0};
    int32_t bytes = n * 2;
    g_buffer_reset(&buffer, &bytes);
    if (!buffer.data || buffer.size != bytes) return;
    std::memcpy(buffer.data, g_pcm.data(), size_t(bytes));
    g_effect_append(&v.effect, &buffer);
    if (buffer.data) { int32_t zero = 0; g_buffer_reset(&buffer, &zero); }   // the effect refused it (queue full)
    v.written += n;
}

void sweep_engines(double now) {
    if (now - g_last_sweep < 1.0) return;
    g_last_sweep = now;
    for (auto it = g_engines.begin(); it != g_engines.end();)
        if (now - it->second.last_tick > 1.0) { release_voice(it->second); it = g_engines.erase(it); }
        else ++it;
}

void hk_tick(uint8_t* engine, void* client, void* vehicle, void* scene, const double* dt) {
    auto original = reinterpret_cast<engine_tick_t>(h_tick.original);
    if (!engine || !g_sound_ok) return original(engine, client, vehicle, scene, dt);
    EngineKind kind = kind_of(pointer_at(engine, off::client_definition));
    int32_t value = rd<int32_t>(engine, off::engine_sound_index);
    bool generated = !kind.rotary && decode(value, kind.layout).mode != Mode::Vanilla;
    if (!generated) {
        auto found = g_engines.find(engine);   // switched back to a game sound: hand the voice back
        if (found != g_engines.end()) { release_voice(found->second); g_engines.erase(found); }
        return original(engine, client, vehicle, scene, dt);
    }
    uint8_t* prev = t_engine; bool prev_generated = t_generated;
    t_engine = engine; t_generated = true;
    g_engines[engine];                               // so the voice hook can record this tick's power volume
    original(engine, client, vehicle, scene, dt);
    t_engine = prev; t_generated = prev_generated;
    stream_sound(engine, kind, value, dt);
    sweep_engines(now_seconds());
}

// A generated engine's stock running and power voices are silenced (a zero-volume update ends a game voice); its
// knock while damaged and its leak pass through. The power voice's volume is kept: it says the engine is firing.
void hk_vol_speed(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos) {
    auto original = reinterpret_cast<vol_speed_t>(h_vol_speed.original);
    uint8_t* e = t_engine;
    if (!e || !t_generated || !single || !volume) return original(single, volume, speed, effect, group, pos);
    ptrdiff_t o = single - e;
    if (o != off::voice_operate && o != off::voice_power && o != off::voice_knock) return original(single, volume, speed, effect, group, pos);
    if (o == off::voice_power) {
        auto found = g_engines.find(e);
        if (found != g_engines.end()) { found->second.game_volume = *volume; found->second.game_volume_seen = true; }
    }
    if (o == off::voice_knock && rd<uint8_t>(e, off::engine_is_damage)) return original(single, volume, speed, effect, group, pos);
    double zero = 0;
    original(single, &zero, speed, effect, group, pos);
}

// Host: widen the engine's sound row so the generated sounds, Custom and packed tunes are accepted and saved, and
// tag it with the engine (layout, size, cylinders) for the clients' panel.
std::atomic<int> g_range_logs{6};
void hk_props(uint8_t* engine, uint8_t* pd) {
    reinterpret_cast<props_t>(h_props.original)(engine, pd);
    if (!engine || !pd || !anymaker::readable(pd + off::pd_elements, sizeof(anymaker::gc_vector_raw))) return;
    EngineKind kind = kind_of(pointer_at(engine, off::server_definition));
    if (kind.rotary) return;                         // AnyEngineParts' rotary keeps its own ports row
    int32_t ext = extend_at(engine, off::server_extend_z);
    int n = cylinders(kind, ext);
    auto* vec = reinterpret_cast<const anymaker::gc_vector_raw*>(pd + off::pd_elements);
    for (int32_t i = 0; i < vec->count && i < 64; ++i) {
        auto* ref = vec->at<anymaker::gc_ref_raw<uint8_t>>(i);
        uint8_t* row = ref ? ref->ptr : nullptr;
        if (!row || !anymaker::readable(row, off::srow_size)) continue;
        if (rd<uint8_t*>(row, off::srow_value_ptr) != engine + off::server_engine_sound) continue;
        int32_t lo = rd<int32_t>(row, off::srow_min), hi = rd<int32_t>(row, off::srow_max), name = rd<int32_t>(row, off::srow_name);
        char engine_label[32];
        engine_name(kind.layout, n, engine_label, sizeof engine_label);
        if (g_range_logs.fetch_sub(1) > 0)
            log(0, "host engine sound row: %s %s (extend z %d), label=%d range %d..%d value=%d", size_name(kind.size), engine_label, ext, name, lo, hi, rd<int32_t>(engine, off::server_engine_sound + 24));
        if (lo != kVanillaMin || hi != kVanillaMax || name != kSoundLabel) {
            log(1, "engine sound row differs from the expected %d..%d (label %d); leaving it vanilla", kVanillaMin, kVanillaMax, kSoundLabel);
            return;
        }
        wr<int32_t>(row, off::srow_min, kServerMin);
        wr<uint8_t>(row, off::srow_min_modified, 1);
        wr<int32_t>(row, off::srow_max, kServerMax - row_tag(kind, n));
        wr<uint8_t>(row, off::srow_max_modified, 1);
        return;
    }
}

// ------------------------------------------------------------------ saves stay stock (host: server and worker threads)
// A save only ever holds a stock sound, so an engine still plays without the mod (enginesound_save.h). The game
// reads and writes an engine's save data with the same parse call: on write the engine holds kSavedStockSound just
// while the game writes it (its modified flag untouched, so the stand-in is never replicated), and the choice is kept
// beside it; on read the choice comes back.
using parse_save_t = void (*)(uint8_t* self, uint8_t* parser);
using parse_s32_t = void (*)(uint8_t* parser, const anymaker::gc_string_view* name, int32_t* value, const int32_t* fallback);
using string_ctor_t = void (*)(anymaker::gc_string_view* s, const char* text);
using string_dtor_t = void (*)(anymaker::gc_string_view* s);
anymaker::cell_hook h_save_engine, h_save_vehicle;
parse_s32_t g_parse_s32;
string_ctor_t g_string_ctor;
string_dtor_t g_string_dtor;
thread_local const uint8_t* t_save_vehicle;   // the save_data.vehicle being read or written on this thread

// The world's choices, mirrored in a file in the game's working folder (which the game swaps when a world loads).
struct FileStamp { bool exists = false; FILETIME created{}, written{}; uint64_t size = 0; };
std::mutex g_save_mutex;
SoundMap g_saved;
std::wstring g_saved_path;                       // empty: the folder was not found, world saves keep the choice as is
FileStamp g_saved_stamp;
bool g_saved_dirty = false;

FileStamp stamp_of(const std::wstring& path) {
    FileStamp s;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return s;
    s.exists = true; s.created = a.ftCreationTime; s.written = a.ftLastWriteTime;
    s.size = (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return s;
}
bool same_stamp(const FileStamp& a, const FileStamp& b) {
    if (a.exists != b.exists) return false;
    return !a.exists || (CompareFileTime(&a.created, &b.created) == 0 && CompareFileTime(&a.written, &b.written) == 0 && a.size == b.size);
}
std::string read_text(const std::wstring& path) {
    std::string out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20)) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr)) got = 0;
        out.resize(got);
    }
    CloseHandle(f);
    return out;
}
bool write_text(const std::wstring& path, const std::string& text) {
    std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size();
    CloseHandle(f);
    return ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
void init_saved_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH || GetFileAttributesW((std::wstring(buf) + L"\\Anymaker").c_str()) == INVALID_FILE_ATTRIBUTES) {
        log(1, "the game's user folder (%%APPDATA%%\\Anymaker) was not found; world saves keep modded sounds as they are");
        return;
    }
    g_saved_path = std::wstring(buf) + L"\\Anymaker\\working\\enginesound_sounds.json";
}
// With g_save_mutex held: reload the choices when the world (the working folder's file) changed under us.
void sync_saved() {
    FileStamp s = stamp_of(g_saved_path);
    if (same_stamp(s, g_saved_stamp)) return;
    size_t before = g_saved.size();
    g_saved = s.exists ? decode_saved(read_text(g_saved_path)) : SoundMap{};
    g_saved_stamp = s;
    g_saved_dirty = false;
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "world engine sounds %s: %zu engines (had %zu)", s.exists ? "loaded" : "file gone (another world)", g_saved.size(), before);
}
void flush_saved() {
    if (!g_saved_dirty) return;
    if (!write_text(g_saved_path, encode_saved(g_saved))) {
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "could not write the world engine sounds file (error %lu)", GetLastError());
        return;
    }
    g_saved_stamp = stamp_of(g_saved_path);
    g_saved_dirty = false;
}
SoundKey save_key(const uint8_t* save_vehicle, const uint8_t* engine) {
    return SoundKey{rd<int32_t>(save_vehicle, off::save_vehicle_id), rd<int32_t>(engine, off::server_component_id)};
}
// Reads or writes kSaveKey in the engine's saved-vehicle (JSON) data.
void parse_key(uint8_t* parser, int32_t* value, const int32_t* fallback) {
    anymaker::gc_string_view name{};
    g_string_ctor(&name, kSaveKey);
    g_parse_s32(parser, &name, value, fallback);
    g_string_dtor(&name);
}

void hk_save_vehicle(uint8_t* save_vehicle, uint8_t* parser) {
    const uint8_t* prev = t_save_vehicle;
    t_save_vehicle = save_vehicle;
    reinterpret_cast<parse_save_t>(h_save_vehicle.original)(save_vehicle, parser);
    t_save_vehicle = prev;
    if (!prev && parser && rd<int32_t>(parser, off::parser_mode) == kParseWrite && !g_saved_path.empty()) {
        std::lock_guard lock(g_save_mutex);
        flush_saved();                                // once per vehicle, before the game copies the folder
    }
}

std::atomic<int> g_save_logs{12};
void hk_save_engine(uint8_t* engine, uint8_t* parser) {
    auto original = reinterpret_cast<parse_save_t>(h_save_engine.original);
    if (!engine || !parser || !anymaker::readable(parser, 8) || !anymaker::readable(engine + off::server_sound_modified, 12))
        return original(engine, parser);
    const bool json = rd<int32_t>(parser, off::parser_format) == kParserJson;
    const uint8_t* vehicle = t_save_vehicle;
    // World saves need the vehicle's id and the file; saved vehicles carry the choice themselves.
    const bool keep = json || (vehicle && !g_saved_path.empty());
    if (rd<int32_t>(parser, off::parser_mode) == kParseWrite) {
        int32_t choice = rd<int32_t>(engine, off::server_sound_value);
        if (!json && keep) {
            std::lock_guard lock(g_save_mutex);
            sync_saved();
            if (note_saved(g_saved, save_key(vehicle, engine), choice)) g_saved_dirty = true;
        }
        if (!keep || !needs_stock_save(choice)) return original(engine, parser);
        wr<int32_t>(engine, off::server_sound_value, kSavedStockSound);
        original(engine, parser);
        wr<int32_t>(engine, off::server_sound_value, choice);
        if (json) { int32_t stock = kSavedStockSound; parse_key(parser, &choice, &stock); }   // a default unlike the value, in case defaults are skipped
        if (g_save_logs.fetch_sub(1) > 0)
            log(0, "engine %d sound %d saved as stock sound %d (%s)", rd<int32_t>(engine, off::server_component_id), choice, kSavedStockSound,
                json ? "saved vehicle" : "world");
        return;
    }
    original(engine, parser);
    if (!keep) return;
    int32_t loaded = rd<int32_t>(engine, off::server_sound_value), choice = loaded;
    if (json) {
        int32_t kept = loaded;
        parse_key(parser, &kept, &loaded);
        choice = restored_from_key(loaded, kept);
    } else {
        std::lock_guard lock(g_save_mutex);
        sync_saved();
        choice = restored(g_saved, save_key(vehicle, engine), loaded);
    }
    if (choice == loaded) return;
    wr<int32_t>(engine, off::server_sound_value, choice);
    wr<uint8_t>(engine, off::server_sound_modified, 1);
    if (g_save_logs.fetch_sub(1) > 0)
        log(0, "engine %d sound %d restored (saved as stock sound %d, %s)", rd<int32_t>(engine, off::server_component_id), choice, loaded,
            json ? "saved vehicle" : "world");
}

// Optional: without these hooks the sounds still work and saves hold the choice as before.
void install_save_hooks() {
    using namespace anymaker::experimental;
    void** engine = hook_cell(bind::server_engine_parse_save_data);
    void** vehicle = route::cell(bind::save_vehicle_parse_save_data);
    g_parse_s32 = reinterpret_cast<parse_s32_t>(route::function(bind::parser_parse_s32));
    g_string_ctor = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
    g_string_dtor = reinterpret_cast<string_dtor_t>(native(anymaker::sym::string_dtor_rva));
    if (!engine || !vehicle || !g_parse_s32 || !g_string_ctor || !g_string_dtor) {
        log(1, "save functions not found (engine=%d vehicle=%d value=%d string=%d/%d); saves keep modded sounds as they are, which the game can't play without EngineSound",
            !!engine, !!vehicle, !!g_parse_s32, !!g_string_ctor, !!g_string_dtor);
        return;
    }
    init_saved_path();
    // The vehicle hook first: once the engine hook is in, world saves need the vehicle's id.
    if (!h_save_vehicle.install(vehicle, (void*)&hk_save_vehicle) || !h_save_engine.install(engine, (void*)&hk_save_engine)) {
        h_save_vehicle.remove((void*)&hk_save_vehicle);
        log(1, "a save hook cell was already taken or not writable; saves keep modded sounds as they are");
        return;
    }
    log(0, "saves hold a stock sound; the choice is kept %s and in saved vehicles", g_saved_path.empty() ? "only in saved vehicles (no world file)" : "in the world's enginesound_sounds.json");
}

// ------------------------------------------------------------------ panel state (shared by threads)
std::mutex g_mutex;
struct Panel {
    uint64_t seen = 0;            // GetTickCount64 when the modded row last drew
    int32_t row_id = 0, value = 0, tag = 0;
    Params draft{};
    int dragging = -1;
    uint64_t last_commit = 0;
    bool has_pending = false; int32_t pending = 0, pending_row = 0;
    float x = 0, w = 0, scale = 1, track_x = 0, track_w = 0;   // last layout, for input
    float list_y = 0, item_h = 0, button_y = 0, button_h = 0, sliders_y = 0, row_h = 0, bottom = 0;
    bool visible = false;
} g_panel;

constexpr int kOptionCount = 3 + synth::kPresetsPerLayout + 1;   // game 1-3, the layout's five, Custom
int32_t option_value(int i) { return i < 3 ? kVanillaMin + i : i < 3 + synth::kPresetsPerLayout ? kFirstPreset + (i - 3) : kCustomIndex; }
bool option_selected(int i, int32_t value) {
    if (i == kOptionCount - 1) return is_packed(value) || value >= kCustomIndex;
    return option_value(i) == value;
}
Layout panel_layout(int* cylinders_out = nullptr) {
    EngineKind k; int n;
    decode_tag(g_panel.tag, k, n);
    if (cylinders_out) *cylinders_out = n;
    return k.layout;
}
void queue_send(int32_t value) {
    g_panel.pending = value; g_panel.pending_row = g_panel.row_id; g_panel.has_pending = true; g_panel.last_commit = GetTickCount64();
}

void hk_row(uint8_t* row, void* fui, uint8_t* client, const int32_t* a, const int32_t* b) {
    auto original = reinterpret_cast<row_ui_t>(h_row.original);
    int32_t real_max = row ? rd<int32_t>(row, off::row_max) : 0;
    if (!row || rd<int32_t>(row, off::row_name) != kSoundLabel || !is_modded_max(real_max))
        return original(row, fui, client, a, b);
    int32_t id = rd<int32_t>(row, off::row_id), value = rd<int32_t>(row, off::row_value);
    bool send = false; int32_t to_send = 0;
    {
        std::lock_guard lock(g_mutex);
        uint64_t now = GetTickCount64();
        if (g_panel.row_id != id || now - g_panel.seen > 1000) { g_panel.dragging = -1; g_panel.has_pending = false; }
        g_panel.seen = now; g_panel.row_id = id; g_panel.tag = kServerMax - real_max;
        if (g_panel.value != value && g_panel.dragging < 0 && now - g_panel.last_commit > 1000) g_panel.draft = decode(value, panel_layout()).params;
        g_panel.value = value;
        if (g_panel.has_pending && g_panel.pending_row == id) { send = true; to_send = g_panel.pending; g_panel.has_pending = false; }
    }
    if (send && client && g_push_cell && *g_push_cell)
        reinterpret_cast<push_s32_t>(*g_push_cell)(client + off::client_peer_data, &id, &to_send);
    // Show the native slider as 0..Custom; a packed value displays at the Custom position.
    int32_t shown = is_packed(value) || value > kCustomIndex ? kCustomIndex : value;
    const ptrdiff_t fields[] = {off::row_value, off::row_prev, off::row_edit};
    bool swapped[3]{};
    for (int i = 0; i < 3; ++i) if (shown != value && rd<int32_t>(row, fields[i]) == value) { wr<int32_t>(row, fields[i], shown); swapped[i] = true; }
    int32_t real_min = rd<int32_t>(row, off::row_min);
    wr<int32_t>(row, off::row_min, kVanillaMin);
    wr<int32_t>(row, off::row_max, kCustomIndex);
    original(row, fui, client, a, b);
    if (rd<int32_t>(row, off::row_max) == kCustomIndex) wr<int32_t>(row, off::row_max, real_max);
    if (rd<int32_t>(row, off::row_min) == kVanillaMin) wr<int32_t>(row, off::row_min, real_min);
    for (int i = 0; i < 3; ++i) if (swapped[i] && rd<int32_t>(row, fields[i]) == shown) wr<int32_t>(row, fields[i], value);
}

// ------------------------------------------------------------------ install (background thread)
// Streaming is required: without it every engine keeps the game's own sound.
bool find_streaming() {
    using namespace anymaker::experimental;
    g_world_transform = reinterpret_cast<world_transform_t>(function(bind::client_get_world_transform));
    g_play_voice = reinterpret_cast<play_voice_t>(native(audio::play_voice_rva));
    g_effect_append = reinterpret_cast<effect_append_t>(native(audio::append_rva));
    g_effect_place = reinterpret_cast<effect_place_t>(native(audio::set_position_velocity_rva));
    g_effect_stop = reinterpret_cast<effect_fn_t>(native(audio::stop_rva));
    g_effect_reset = reinterpret_cast<effect_fn_t>(native(audio::reset_rva));
    g_buffer_reset = reinterpret_cast<buffer_reset_t>(native(audio::buffer_reset_rva));
    return g_world_transform && g_play_voice && g_effect_append && g_effect_place && g_effect_stop && g_effect_reset && g_buffer_reset;
}
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; EngineSound stays off"); return; }
        void** tick = hook_cell(bind::client_engine_tick);
        void** vol = hook_cell(bind::effect_update_play_volume_speed);
        void** props = hook_cell(bind::server_engine_get_properties);
        void** row = hook_cell(bind::client_property_s32_range_update_ui);
        void** push = hook_cell(bind::client_push_set_property_s32);
        bool streaming = find_streaming();
        if (!(tick && vol && props && row && push && streaming)) {
            if (attempt == 179) log(2, "could not locate game functions (tick=%d volume=%d props=%d row=%d push=%d streaming=%d)",
                !!tick, !!vol, !!props, !!row, !!push, streaming);
            continue;
        }
        g_push_cell = push;
        // The voice hook goes in before the tick hook: once the tick hook is in, generated engines need their stock
        // voices silenced.
        bool ok = h_vol_speed.install(vol, (void*)&hk_vol_speed) && h_tick.install(tick, (void*)&hk_tick) &&
                  h_props.install(props, (void*)&hk_props) && h_row.install(row, (void*)&hk_row);
        if (!ok) { log(2, "a hook cell was already taken or not writable; EngineSound is off"); return; }
        g_sound_ok = true;
        install_save_hooks();
        g_ready = true;
        log(0, "Ready after %d s: generated sounds %d..%d, Custom %d", attempt, kFirstPreset, kCustomIndex - 1, kCustomIndex);
        return;
    }
}
void remove_hooks() {
    g_sound_ok = false;
    h_save_engine.remove((void*)&hk_save_engine); h_save_vehicle.remove((void*)&hk_save_vehicle);
    h_row.remove((void*)&hk_row); h_props.remove((void*)&hk_props);
    h_tick.remove((void*)&hk_tick); h_vol_speed.remove((void*)&hk_vol_speed);
}

// ------------------------------------------------------------------ panel drawing and input
void value_text(int s, const Params& p, Layout layout, int cyl, wchar_t* buf, size_t n) {
    const int v = p.v[s];
    static const wchar_t* four[4] = {L"Off", L"Light", L"Medium", L"Heavy"};
    switch (s) {
    case kNote: swprintf(buf, n, L"%d Hz %ls", int(note_hz(v)), v < 5 ? L"deep" : v < 10 ? L"" : L"high"); break;
    case kBark: swprintf(buf, n, L"%d Hz", int(bark_hz(v))); break;
    case kMuffler: { static const wchar_t* m[8] = {L"Straight pipe", L"Open", L"Race", L"Loud", L"Sport", L"Mild", L"Stock", L"Quiet"}; swprintf(buf, n, L"%ls", m[v & 7]); break; }
    case kIntake: case kMech: swprintf(buf, n, L"%ls", four[v & 3]); break;
    case kFiring: swprintf(buf, n, L"%hs", firing_name(layout, cyl, v)); break;
    case kDiesel: swprintf(buf, n, L"%ls", v ? L"Diesel" : L"Petrol"); break;
    case kVolume: swprintf(buf, n, L"%d%%", int(std::lround(volume_gain(v) * 100))); break;
    default: swprintf(buf, n, v ? L"%d / 7" : L"Off", v); break;
    }
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_mutex);
    g_panel.visible = g_ready && frame->focused && GetTickCount64() - g_panel.seen < 300;
    if (!g_panel.visible) { g_panel.dragging = -1; return; }
    int cyl = 4;
    Layout layout = panel_layout(&cyl);
    EngineKind kind; int unused;
    decode_tag(g_panel.tag, kind, unused);
    Choice c = decode(g_panel.value, layout);
    bool custom = c.mode == Mode::Custom;
    float s = std::clamp(float(frame->height) / 1080.f, .7f, 1.8f);
    float w = 380 * s, head = 58 * s, item_h = 24 * s, row_h = 26 * s;
    float h = head + kOptionCount * item_h + 10 * s + (custom ? row_h * kSliderCount + 12 * s : 40 * s);
    float x = std::max(0.f, float(frame->width) - w - 24 * s), y = std::max(8 * s, float(frame->height) * .5f - h * .5f);
    g_panel.x = x; g_panel.w = w; g_panel.scale = s; g_panel.item_h = item_h; g_panel.row_h = row_h;
    g_panel.list_y = y + head;
    float after_list = g_panel.list_y + kOptionCount * item_h + 10 * s;
    g_panel.button_y = after_list; g_panel.button_h = custom ? 0 : 30 * s;
    g_panel.sliders_y = custom ? after_list : 0;
    g_panel.track_x = x + 160 * s; g_panel.track_w = w - 175 * s;
    g_panel.bottom = y + h;
    auto rect = [&](uint32_t k, float rx, float ry, float rw, float rh, uint32_t color, float radius = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = k; cmd.rect[0] = rx; cmd.rect[1] = ry; cmd.rect[2] = rw; cmd.rect[3] = rh; cmd.color = color; cmd.radius = radius; gpu->emit(&cmd); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 cmd; cmd.kind = ANY_GPU_TEXT; cmd.rect[0] = tx; cmd.rect[1] = ty; cmd.rect[2] = tw; cmd.rect[3] = th; cmd.text = t;
        cmd.text_length = uint32_t(wcslen(t)); cmd.font_size = size; cmd.color = color; cmd.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&cmd); };
    rect(ANY_GPU_ROUND_RECT, x, y, w, h, 0xf0191c20, 6 * s);
    text(x + 14 * s, y + 8 * s, w - 28 * s, 18 * s, L"ENGINE SOUND", 11 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    wchar_t buf[96];
    char engine_label[32];
    engine_name(layout, cyl, engine_label, sizeof engine_label);
    if (kind.known) swprintf(buf, 96, L"%hs (%hs block): %hs", engine_label, size_name(kind.size), choice_name(g_panel.value, layout));
    else swprintf(buf, 96, L"%hs", choice_name(g_panel.value, layout));
    text(x + 14 * s, y + 26 * s, w - 28 * s, 26 * s, buf, 16 * s, 0xfff5f7fa);
    // The options: this layout's five sounds, the game's own three and Custom.
    for (int i = 0; i < kOptionCount; ++i) {
        int slot = i < synth::kPresetsPerLayout ? 3 + i : i < synth::kPresetsPerLayout + 3 ? i - synth::kPresetsPerLayout : i;   // generated first
        float ry = g_panel.list_y + i * item_h;
        bool on = option_selected(slot, g_panel.value);
        if (on) rect(ANY_GPU_ROUND_RECT, x + 10 * s, ry + 1 * s, w - 20 * s, item_h - 2 * s, 0xff3a4048, 4 * s);
        const char* label = slot < 3 ? choice_name(option_value(slot), layout) : slot == kOptionCount - 1 ? "Custom" : synth::kPresets[layout][slot - 3].name;
        swprintf(buf, 96, L"%hs", label);
        text(x + 22 * s, ry, w - 44 * s, item_h, buf, 13 * s, on ? 0xffedc56c : slot < 3 ? 0xff8c96a2 : 0xffdfe4ea, on ? ANY_GPU_BOLD : 0);
    }
    if (!custom) {
        // Copies this sound into Custom so it can be tuned from there.
        rect(ANY_GPU_ROUND_RECT, x + 14 * s, g_panel.button_y, w - 28 * s, g_panel.button_h, 0xff3a4048, 4 * s);
        text(x + 14 * s, g_panel.button_y, w - 28 * s, g_panel.button_h, L"Customize this sound", 13 * s, 0xffedc56c, ANY_GPU_CENTER | ANY_GPU_BOLD);
        return;
    }
    const Params& p = g_panel.draft;
    for (int i = 0; i < kSliderCount; ++i) {
        float ry = g_panel.sliders_y + i * row_h;
        swprintf(buf, 96, L"%hs", kSliders[i].label);
        text(x + 14 * s, ry, 145 * s, row_h, buf, 12 * s, 0xffdfe4ea);
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .6f - 2 * s, g_panel.track_w, 4 * s, 0xff3a4048, 2 * s);
        float t = float(p.v[i]) / float(max_step(i));
        rect(ANY_GPU_ROUND_RECT, g_panel.track_x, ry + row_h * .6f - 2 * s, g_panel.track_w * t, 4 * s, 0xffedc56c, 2 * s);
        rect(ANY_GPU_ELLIPSE, g_panel.track_x + g_panel.track_w * t - 6 * s, ry + row_h * .6f - 6 * s, 12 * s, 12 * s, g_panel.dragging == i ? 0xffffffff : 0xffedc56c);
        value_text(i, p, layout, cyl, buf, 96);
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
    const Layout layout = panel_layout();
    const bool inside = e->x >= g_panel.x && e->x < g_panel.x + g_panel.w && e->y >= g_panel.list_y && e->y < g_panel.bottom;
    const bool down = e->kind == ANY_MOUSE_DOWN && e->button == 1;
    // Option list.
    if (down && inside && e->y < g_panel.list_y + kOptionCount * g_panel.item_h) {
        int i = std::clamp(int((e->y - g_panel.list_y) / g_panel.item_h), 0, kOptionCount - 1);
        int slot = i < synth::kPresetsPerLayout ? 3 + i : i < synth::kPresetsPerLayout + 3 ? i - synth::kPresetsPerLayout : i;
        if (!option_selected(slot, g_panel.value)) {
            int32_t v = slot == kOptionCount - 1 ? pack(decode(g_panel.value, layout).params) : option_value(slot);
            if (slot == kOptionCount - 1) g_panel.draft = decode(g_panel.value, layout).params;
            queue_send(v);
            log(0, "panel: %s -> %s", choice_name(g_panel.value, layout), choice_name(v, layout));
        }
        return 1;
    }
    if (decode(g_panel.value, layout).mode != Mode::Custom) {
        bool on_button = inside && e->y >= g_panel.button_y && e->y < g_panel.button_y + g_panel.button_h;
        if (down && on_button) {
            g_panel.draft = decode(g_panel.value, layout).params;
            queue_send(pack(g_panel.draft));
            return 1;
        }
        return (e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP || e->kind == ANY_MOUSE_WHEEL) && inside;
    }
    bool on_sliders = inside && e->y >= g_panel.sliders_y && e->y < g_panel.sliders_y + g_panel.row_h * kSliderCount;
    if (down && on_sliders) {
        g_panel.dragging = std::clamp(int((e->y - g_panel.sliders_y) / g_panel.row_h), 0, kSliderCount - 1);
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
        gpu = nullptr; log(1, "GPU drawing unavailable: the sounds work, the Engine sound panel is hidden");
    }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
