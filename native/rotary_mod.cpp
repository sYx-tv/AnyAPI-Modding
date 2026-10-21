// Rotary: a stretchable rotary engine, 1 to 4 rotors, in the small engine's footprint, with its own sound.
//
// The part is the game's own engine class: the small engine's definition cell for cell (zones, ports, manifolds,
// gearboxes, engine wheels, oil, coolant, starting at 6 rps), with a generated rotary housing mesh (rotary_mesh.h)
// and 25% more power per cell. The engine's power, fuel and air use scale with its volume (m_volume, set once in
// create), so the mod raises that volume by 25% when a rotary is created. Everything else is the stock engine.
//
// Sound: on each client, the rotary's stock engine voices (running and power) are silenced and the mod streams its
// own synthesized exhaust note (rotary_sound.h) into a positional voice, the way Turbo streams its sounds. It reads
// the engine's replicated speed, load and sound index, so every player with the mod hears the same engine. The
// knock (when damaged) and leak voices stay stock. The engine's "Sound effect" row picks the port: 0 bridge port,
// 1 street port, 2 peripheral port. Works next to EngineSound: both hook the engine's client tick and the voice
// update, and the rotary's voices stay silent whichever mod's hook sits on top.
//
// Getting it: tech_tier 4 loot (after the 3rd bunker) in the "mechanical" group. Single player / host for now: the
// definition is added on this game's server and client scene, and every player would need the mod.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (rotary_bindings.h): Anymaker 0.1.27 / Steam
// build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "rotary_bindings.h"
#include "rotary_logic.h"
#include "rotary_mesh.h"
#include "engine_parts_shared.h"
#include "rotary_sound.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rotary {
namespace {
constexpr const char* kModId = "rotary";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// Reference layouts (metadata, Anymaker 0.1.24; unchanged in 0.1.25).
namespace off {
constexpr ptrdiff_t component_id = 64;               // server_scene.vehicle_component.m_id (s32)
constexpr ptrdiff_t component_definition = 312;      // m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t engine_volume = 1416;            // server_scene.vehicle_component.engine.m_volume (s32)
constexpr ptrdiff_t client_definition = 176;         // client_scene.vehicle_component.m_definition
constexpr ptrdiff_t client_extend_z = 152 + 8 + 8;   // m_extend_size (property_vec3_s32): value at +8, z at +8
constexpr ptrdiff_t client_factor_speed = 464 + 8;   // client engine m_factor_speed's value (f64)
constexpr ptrdiff_t client_factor_power = 488 + 8;   // m_factor_power's value (f64)
constexpr ptrdiff_t client_sound_index = 512 + 8;    // _m_sound_index's value (s32)
constexpr ptrdiff_t client_is_damage = 528 + 8;      // m_is_damage's value (bool)
constexpr ptrdiff_t voice_operate = 576, voice_power = 592, voice_knock = 608;   // audio_effect_single (leak at 624)
constexpr ptrdiff_t mat34_t = 72;                    // mat34.t (vec3)
}  // namespace off
constexpr int32_t kStoreSystem = 1;                  // file.e_store.system (as GpsPart and Turbo)

template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> const T& at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

using self_fn_t = void (*)(void* self);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
string_ctor_t g_string_ctor;
get_definition_t g_get_definition;

std::recursive_mutex g_mutex;                        // definition cache (server and main thread)
std::unordered_map<const void*, bool> g_is_rotary;   // definition pointer -> rotary?

double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

bool rotary_definition(const void* definition) {
    if (!definition) return false;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto found = g_is_rotary.find(definition);
    if (found != g_is_rotary.end()) return found->second;
    bool yes = false;
    if (anymaker::readable(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto& id = at<anymaker::gc_string_view>(definition, off::definition_id);
        if (id.data && id.length > 0 && id.length < 64 && anymaker::readable(id.data, size_t(id.length)))
            yes = is_rotary_id(id.data, size_t(id.length));
    }
    g_is_rotary[definition] = yes;
    return yes;
}
const void* definition_at(const void* component, ptrdiff_t offset) {
    if (!component || !anymaker::readable(static_cast<const uint8_t*>(component) + offset, 8)) return nullptr;
    return at<const void*>(component, offset);
}

// ------------------------------------------------------------------------------------- engine create (server)
// The stock create sets m_volume from the zones; a rotary's goes up by 25%, which raises its power (and the fuel
// and air it burns) by the same.
using create_t = void (*)(void* self, void* vehicle);
anymaker::cell_hook h_create;
void hk_create(void* self, void* vehicle) {
    reinterpret_cast<create_t>(h_create.original)(self, vehicle);
    if (!self || !rotary_definition(definition_at(self, off::component_definition))) return;
    if (!anymaker::readable(static_cast<uint8_t*>(self) + off::engine_volume, 4)) return;
    int32_t& volume = at<int32_t>(self, off::engine_volume);
    int32_t before = volume;
    volume = boosted_volume(volume);
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0)
        log(0, "Rotary created (component %d): volume %d -> %d (%d rotor%s, max power %.1f kW)", at<int32_t>(self, off::component_id), before, volume,
            before / 36, before / 36 == 1 ? "" : "s", volume * 0.4);
}

// ------------------------------------------------------------------------------------- part definition
// GamePath = file.path { e_store m_store; string m_path; } (24 bytes)
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
using add_definitions_t = void (*)(void* container, const void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
self_fn_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::string g_json_path, g_json_forward;            // absolute, backslashes / slashes; set before g_ready, then fixed

bool write_file(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, DWORD(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

// Writes the three mesh tiles into rom/meshes/components (where the game loads part meshes) and the definition
// file into "AnyAPI and Modding/Rotary". Without the meshes the rotary uses the small engine's look.
bool write_definition_file() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_definition_file, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring game_dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring components = game_dir + L"\\rom\\meshes\\components\\";
    bool meshes = true;
    size_t vertices = 0;
    for (int tile = 0; tile < 3; ++tile) {
        Mesh m = build_tile(tile);
        vertices += m.v.size();
        std::string name = tile_name(tile);
        std::vector<uint8_t> file = encode_mesh(m, name);
        meshes = write_file(components + std::wstring(name.begin(), name.end()) + L".mesh", file.data(), file.size()) && meshes;
    }
    if (!meshes) log(1, "could not write the rotary meshes into rom\\meshes\\components; the rotary uses the small engine's look");
    else log(0, "rotary meshes written (%zu vertices in 3 tiles)", vertices);
    dir += L"\\Rotary";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\rotary_engine.json";
    std::string json = definition_json(meshes ? mesh_path() : std::string("meshes/components/engine_block_a_0_0_0.mesh"));
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    g_json_forward = g_json_path;
    for (char& c : g_json_forward) if (c == '\\') c = '/';
    return ok && !g_json_path.empty();
}

bool already_added(void* container) {
    if (!g_get_definition || !g_string_ctor) return false;
    anymaker::gc_string_view s{};
    g_string_ctor(&s, kDefinitionId);
    const void* found = nullptr;
    g_get_definition(&found, container, &s);
    return found != nullptr;
}

// Loads our JSON into a fresh vehicle_component_definition_file and hands it to the container. The path's
// characters must outlive the call ($string_ctor_cstr makes a non-owning view): they are g_json_path or
// g_json_forward, which never change once the Rotary is ready, so the main and server threads can both add. Other
// mods' hooks (GpsPart, Turbo) may sit under ours on this cell; calling down through them is fine because every mod
// checks whether its parts are already in the container before adding them.
bool add_ours(void* container, const std::string& chars) {
    GamePath path{kStoreSystem, 0, {}};
    g_string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};
    g_file_ctor(file);
    bool ok = false;
    g_file_load(&ok, file, &path);
    if (ok) reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    g_file_dtor(file);
    return ok;
}

thread_local bool t_adding = false;
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!t_adding) { std::lock_guard<std::recursive_mutex> lock(g_mutex); g_is_rotary.clear(); }   // definitions may move
    if (!g_ready.load() || t_adding || !container || already_added(container)) return;
    t_adding = true;
    bool ok = add_ours(container, g_json_path) || add_ours(container, g_json_forward);
    t_adding = false;
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(ok ? 0 : 2, ok ? "Rotary Engine added to the game's parts (%s)" : "could not load the Rotary definition from %s", g_json_path.c_str());
}

// ------------------------------------------------------------------------------------- sound (client, main thread)
namespace audio {
constexpr uint32_t play_voice_rva = 0x13f240;       // (effect_3d) application.audio.play_voice (s32 group, vec3, vec3, f64)
constexpr uint32_t append_rva = 0x13f9c0;           // effect_3d.append_audio_data (takes the buffer's memory)
constexpr uint32_t set_position_velocity_rva = 0x13f9a0;
constexpr uint32_t stop_rva = 0x13f970;
constexpr uint32_t reset_rva = 0x13f8b0;            // effect_3d.reset (releases the handle)
constexpr uint32_t buffer_reset_rva = 0x41a00;      // binary.buffer.reset (allocates with the game's allocator)
constexpr int32_t kGroupVehicle = 4;                // e_audio_group.vehicle: follows the vehicle volume slider
constexpr double kRange = 140.0;                    // metres
constexpr double kLead = 0.08;                      // seconds of sound queued ahead
// The voice object (what play_voice hands back) keeps the PCM still to play: a byte buffer whose size is at +0x78 and
// a read cursor in samples at +0x80 (effect_3d.append_audio_data's handler and the mixer, game.exe 0xe85f0/0xe86e0).
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
using engine_tick_t = void (*)(void* self, void* client, void* vehicle, void* scene, const double* dt);
using vol_speed_t = void (*)(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos);
play_voice_t g_play_voice;
effect_fn_t g_effect_stop, g_effect_reset;
effect_append_t g_effect_append;
effect_place_t g_effect_place;
buffer_reset_t g_buffer_reset;
world_transform_t g_world_transform;
anymaker::cell_hook h_client_tick, h_vol_speed;
bool g_voices_hooked = false;                        // the voice update hook is in: the game's own voice says when it runs

struct ClientEngine {
    void* effect = nullptr;                          // application.audio.effect_3d (a handle)
    Synth synth;
    double start = 0, last_tick = 0, retry_at = 0;
    long long written = 0;                           // samples queued since start
    bool rate_logged = false;
    double position[3]{}, velocity[3]{};
    bool placed = false;
    double game_volume = 0;                          // the stock power voice's volume this tick (> 0 while it runs)
    bool game_volume_seen = false;
    int logged_rotors = 0;
    double quiet_since = -1;                         // when it went silent (now_seconds), -1 while audible
};
std::unordered_map<const void*, ClientEngine> g_engines;   // keyed by client component
std::vector<int16_t> g_pcm;
double g_last_sweep = 0;
thread_local uint8_t* t_rotary;                       // rotary whose client tick is running on this thread

void release_voice(ClientEngine& e) {
    if (!e.effect) return;
    g_effect_stop(&e.effect);
    g_effect_reset(&e.effect);
    e.effect = nullptr;
}

// The rotary's stock running and power voices are silenced (a zero-volume update ends a game voice); knock while
// damaged and leak pass through. The power voice's volume is kept: it says the engine is firing.
void hk_vol_speed(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos) {
    auto original = reinterpret_cast<vol_speed_t>(h_vol_speed.original);
    uint8_t* e = t_rotary;
    if (!e || !single || !volume) return original(single, volume, speed, effect, group, pos);
    ptrdiff_t o = single - e;
    if (o != off::voice_operate && o != off::voice_power && o != off::voice_knock) return original(single, volume, speed, effect, group, pos);
    if (o == off::voice_power) {
        auto found = g_engines.find(e);
        if (found != g_engines.end()) { found->second.game_volume = *volume; found->second.game_volume_seen = true; }
    }
    if (o == off::voice_knock && at<uint8_t>(e, off::client_is_damage)) return original(single, volume, speed, effect, group, pos);
    double zero = 0;
    original(single, &zero, speed, effect, group, pos);
}

// Samples the voice still has to play, or -1 when its fields don't look right.
long long voice_queued(void* effect) {
    auto* base = static_cast<uint8_t*>(effect);
    if (!effect || !anymaker::readable(base + audio::kVoiceBytes, 0x10)) return -1;
    int32_t bytes = at<int32_t>(effect, audio::kVoiceBytes), cursor = at<int32_t>(effect, audio::kVoiceCursor);
    if (bytes < 0 || cursor < 0 || bytes > 64 * kSampleRate) return -1;
    return std::max<long long>(0, bytes / 2 - cursor);    // the audio thread may be mid-update: never negative
}

void stream_sound(void* self, const double* dt) {
    double now = now_seconds();
    ClientEngine& v = g_engines[self];
    v.last_tick = now;
    Input in;
    in.rps = factor_to_rps(at<double>(self, off::client_factor_speed));
    in.load = at<double>(self, off::client_factor_power);
    in.rotors = rotors_from_extend(at<int32_t>(self, off::client_extend_z));
    in.port = port_from_index(at<int32_t>(self, off::client_sound_index));
    // Firing: the game's power voice is audible. Without the voice hook, assume it fires at start speed or above.
    in.running = g_voices_hooked && v.game_volume_seen ? v.game_volume > 0.001 : in.rps >= kStartRps;
    v.game_volume_seen = false;
    if (v.logged_rotors != in.rotors) {
        static std::atomic<int> logs{12};
        if (logs.fetch_sub(1) > 0) log(0, "rotary sound: %d rotor%s (extend z %d), %s", in.rotors, in.rotors == 1 ? "" : "s", at<int32_t>(self, off::client_extend_z), port_name(in.port));
        v.logged_rotors = in.rotors;
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
    else if (v.effect) {                             // a parked rotary hands its voice back after 2 s of silence
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
            if (logs.fetch_sub(1) > 0) log(1, "the game gave no voice for a rotary's sound; retrying");
            return;
        }
        static std::atomic<int> logs{4};
        if (logs.fetch_sub(1) > 0) log(0, "rotary voice started (%.1f rps, %s)", in.rps, in.running ? "firing" : "cranking");
        v.start = now; v.written = 0;
    }
    g_effect_place(&v.effect, v.position, v.velocity);
    // Keep about kLead seconds queued. The voice's own queue says how much is still to play: the game can play it
    // slower than real time or start late, and counting by the clock alone let a backlog build up until the sound
    // ran seconds behind the engine. Without it, count by the clock; after a stall the queue ran dry: start again.
    long long played = (long long)((now - v.start) * kSampleRate);
    long long queued = voice_queued(v.effect);
    if (queued >= 0) played = v.written - queued;
    if (v.written < played) v.written = played;
    if (!v.rate_logged && queued >= 0 && now - v.start > 10.0) {
        v.rate_logged = true;
        log(0, "rotary sound plays %.0f samples/s, %.0f ms queued", double(v.written - queued) / (now - v.start), queued * 1000.0 / kSampleRate);
    }
    long long want = played + (long long)(audio::kLead * kSampleRate) - v.written;
    if (want <= 0) return;
    int n = int(std::min<long long>(want, kSampleRate / 5));
    g_pcm.resize(size_t(n));
    render(v.synth, in, g_pcm.data(), n);
    GameBuffer buffer{nullptr, 0, 0};
    int32_t bytes = n * 2;
    g_buffer_reset(&buffer, &bytes);
    if (!buffer.data || buffer.size != bytes) return;
    std::memcpy(buffer.data, g_pcm.data(), size_t(bytes));
    g_effect_append(&v.effect, &buffer);
    if (buffer.data) { int32_t zero = 0; g_buffer_reset(&buffer, &zero); }   // the effect refused it (queue full)
    v.written += n;
}

void hk_client_tick(void* self, void* client, void* vehicle, void* scene, const double* dt) {
    auto original = reinterpret_cast<engine_tick_t>(h_client_tick.original);
    if (!self || !rotary_definition(definition_at(self, off::client_definition))) return original(self, client, vehicle, scene, dt);
    uint8_t* prev = t_rotary;
    t_rotary = static_cast<uint8_t*>(self);
    g_engines[self];                                 // so the voice hook can record this tick's power volume
    original(self, client, vehicle, scene, dt);
    t_rotary = prev;
    stream_sound(self, dt);
    double now = now_seconds();
    if (now - g_last_sweep > 1.0) {                  // engines gone from the scene stop ticking: free their voices
        g_last_sweep = now;
        for (auto it = g_engines.begin(); it != g_engines.end();)
            if (now - it->second.last_tick > 1.0) { release_voice(it->second); it = g_engines.erase(it); }
            else ++it;
    }
}

// ------------------------------------------------------------------------------------- install (background thread)
bool executable(const void* p) {
    MEMORY_BASIC_INFORMATION r{};
    const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return p && VirtualQuery(p, &r, sizeof(r)) && r.State == MEM_COMMIT && (r.Protect & x) && !(r.Protect & PAGE_GUARD);
}
// The call cell of a binding reached through a call route. The SDK's lookup refuses when the route's root code
// signature matches more than once, which happened on the laptop for the definition functions (rooted at
// client_scene.create): some sessions have a second copy of that code in memory. Here every match is followed and
// the cell is taken when all the matches that lead to a live function agree on it.
int g_route_roots = -1;
void** route_cell(const anymaker::func_desc& d) {
    if (!anymaker::experimental::matching_build()) return nullptr;
    if (void** cell = anymaker::cell_of(d)) return cell;
    const anymaker::slot_route& r = d.call_route;
    if (!r.nhops || r.nhops > 8 || !r.root_sig || !*r.root_sig) return nullptr;
    uint8_t* roots[8];
    size_t n = anymaker::find_gcl_functions(r.root_sig, roots, 8);
    g_route_roots = int(n);
    void** found = nullptr;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* fn = roots[i];
        void** cell = nullptr;
        for (uint8_t h = 0; fn && h < r.nhops; ++h) {
            cell = reinterpret_cast<void**>(anymaker::read_slot(fn, r.hops[h]));
            fn = anymaker::readable(cell, 8) ? reinterpret_cast<uint8_t*>(*cell) : nullptr;
        }
        if (!fn || !executable(fn)) continue;
        if (found && found != cell) return nullptr;   // the copies disagree: refuse rather than guess
        found = cell;
    }
    return found;
}

// Sound is optional: without it the rotary sounds like the stock engine.
void install_sound() {
    using namespace anymaker::experimental;
    void** tick = hook_cell(bind::client_engine_tick);
    void** vol = hook_cell(bind::effect_update_play_volume_speed);
    g_world_transform = reinterpret_cast<world_transform_t>(function(bind::client_get_world_transform));
    g_play_voice = reinterpret_cast<play_voice_t>(native(audio::play_voice_rva));
    g_effect_append = reinterpret_cast<effect_append_t>(native(audio::append_rva));
    g_effect_place = reinterpret_cast<effect_place_t>(native(audio::set_position_velocity_rva));
    g_effect_stop = reinterpret_cast<effect_fn_t>(native(audio::stop_rva));
    g_effect_reset = reinterpret_cast<effect_fn_t>(native(audio::reset_rva));
    g_buffer_reset = reinterpret_cast<buffer_reset_t>(native(audio::buffer_reset_rva));
    bool found = tick && vol && g_world_transform && g_play_voice && g_effect_append && g_effect_place && g_effect_stop && g_effect_reset && g_buffer_reset;
    if (!found) {
        log(1, "rotary sound unavailable (client tick %d, voice update %d, transform %d, audio %d/%d/%d/%d/%d/%d); rotaries keep the stock engine sound",
            !!tick, !!vol, !!g_world_transform, !!g_play_voice, !!g_effect_append, !!g_effect_place, !!g_effect_stop, !!g_effect_reset, !!g_buffer_reset);
        return;
    }
    // The voice hook goes in first: once the tick hook is in, rotaries stream their own sound and need the stock
    // voices silenced.
    g_voices_hooked = h_vol_speed.install(vol, (void*)&hk_vol_speed);
    if (!g_voices_hooked) { log(1, "voice update hook not installed; rotaries keep the stock engine sound"); return; }
    if (!h_client_tick.install(tick, (void*)&hk_client_tick)) {
        h_vol_speed.remove((void*)&hk_vol_speed);
        g_voices_hooked = false;
        log(1, "engine client tick hook not installed; rotaries keep the stock engine sound");
        return;
    }
    log(0, "rotary sound on");
}

// Identity checks for the definition functions, from their first code bytes (Anymaker 0.1.24 and 0.1.25 alike).
// Laptop round 3 (2026-10-13): one session's hook landed on vehicle_component_definition_container.load instead of
// _add_definitions, so the game was handed Rotary's definition file as a file path and crashed. Before hooking, the
// cells are now checked: the add cell must hold _add_definitions (or another mod's hook on it, which lives in a mod
// DLL), never container.load, and the load cell must hold vehicle_component_definition_file.load.
constexpr const char* kSigAddDefinitions = "53 48 81 ec 90 00 00 00 48 89 4c 24 48 48 89 54 24 50 48 8b 44 24 28 48 89 44 24 58";
constexpr const char* kSigContainerLoad = "53 48 81 ec a0 00 00 00 48 89 4c 24 40 48 89 54 24 48 48 8d 01 48 8b 1d";
constexpr const char* kSigFileLoad = "53 55 48 81 ec 98 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58";
bool code_is(const void* fn, const char* sig) {
    anymaker::pattern pat(sig);
    return fn && !pat.bytes.empty() && anymaker::readable(fn, pat.bytes.size()) && pat.match(static_cast<const uint8_t*>(fn));
}
// A function inside a loaded DLL other than the game: another mod's hook sitting on the cell.
bool in_other_module(const void* fn) {
    HMODULE m = nullptr;
    return fn && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m) &&
           m && m != GetModuleHandleW(nullptr);
}
bool add_cell_ok(void** cell) {
    void* fn = cell && anymaker::readable(cell, 8) ? *cell : nullptr;
    if (!fn || code_is(fn, kSigContainerLoad) || code_is(fn, kSigFileLoad)) return false;
    return code_is(fn, kSigAddDefinitions) || in_other_module(fn);
}
bool load_fn_ok(void* fn) { return code_is(fn, kSigFileLoad); }
const char* what_is(const void* fn) {
    return !fn ? "nothing" : code_is(fn, kSigAddDefinitions) ? "_add_definitions" : code_is(fn, kSigContainerLoad) ? "container.load"
         : code_is(fn, kSigFileLoad) ? "file.load" : in_other_module(fn) ? "another mod's hook" : "unknown code";
}

void install_hooks() {
    using namespace anymaker::experimental;
    // Each lookup scans the game's code, so what is found is kept and only the rest is looked up again; after the
    // first 30 s the retries slow to every 5 s. Nothing is hooked until everything is found.
    void **add = nullptr, **create = nullptr, **load_cell = nullptr;
    self_fn_t fctor = nullptr, fdtor = nullptr;
    file_load_t fload = nullptr;
    string_ctor_t sc = nullptr;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(attempt < 30 ? 1000 : 5000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; Rotary stays off"); return; }
        if (!add) add = route_cell(bind::definitions_add_definitions);
        if (!create) create = hook_cell(bind::engine_create);
        if (!fctor) fctor = reinterpret_cast<self_fn_t>(function(bind::definition_file_ctor));
        if (!fdtor) fdtor = reinterpret_cast<self_fn_t>(function(bind::definition_file_dtor));
        if (!load_cell) load_cell = route_cell(bind::definition_file_load);
        if (!fload && load_cell) fload = reinterpret_cast<file_load_t>(*load_cell);
        if (add && !add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", what_is(*add));
            add = nullptr;
        }
        if (fload && !load_fn_ok((void*)fload)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the file load route led to %s, not vehicle_component_definition_file.load; looking again", what_is((void*)fload));
            fload = nullptr; load_cell = nullptr;
        }
        if (!sc) sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        bool core = add && create && fctor && fdtor && fload && sc;
        if (!core) {
            if (attempt == 179)
                log(2, "could not locate the game functions (add=%d create=%d file=%d/%d/%d string=%d, definition route roots %d)", !!add, !!create, !!fctor, !!fdtor, !!fload, !!sc, g_route_roots);
            continue;
        }
        std::lock_guard install_lock(engine_parts::install_mutex());   // one AnyEngineParts part hooks at a time
        g_file_ctor = fctor; g_file_dtor = fdtor; g_file_load = fload; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(function(bind::definitions_get_by_id));
        if (!g_get_definition) log(1, "definition lookup not found; Rotary may add its part twice if another mod re-adds definitions");
        if (!write_definition_file()) { log(2, "could not write the Rotary definition file; Rotary is off"); return; }
        if (!h_create.install(create, (void*)&hk_create))
            log(1, "engine create hook not installed; rotaries make the small engine's power");
        if (!add_cell_ok(add)) { log(2, "the definitions hook cell changed to %s before hooking; Rotary is off", what_is(*add)); h_create.remove((void*)&hk_create); return; }
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) { log(2, "the definitions hook cell was already taken or not writable; Rotary is off"); h_create.remove((void*)&hk_create); return; }
        install_sound();
        g_ready = true;
        if (g_route_roots > 1) log(0, "the definition functions' route root matched %d times; all matches agreed", g_route_roots);
        log(0, "Ready after %d s. Load or start a world to add the Rotary Engine (definition %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_client_tick.remove((void*)&hk_client_tick);
    h_vol_speed.remove((void*)&hk_vol_speed);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_create.remove((void*)&hk_create);
}
}  // namespace

// AnyEngineParts (engine_parts_mod.cpp) is the DLL; it starts this part of it.
void init(const AnyModHostV1& h) { host = h; }
void start() { install_hooks(); }       // on AnyEngineParts' install thread
void shutdown() { g_stop = true; remove_hooks(); }
}  // namespace rotary
