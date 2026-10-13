// CustomGame: adds a "Custom Game" button to the New Game screen. It opens a settings page (game mode, day cycle
// speed, start time, starting loot tier, max players, survival, danger and world settings); Start game begins a
// normal Career, Creative or Sandbox world with those settings applied and remembered for that world.
//
// How:
// - The button and page are drawn with anyapi.gpu_draw over the game's New Game screen, which this mod notices by
//   hooking frontend_ui._update_ui_new_game. Start game calls the game's own frontend_ui._start_new_game with the
//   chosen mode from inside that hook, so the world starts exactly like the matching native button.
// - server_scene._generate_new_game binds the settings to the new world's seed and saves them in
//   <mods>/CustomGame/worlds/<seed>.cfg. Whenever the host runs a world (server_scene.tick_loading / tick), the file
//   for its seed is applied, or the game's defaults are restored when there is none.
// - Most settings are the game's own tuning globals (hunger seconds, AI sight distance, loot capacity and so on),
//   scaled from the values read at start-up. They run on the host, so only the host needs the mod for those.
// - Day cycle and start time drive the server time of day's replicated override (m_is_override and
//   m_cycle_factor_override), which every client, modded or not, already obeys. The override value also carries the
//   weather lock and rain density in its lowest bits (customgame_logic.h), so joining players with CustomGame see
//   the same locked weather. Weather is locked by hooking weather_manager.get_weather_data.
// - Starting loot tier calls server_scene.set_loot_level once, on the first tick of a new world, the way finishing a
//   bunker does.
//
// EXPERIMENTAL. Uses the experimental SDK (customgame_bindings.h): Anymaker 0.1.24 / Steam build 25826614 only.
// First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_experimental.hpp"
#include "customgame_bindings.h"
#include "customgame_logic.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace customgame {
namespace {
constexpr const char* kModId = "customgame";
constexpr int kPresetCount = 3;
constexpr ULONGLONG kPendingLifetimeMs = 180000;   // a Start game click must reach world generation within this

// Field offsets (0.1.24 reference layouts, metadata; server_scene tick verified from server.tick's increment).
namespace off {
constexpr ptrdiff_t scene_tick = 0x40;                     // s32, +1 per server tick (reference label: m_version)
constexpr ptrdiff_t scene_seed = 0x48 + 0x18;              // m_world_seed: master property_s32, _m_value
constexpr ptrdiff_t scene_gamemode = 0x88 + 0x18;          // m_gamemode: master property_enum, _m_value
constexpr ptrdiff_t scene_time_of_day = 0xb10;             // server_scene.time_of_day
constexpr ptrdiff_t tod_is_override = 0x18;                // master property_bool
constexpr ptrdiff_t tod_override_value = 0x38;             // master property_f64
constexpr ptrdiff_t server_max_players = 928;              // server.m_settings._m_max_player_count (s32)
constexpr ptrdiff_t client_tod = 8552;                     // client_scene.m_time_of_day
constexpr ptrdiff_t client_tod_is_override = 8 + 8;        // slave property_bool _m_value
constexpr ptrdiff_t client_tod_override_value = 24 + 8;    // slave property_f64 _m_value
}  // namespace off

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
std::atomic<bool> g_stop{false}, g_ready{false};
std::filesystem::path g_dir;   // <mods>/CustomGame

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }

// ------------------------------------------------------------------ files
bool read_text(const std::filesystem::path& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < (1 << 20);
    out.assign(ok ? size_t(size.QuadPart) : 0, '\0');
    DWORD got = 0;
    ok = ok && (out.empty() || (ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size()));
    CloseHandle(h);
    return ok;
}
bool write_text(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path; tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size();
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
std::filesystem::path world_path(int32_t seed) { return g_dir / L"worlds" / (std::to_wstring(seed) + L".cfg"); }
std::filesystem::path preset_path(int slot) { return g_dir / (L"preset" + std::to_wstring(slot + 1) + L".cfg"); }
std::filesystem::path last_path() { return g_dir / L"last.cfg"; }
bool load_config(const std::filesystem::path& path, WorldFile& w) { std::string t; return read_text(path, t) && parse(t, w); }

// ------------------------------------------------------------------ game globals (defaults read once at start-up)
enum F64Global {
    gHunger, gThirst, gStaminaDrain, gStaminaRecover, gOxygenDeplete, gOxygenDrown, gRainSoak, gSwimSoak, gDryWarm,
    gDryCold, gCorpse, gLostBody, gTempMin, gTempMax, gSurvivorSight, gSurvivorHearing, gCreatureSight,
    gCreatureHearing, gZombieSight, gZombieHearing, gSpawnExclusion, gLoot, gJoints, gSandboxFactor, gDeltaTime, kF64Count
};
enum S32Global { gDayTicks, gStartOffset, gMaxPlayers, gHorde, gMaxSpawned, gRainParticles, gSnowParticles, kS32Count };
enum BoolGlobal { gNoCreatures, gNoZombies, kBoolCount };
template <class T> struct Global { T* p = nullptr; T def{}; const char* name = ""; };
Global<double> g_f64[kF64Count];
Global<int32_t> g_s32[kS32Count];
Global<bool> g_bool[kBoolCount];
std::atomic<bool> g_globals_custom{false};

template <class T> void set(Global<T>& g, T v) { if (g.p) *g.p = v; }
template <class T> void reset(Global<T>& g) { if (g.p) *g.p = g.def; }

// Server-side tuning. Called with nullptr to restore every default.
void apply_globals(const Config* c) {
    if (!c) {
        if (!g_globals_custom.exchange(false)) return;
        for (auto& g : g_f64) if (&g != &g_f64[gSandboxFactor] && &g != &g_f64[gDeltaTime]) reset(g);
        reset(g_s32[gHorde]); reset(g_s32[gMaxSpawned]);
        for (auto& g : g_bool) reset(g);
        log(0, "game defaults restored");
        return;
    }
    const Config& v = *c;
    auto dur = [&](F64Global g, double pct) { if (g_f64[g].p) *g_f64[g].p = scaled_duration(g_f64[g].def, pct); };
    auto mul = [&](F64Global g, double pct) { if (g_f64[g].p) *g_f64[g].p = scaled(g_f64[g].def, pct); };
    dur(gHunger, v[kHunger]);
    dur(gThirst, v[kThirst]);
    dur(gStaminaDrain, v[kStaminaDrain]);
    dur(gStaminaRecover, v[kStaminaRecover]);
    mul(gOxygenDeplete, v[kBreath]);
    mul(gOxygenDrown, v[kBreath]);
    dur(gRainSoak, v[kSoak]);
    dur(gSwimSoak, v[kSoak]);
    dur(gDryWarm, v[kDry]);
    dur(gDryCold, v[kDry]);
    mul(gCorpse, v[kCorpseTime]);
    mul(gLostBody, v[kLostBodyTime]);
    if (g_f64[gTempMin].p && g_f64[gTempMax].p) {
        double lo = g_f64[gTempMin].def + v[kColdest], hi = g_f64[gTempMax].def + v[kHottest];
        if (hi < lo + 1) hi = lo + 1;   // keep the range the right way round
        *g_f64[gTempMin].p = lo; *g_f64[gTempMax].p = hi;
    }
    for (F64Global g : {gSurvivorSight, gCreatureSight, gZombieSight}) mul(g, v[kSight]);
    for (F64Global g : {gSurvivorHearing, gCreatureHearing, gZombieHearing}) mul(g, v[kHearing]);
    mul(gSpawnExclusion, v[kSafeRadius]);
    if (g_f64[gLoot].p) *g_f64[gLoot].p = loot_capacity(g_f64[gLoot].def, v[kLoot]);
    if (g_f64[gJoints].p) *g_f64[gJoints].p = v[kJoints] >= kUnbreakable ? 1e15 : scaled(g_f64[gJoints].def, v[kJoints]);
    set(g_s32[gHorde], scaled_count(g_s32[gHorde].def, v[kHordeSize], 0));
    set(g_s32[gMaxSpawned], scaled_count(g_s32[gMaxSpawned].def, v[kMaxEnemies], 1));
    set(g_bool[gNoZombies], enemies_on(v, kZombies) ? g_bool[gNoZombies].def : true);
    set(g_bool[gNoCreatures], enemies_on(v, kCreatures) ? g_bool[gNoCreatures].def : true);
    g_globals_custom = true;
    // One line per changed global, so a test log shows exactly what the world runs with.
    std::string changed;
    char item[128];
    auto note = [&](const char* name, double from, double to) {
        if (from == to) return;
        snprintf(item, sizeof item, "%s%s %g -> %g", changed.empty() ? "" : ", ", name, from, to);
        changed += item;
    };
    for (auto& g : g_f64) if (g.p) note(g.name, g.def, *g.p);
    for (auto& g : g_s32) if (g.p) note(g.name, g.def, *g.p);
    for (auto& g : g_bool) if (g.p) note(g.name, g.def, *g.p);
    log(0, "applied: %s", changed.empty() ? "no game values changed" : changed.c_str());
}

GameDefaults game_defaults() {
    GameDefaults d;
    if (g_f64[gCorpse].p) d.corpse_seconds = g_f64[gCorpse].def;
    if (g_f64[gLostBody].p) d.lost_body_seconds = g_f64[gLostBody].def;
    if (g_f64[gTempMin].p) d.temperature_min = g_f64[gTempMin].def;
    if (g_f64[gTempMax].p) d.temperature_max = g_f64[gTempMax].def;
    if (g_s32[gDayTicks].p && g_f64[gDeltaTime].p) {
        double minutes = g_s32[gDayTicks].def * g_f64[gDeltaTime].def / 60.0;
        if (minutes > 1 && minutes < 100000) d.day_minutes = minutes;
    }
    return d;
}

// ------------------------------------------------------------------ resolved game functions
using ui_new_game_t = void (*)(void* fui, const double* dt, void* settings, void* ui_data);
using start_new_game_t = void (*)(void* fui, void* ui_data, const int32_t* mode);
using scene_fn_t = void (*)(uint8_t* scene);
using scene_tick_t = void (*)(uint8_t* scene, uint8_t* server);
using set_loot_level_t = void (*)(uint8_t* scene, const int32_t* level);
using prop_bool_set_t = void (*)(void* prop, const bool* value);
using prop_f64_set_t = void (*)(void* prop, const double* value);
using client_tick_t = void (*)(uint8_t* scene, void* client, const double* a, const double* b);
using weather_t = void (*)(WeatherData* ret, void* manager, const double* position, const double* time);
using particles_create_t = void (*)(void* particles);

start_new_game_t fn_start_new_game;
set_loot_level_t fn_set_loot_level;
prop_bool_set_t fn_prop_bool_set;
prop_f64_set_t fn_prop_f64_set;
anymaker::cell_hook h_ui, h_generate, h_tick, h_tick_loading, h_client_tick, h_weather, h_particles;
anymaker::cell_hook h_tick_spawn, h_spawn_zones, h_sleeping, h_destroy_horde, h_spawn_horde;

// ------------------------------------------------------------------ menu -> world hand-off
std::mutex g_pending_mutex;
Config g_pending;
ULONGLONG g_pending_at = 0;       // 0: nothing pending
std::atomic<int> g_start_request{-1};
std::atomic<ULONGLONG> g_new_game_seen{0};

void hk_update_ui_new_game(void* fui, const double* dt, void* settings, void* ui_data) {
    reinterpret_cast<ui_new_game_t>(h_ui.original)(fui, dt, settings, ui_data);
    g_new_game_seen = GetTickCount64();
    int mode = g_start_request.exchange(-1);
    if (mode >= 0 && fn_start_new_game && fui && ui_data) {
        int32_t m = mode;
        log(0, "starting a custom %s game", value_text(kMode, m, {}).c_str());
        fn_start_new_game(fui, ui_data, &m);
    }
}

// ------------------------------------------------------------------ host: worlds
struct ServerState {
    uint8_t* scene = nullptr;
    bool settled = false, active = false, tier_pending = false, players_set = false;
    int players_logged = 0;
    int waits = 0;
    WorldFile world;
};
std::mutex g_server_mutex;
ServerState g_srv;
uint8_t* g_generated_scene = nullptr;   // set by _generate_new_game for the scene it built
bool g_generated_custom = false;
WorldFile g_generated_world;
std::atomic<int> g_host_weather{-1};    // weather lock from the host's own config, -1 when not hosting one
std::atomic<int> g_host_precipitation{-1};
std::atomic<ULONGLONG> g_host_seen{0};  // last server tick; a stale host config must not leak into a joined world
std::atomic<bool> g_sandbox_spawns{false};   // the host's world forces zombies or creatures on in Sandbox

void hk_generate_new_game(uint8_t* scene) {
    auto original = reinterpret_cast<scene_fn_t>(h_generate.original);
    bool custom = false;
    Config cfg;
    {
        std::lock_guard lock(g_pending_mutex);
        if (g_pending_at && GetTickCount64() - g_pending_at < kPendingLifetimeMs) { custom = true; cfg = g_pending; }
        g_pending_at = 0;
    }
    apply_globals(custom ? &cfg : nullptr);   // world generation already spawns loot and enemies
    g_sandbox_spawns = custom && sandbox_spawns(cfg);
    original(scene);
    if (!scene) return;
    int32_t seed = rd<int32_t>(scene, off::scene_seed);
    std::lock_guard lock(g_server_mutex);
    g_generated_scene = scene;
    g_generated_custom = custom;
    g_generated_world = WorldFile{};
    if (custom) {
        g_generated_world.config = cfg;
        g_generated_world.seed = seed;
        bool saved = write_text(world_path(seed), serialize(g_generated_world, true));
        log(saved ? 0 : 2, "new custom world, seed %d: settings %s %ls", seed, saved ? "saved to" : "could NOT be saved to",
            world_path(seed).c_str());
    } else {
        std::error_code ec;
        if (std::filesystem::remove(world_path(seed), ec)) log(0, "new normal world reuses seed %d: removed its old CustomGame file", seed);
    }
}

void set_players(uint8_t* server, const Config* c) {
    if (!server) return;
    auto& cap = g_s32[gMaxPlayers];
    auto* count = reinterpret_cast<int32_t*>(server + off::server_max_players);
    if (c && !c->is_default(kMaxPlayers)) {
        int32_t want = int32_t((*c)[kMaxPlayers]);
        if (cap.p) *cap.p = std::max(cap.def, want);
        *count = want;
        log(0, "max players set to %d", want);
    } else {
        if (cap.p && *cap.p != cap.def) { *cap.p = cap.def; if (*count > cap.def) *count = cap.def; }
    }
}

void drive_time(uint8_t* scene, const Config& c) {
    if (!fn_prop_bool_set || !fn_prop_f64_set || !g_s32[gDayTicks].p) return;
    int32_t tick = rd<int32_t>(scene, off::scene_tick);
    double sandbox = g_f64[gSandboxFactor].p ? *g_f64[gSandboxFactor].p : 0.0;
    int32_t start = g_s32[gStartOffset].p ? *g_s32[gStartOffset].p : 0;
    double value = encode(day_factor(c, tick, *g_s32[gDayTicks].p, start, sandbox), pack(int(c[kWeather]), c[kPrecipitation]));
    uint8_t* tod = scene + off::scene_time_of_day;
    bool on = true;
    fn_prop_bool_set(tod + off::tod_is_override, &on);
    fn_prop_f64_set(tod + off::tod_override_value, &value);
}

void server_tick(uint8_t* scene, uint8_t* server, bool loading) {
    if (!scene || !g_ready) return;
    g_host_seen = GetTickCount64();
    std::lock_guard lock(g_server_mutex);
    ServerState& s = g_srv;
    if (scene != s.scene) { s = ServerState{}; s.scene = scene; }
    if (!s.settled) {
        if (scene == g_generated_scene) {
            s.active = g_generated_custom;
            s.world = g_generated_world;
        } else {
            int32_t seed = rd<int32_t>(scene, off::scene_seed);
            if (seed == 0 && ++s.waits < 600) return;   // the save may not be parsed yet
            WorldFile w;
            s.active = load_config(world_path(seed), w);
            if (s.active) s.world = w;
            log(0, "world seed %d: %s", seed, s.active ? "CustomGame settings found" : "no CustomGame settings, normal rules");
        }
        s.settled = true;
        s.tier_pending = s.active && s.world.config[kStartTier] > 1 && !s.world.tier_applied;
        apply_globals(s.active ? &s.world.config : nullptr);
        set_players(server, s.active ? &s.world.config : nullptr);
        g_sandbox_spawns = s.active && sandbox_spawns(s.world.config);
        g_host_weather = s.active ? int(s.world.config[kWeather]) : -1;
        g_host_precipitation = s.active ? int(s.world.config[kPrecipitation]) : -1;
        if (s.active) {
            int32_t mode = rd<int32_t>(scene, off::scene_gamemode);
            log(0, "custom world active: %s mode (world reports mode %d), %s", value_text(kMode, s.world.config[kMode], {}).c_str(),
                mode, summary(s.world.config).c_str());
        }
    }
    if (!s.active) return;
    // The multiplayer host screen writes its own player count when hosting starts (server.settings.update_settings),
    // so keep the world's choice in place; joins check this value (server.on_network_event request_join).
    if (server && !s.world.config.is_default(kMaxPlayers)) {
        auto* count = reinterpret_cast<int32_t*>(server + off::server_max_players);
        int32_t want = int32_t(s.world.config[kMaxPlayers]);
        if (*count != want) {
            if (s.players_logged < 5) { log(0, "max players was changed to %d by the game; set back to %d", int(*count), int(want)); ++s.players_logged; }
            if (g_s32[gMaxPlayers].p && *g_s32[gMaxPlayers].p < want) *g_s32[gMaxPlayers].p = want;
            *count = want;
        }
    }
    if (needs_time_override(s.world.config)) drive_time(scene, s.world.config);
    if (s.tier_pending && !loading && fn_set_loot_level) {
        int32_t level = int32_t(s.world.config[kStartTier]);
        fn_set_loot_level(scene, &level);
        s.tier_pending = false;
        s.world.tier_applied = true;
        write_text(world_path(s.world.seed ? s.world.seed : rd<int32_t>(scene, off::scene_seed)), serialize(s.world, true));
        log(0, "starting loot tier %d applied", int(level));
    }
}
void hk_server_tick(uint8_t* scene, uint8_t* server) {
    server_tick(scene, server, false);
    reinterpret_cast<scene_tick_t>(h_tick.original)(scene, server);
}
void hk_server_tick_loading(uint8_t* scene, uint8_t* server) {
    server_tick(scene, server, true);
    reinterpret_cast<scene_tick_t>(h_tick_loading.original)(scene, server);
}

// ------------------------------------------------------------------ host: zombies and creatures in Sandbox
// Sandbox turns its spawners off by checking the mode (creature_manager._tick_spawn, tile spawn zones, sleeping
// actors, hordes). When the world forces enemies on, those functions run with the scene's mode value reading Career;
// it is written raw (no replication) and put back before they return. All of them run on the server thread.
struct CareerScope {
    int32_t* mode = nullptr;
    explicit CareerScope(uint8_t* scene) {
        if (!g_sandbox_spawns || !scene) return;
        auto* m = reinterpret_cast<int32_t*>(scene + off::scene_gamemode);
        if (*m == 2) { mode = m; *m = 0; }
    }
    ~CareerScope() { if (mode) *mode = 2; }
};
using tick_spawn_t = void (*)(void* manager, uint8_t* scene, const void* positions, const double* dt);
using scene_arg_t = void (*)(void* self, uint8_t* scene);
using destroy_horde_t = void (*)(bool* ret, void* controller, uint8_t* scene);
void hk_tick_spawn(void* manager, uint8_t* scene, const void* positions, const double* dt) {
    CareerScope career(scene);
    reinterpret_cast<tick_spawn_t>(h_tick_spawn.original)(manager, scene, positions, dt);
}
void hk_spawn_zones(void* tile, uint8_t* scene) { CareerScope career(scene); reinterpret_cast<scene_arg_t>(h_spawn_zones.original)(tile, scene); }
void hk_sleeping(void* vehicle, uint8_t* scene) { CareerScope career(scene); reinterpret_cast<scene_arg_t>(h_sleeping.original)(vehicle, scene); }
void hk_spawn_horde(void* manager, uint8_t* scene) { CareerScope career(scene); reinterpret_cast<scene_arg_t>(h_spawn_horde.original)(manager, scene); }
void hk_destroy_horde(bool* ret, void* controller, uint8_t* scene) {
    CareerScope career(scene);
    reinterpret_cast<destroy_horde_t>(h_destroy_horde.original)(ret, controller, scene);
}

// ------------------------------------------------------------------ every player: weather lock and rain density
std::atomic<int> g_client_weather{-1}, g_client_precipitation{-1};
uint8_t* g_client_scene = nullptr;   // main thread only
int g_decode_hits = 0, g_decode_misses = 0;

bool hosting() { return GetTickCount64() - g_host_seen.load() < 3000; }
int effective_weather() { int h = hosting() ? g_host_weather.load() : -1; return h >= 0 ? h : g_client_weather.load(); }
int effective_precipitation() { int h = hosting() ? g_host_precipitation.load() : -1; return h >= 0 ? h : g_client_precipitation.load(); }

void hk_client_tick(uint8_t* scene, void* client, const double* a, const double* b) {
    if (scene && g_ready) {
        if (scene != g_client_scene) { g_client_scene = scene; g_decode_hits = g_decode_misses = 0; g_client_weather = -1; g_client_precipitation = -1; }
        bool override = rd<bool>(scene, off::client_tod + off::client_tod_is_override);
        double value = rd<double>(scene, off::client_tod + off::client_tod_override_value);
        int w = 0; double p = 100;
        if (override && decode(value, w, p)) {
            g_decode_misses = 0;
            if (++g_decode_hits == 3) {
                g_client_weather = w; g_client_precipitation = int(p);
                log(0, "host's CustomGame world: weather %s, rain and snow %d%%", kWeatherNames[w], int(p));
            }
        } else if (++g_decode_misses > 120 && g_decode_hits) {
            g_decode_hits = 0; g_client_weather = -1; g_client_precipitation = -1;
        }
        int pct = effective_precipitation();
        if (pct < 0) pct = 100;
        for (S32Global g : {gRainParticles, gSnowParticles})
            if (g_s32[g].p) *g_s32[g].p = std::min(g_s32[g].def, scaled_count(g_s32[g].def, pct, 0));
    }
    reinterpret_cast<client_tick_t>(h_client_tick.original)(scene, client, a, b);
}
void hk_get_weather_data(WeatherData* ret, void* manager, const double* position, const double* time) {
    reinterpret_cast<weather_t>(h_weather.original)(ret, manager, position, time);
    int w = effective_weather();
    if (ret && w > 0) lock_weather(*ret, w);
}
// The particle buffers are sized from these counts when created, so create them at full size; the lowered count
// only ever draws fewer.
void hk_particles_create(void* particles) {
    auto &rain = g_s32[gRainParticles], &snow = g_s32[gSnowParticles];
    int32_t r = rain.p ? *rain.p : 0, s = snow.p ? *snow.p : 0;
    reset(rain); reset(snow);
    reinterpret_cast<particles_create_t>(h_particles.original)(particles);
    if (rain.p) *rain.p = r;
    if (snow.p) *snow.p = s;
}

// ------------------------------------------------------------------ the page
struct Rect { float x, y, w, h; bool in(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
enum HitKind { kHitNone, kHitOpen, kHitTab, kHitMinus, kHitPlus, kHitValue, kHitBack, kHitReset, kHitStart, kHitSave, kHitLoad, kHitScroll };
struct Hit { Rect r; HitKind kind; int arg; };

std::mutex g_ui_mutex;   // render and input threads
bool g_open = false;
int g_tab = 0;           // 0..kCategoryCount-1, kCategoryCount = presets
float g_scroll = 0, g_scroll_max = 0;
Config g_edit;
std::vector<Hit> g_hits;
float g_mouse_x = -1, g_mouse_y = -1;
uint32_t g_frame_w = 1920, g_frame_h = 1080;
std::string g_toast;
ULONGLONG g_toast_until = 0;
bool g_preset_exists[kPresetCount];
std::string g_preset_summary[kPresetCount];

bool new_game_visible() { ULONGLONG seen = g_new_game_seen.load(); return seen && GetTickCount64() - seen < 300; }

void refresh_presets() {
    for (int i = 0; i < kPresetCount; ++i) {
        WorldFile w;
        g_preset_exists[i] = load_config(preset_path(i), w);
        g_preset_summary[i] = g_preset_exists[i] ? summary(w.config) : "Empty";
    }
}
void toast(const std::string& text) { g_toast = text; g_toast_until = GetTickCount64() + 2200; }

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Drawing helpers (render thread, inside the gpu_draw callback).
void rect(Rect r, uint32_t color, float radius = 0, float opacity = 1, bool stroke = false, float width = 1) {
    AnyGpuCommandV1 c; c.kind = radius > 0 ? ANY_GPU_ROUND_RECT : ANY_GPU_RECT;
    c.rect[0] = r.x; c.rect[1] = r.y; c.rect[2] = r.w; c.rect[3] = r.h; c.radius = radius; c.color = color; c.opacity = opacity;
    if (stroke) { c.flags = ANY_GPU_STROKE; c.stroke = width; }
    gpu->emit(&c);
}
void text(Rect r, const std::string& s, float size, uint32_t color, uint32_t flags = ANY_GPU_VCENTER | ANY_GPU_NOWRAP) {
    std::wstring w = wide(s);
    AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = r.x; c.rect[1] = r.y; c.rect[2] = r.w; c.rect[3] = r.h;
    c.font_size = size; c.color = color; c.text = w.c_str(); c.text_length = uint32_t(w.size()); c.flags = flags;
    gpu->emit(&c);
}
bool hovered(Rect r) { return r.in(g_mouse_x, g_mouse_y); }

constexpr uint32_t kBackdrop = 0xff0d1014, kPanel = 0xff171b21, kRow = 0xff1f252d, kRowHover = 0xff28303a,
                   kText = 0xfff2f4f7, kMuted = 0xff9aa4b2, kAccent = 0xffe8a33d, kAccentDark = 0xff3a2c14, kButton = 0xff2a313b;

void button(Rect r, const std::string& label, float s, HitKind kind, int arg, bool primary = false) {
    bool hot = hovered(r);
    rect(r, primary ? (hot ? 0xfff0b553 : kAccent) : (hot ? 0xff36404c : kButton), 6 * s);
    text(r, label, 17 * s, primary ? 0xff16120a : kText, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
    g_hits.push_back({r, kind, arg});
}

void draw_open_button(float W, float H, float s) {
    Rect r{(W - 380 * s) / 2, H - 150 * s, 380 * s, 66 * s};
    bool hot = hovered(r);
    rect(r, hot ? 0xff2b2416 : 0xee15181d, 8 * s);
    rect(r, kAccent, 8 * s, 1, true, 2 * s);
    text({r.x, r.y + 6 * s, r.w, 32 * s}, "Custom Game", 24 * s, hot ? kAccent : kText, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
    text({r.x, r.y + 36 * s, r.w, 22 * s}, "Pick your own world settings", 14 * s, kMuted, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP);
    g_hits.push_back({r, kHitOpen, 0});
}

void draw_page(float W, float H, float s) {
    rect({0, 0, W, H}, kBackdrop, 0, 0.97f);
    float pw = std::min(1180 * s, W - 60 * s), ph = std::min(860 * s, H - 60 * s);
    Rect panel{(W - pw) / 2, (H - ph) / 2, pw, ph};
    rect(panel, kPanel, 10 * s);
    float pad = 28 * s;
    text({panel.x + pad, panel.y + 18 * s, pw - 2 * pad, 44 * s}, "Custom Game", 32 * s, kText, ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
    text({panel.x + pad, panel.y + 60 * s, pw - 2 * pad, 24 * s},
         "Anything left on its default plays like the normal game. Settings are saved with the world.", 15 * s, kMuted);

    // Tabs
    float top = panel.y + 104 * s, tab_w = 210 * s, footer = 84 * s;
    for (int t = 0; t <= kCategoryCount; ++t) {
        Rect r{panel.x + pad, top + t * 54 * s, tab_w, 46 * s};
        bool sel = t == g_tab, hot = hovered(r);
        rect(r, sel ? kAccentDark : hot ? kRowHover : kRow, 6 * s);
        if (sel) rect({r.x, r.y, 4 * s, r.h}, kAccent);
        int changed = 0;
        if (t < kCategoryCount) for (int i = 0; i < kCount; ++i) changed += kSettings[i].category == t && i != kMode && !g_edit.is_default(i);
        std::string label = t < kCategoryCount ? kCategoryNames[t] : "Presets";
        if (changed) label += "  \xE2\x80\xA2 " + std::to_string(changed);
        text({r.x + 16 * s, r.y, r.w - 20 * s, r.h}, label, 18 * s, sel ? kAccent : kText, ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
        g_hits.push_back({r, kHitTab, t});
    }

    // Rows
    Rect list{panel.x + pad + tab_w + 20 * s, top, pw - 2 * pad - tab_w - 20 * s, panel.y + ph - footer - top - 12 * s};
    AnyGpuCommandV1 clip; clip.kind = ANY_GPU_CLIP_PUSH; clip.rect[0] = list.x; clip.rect[1] = list.y; clip.rect[2] = list.w; clip.rect[3] = list.h;
    gpu->emit(&clip);
    float row_h = 68 * s, gap = 8 * s, y = list.y - g_scroll, content = 0;
    GameDefaults defaults = game_defaults();
    if (g_tab < kCategoryCount) {
        for (int i = 0; i < kCount; ++i) {
            const Setting& st = kSettings[i];
            if (st.category != g_tab) continue;
            Rect r{list.x, y, list.w, row_h};
            if (r.y + r.h > list.y && r.y < list.y + list.h) {
                bool changed = i != kMode && !g_edit.is_default(i);
                rect(r, hovered(r) ? kRowHover : kRow, 6 * s);
                if (changed) rect({r.x, r.y, 4 * s, r.h}, kAccent);
                float stepper_w = std::min(360 * s, r.w * 0.45f);
                text({r.x + 18 * s, r.y + 8 * s, r.w - stepper_w - 30 * s, 28 * s}, st.label, 18 * s, kText, ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
                text({r.x + 18 * s, r.y + 36 * s, r.w - stepper_w - 30 * s, 24 * s}, st.help, 13.5f * s, kMuted, ANY_GPU_VCENTER | ANY_GPU_NOWRAP);
                Rect sr{r.x + r.w - stepper_w - 14 * s, r.y + (row_h - 42 * s) / 2, stepper_w, 42 * s};
                Rect minus{sr.x, sr.y, 42 * s, sr.h}, plus{sr.x + sr.w - 42 * s, sr.y, 42 * s, sr.h};
                Rect value{sr.x + 46 * s, sr.y, sr.w - 92 * s, sr.h};
                int index = value_index(i, g_edit[i]);
                bool can_down = index > 0, can_up = index < st.count - 1;
                rect(minus, hovered(minus) && can_down ? 0xff36404c : kButton, 6 * s, can_down ? 1 : 0.4f);
                rect(plus, hovered(plus) && can_up ? 0xff36404c : kButton, 6 * s, can_up ? 1 : 0.4f);
                text(minus, "\xE2\x97\x80", 16 * s, can_down ? kText : kMuted, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP);
                text(plus, "\xE2\x96\xB6", 16 * s, can_up ? kText : kMuted, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP);
                rect(value, 0xff12161b, 6 * s);
                text(value, value_text(i, g_edit[i], defaults), 17 * s, changed ? kAccent : kText,
                     ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
                g_hits.push_back({minus, kHitMinus, i});
                g_hits.push_back({plus, kHitPlus, i});
                g_hits.push_back({value, kHitValue, i});
            }
            y += row_h + gap; content += row_h + gap;
        }
    } else {
        for (int p = 0; p < kPresetCount; ++p) {
            Rect r{list.x, y, list.w, row_h};
            rect(r, kRow, 6 * s);
            text({r.x + 18 * s, r.y + 8 * s, r.w * 0.6f, 28 * s}, "Preset " + std::to_string(p + 1), 18 * s, kText, ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD);
            text({r.x + 18 * s, r.y + 36 * s, r.w * 0.6f, 24 * s}, g_preset_summary[p], 13.5f * s, kMuted);
            button({r.x + r.w - 270 * s, r.y + 13 * s, 120 * s, 42 * s}, "Save", s, kHitSave, p);
            if (g_preset_exists[p]) button({r.x + r.w - 140 * s, r.y + 13 * s, 120 * s, 42 * s}, "Load", s, kHitLoad, p);
            y += row_h + gap; content += row_h + gap;
        }
        text({list.x, y + 6 * s, list.w, 48 * s}, "Save keeps every setting on this page in a slot; Load puts them back.", 14 * s, kMuted);
        content += 60 * s;
    }
    AnyGpuCommandV1 pop; pop.kind = ANY_GPU_CLIP_POP; gpu->emit(&pop);
    g_scroll_max = std::max(0.f, content - list.h);
    g_scroll = std::clamp(g_scroll, 0.f, g_scroll_max);
    g_hits.push_back({list, kHitScroll, 0});

    // Footer
    float fy = panel.y + ph - footer + 14 * s;
    button({panel.x + pad, fy, 150 * s, 52 * s}, "Back", s, kHitBack, 0);
    button({panel.x + pad + 166 * s, fy, 190 * s, 52 * s}, "Reset to default", s, kHitReset, 0);
    std::string start = "Start " + value_text(kMode, g_edit[kMode], {}) + " game";
    button({panel.x + pw - pad - 300 * s, fy, 300 * s, 52 * s}, start, s, kHitStart, 0, true);
    if (!g_ready) text({panel.x + pad + 380 * s, fy, pw - 2 * pad - 700 * s, 52 * s},
                       "Game hooks not ready: settings may not apply (see anymaker_modding.log)", 14 * s, 0xffff7a6b, ANY_GPU_VCENTER);
    if (!g_toast.empty() && GetTickCount64() < g_toast_until)
        text({panel.x + pad + 380 * s, fy, pw - 2 * pad - 700 * s, 52 * s}, g_toast, 15 * s, kAccent, ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP);
}

void render(const AnyFrameV1* f, void*) {
    if (!f || !gpu) return;
    std::lock_guard lock(g_ui_mutex);
    g_frame_w = f->width; g_frame_h = f->height;
    g_hits.clear();
    bool visible = new_game_visible();
    if (!visible && g_open) { g_open = false; if (host.capture_input) host.capture_input(0); }
    if (!visible || !f->focused) return;
    float W = float(f->width), H = float(f->height), s = std::max(0.6f, H / 1080.f);
    if (g_open) draw_page(W, H, s);
    else draw_open_button(W, H, s);
}

void to_frame(int x, int y) {
    RECT r{}; HWND window = GetForegroundWindow(); DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && GetClientRect(window, &r) && r.right > 0 && r.bottom > 0) {
        g_mouse_x = float(x) * float(g_frame_w) / float(r.right);
        g_mouse_y = float(y) * float(g_frame_h) / float(r.bottom);
    } else { g_mouse_x = float(x); g_mouse_y = float(y); }
}

const Hit* hit_at() {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (it->kind != kHitScroll && it->r.in(g_mouse_x, g_mouse_y)) return &*it;
    return nullptr;
}

void open_page() {
    g_open = true; g_scroll = 0;
    WorldFile w;
    if (load_config(last_path(), w)) g_edit = w.config;
    refresh_presets();
    if (host.capture_input) host.capture_input(1);
    log(0, "Custom Game page opened");
}
void close_page() { g_open = false; if (host.capture_input) host.capture_input(0); }

void click(const Hit& h, int button) {
    switch (h.kind) {
    case kHitOpen: open_page(); break;
    case kHitTab: g_tab = h.arg; g_scroll = 0; if (g_tab == kCategoryCount) refresh_presets(); break;
    case kHitMinus: step(g_edit, h.arg, -1); break;
    case kHitPlus: step(g_edit, h.arg, +1); break;
    case kHitValue: step(g_edit, h.arg, button == 2 ? -1 : +1); break;
    case kHitBack: close_page(); break;
    case kHitReset: { int mode = g_edit.mode(); g_edit.reset(); g_edit.v[kMode] = mode; toast("Settings reset to the game's defaults"); break; }
    case kHitSave: {
        WorldFile w; w.config = g_edit;
        bool ok = write_text(preset_path(h.arg), serialize(w, false));
        refresh_presets();
        toast(ok ? "Saved to preset " + std::to_string(h.arg + 1) : "Could not save the preset");
        break;
    }
    case kHitLoad: {
        WorldFile w;
        if (load_config(preset_path(h.arg), w)) { g_edit = w.config; toast("Loaded preset " + std::to_string(h.arg + 1)); }
        break;
    }
    case kHitStart: {
        WorldFile w; w.config = g_edit;
        write_text(last_path(), serialize(w, false));
        {
            std::lock_guard lock(g_pending_mutex);
            g_pending = g_edit; g_pending_at = GetTickCount64();
        }
        g_start_request = g_edit.mode();
        log(0, "Start pressed: %s", summary(g_edit).c_str());
        close_page();
        break;
    }
    default: break;
    }
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e || !gpu) return 0;
    std::lock_guard lock(g_ui_mutex);
    if (e->kind == ANY_FOCUS_LOST) return 0;
    if (e->kind == ANY_MOUSE_MOVE || e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP) to_frame(e->x, e->y);
    bool visible = new_game_visible();
    if (!visible) return 0;
    if (!g_open) {
        if ((e->kind == ANY_MOUSE_DOWN || e->kind == ANY_MOUSE_UP) && e->button == 1) {
            const Hit* h = hit_at();
            if (h && h->kind == kHitOpen) { if (e->kind == ANY_MOUSE_UP) click(*h, 1); return 1; }
        }
        return 0;
    }
    // The page is modal: every key and click belongs to it.
    if (e->kind == ANY_KEY_DOWN && e->key == VK_ESCAPE) { close_page(); return 1; }
    if (e->kind == ANY_MOUSE_WHEEL) { g_scroll = std::clamp(g_scroll - float(e->wheel) / WHEEL_DELTA * 60.f * std::max(0.6f, g_frame_h / 1080.f), 0.f, g_scroll_max); return 1; }
    if (e->kind == ANY_MOUSE_UP && (e->button == 1 || e->button == 2)) { if (const Hit* h = hit_at()) click(*h, e->button); return 1; }
    return e->kind == ANY_MOUSE_MOVE ? 0 : 1;
}

// ------------------------------------------------------------------ install (background thread)
template <class T> bool resolve(Global<T>& g, const anymaker::global_desc& d) {
    auto* p = static_cast<T*>(anymaker::experimental::global(d));
    if (!p) { log(1, "global %s not located: its setting will have no effect", d.name); return false; }
    g.p = p; g.def = *p; g.name = d.name;
    return true;
}
template <class F> F function(const anymaker::func_desc& d) {
    auto p = reinterpret_cast<F>(anymaker::experimental::function(d));
    if (!p) log(1, "function not located: %s", d.sig);
    return p;
}
bool hook(anymaker::cell_hook& h, const anymaker::func_desc& d, void* replacement) {
    void** cell = anymaker::experimental::hook_cell(d);
    if (!cell) { log(1, "hook point not located: %s", d.sig); return false; }
    if (!h.install(cell, replacement)) { log(2, "hook cell busy or not writable: %s", d.sig); return false; }
    return true;
}

void install() {
    using namespace anymaker::experimental;
    namespace b = bind;
    bool found = false;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; CustomGame stays off"); return; }
        if (hook_cell(b::update_ui_new_game) && global(b::g_day_ticks)) { found = true; break; }
    }
    if (!found || g_stop) { log(2, "could not locate the New Game screen; CustomGame is off"); return; }

    resolve(g_s32[gDayTicks], b::g_day_ticks); resolve(g_s32[gStartOffset], b::g_time_start_offset);
    resolve(g_s32[gMaxPlayers], b::g_max_player_count); resolve(g_s32[gHorde], b::g_horde_size);
    resolve(g_s32[gMaxSpawned], b::g_max_spawned_actors); resolve(g_s32[gRainParticles], b::g_rain_particles);
    resolve(g_s32[gSnowParticles], b::g_snow_particles);
    resolve(g_bool[gNoCreatures], b::g_disable_creatures); resolve(g_bool[gNoZombies], b::g_disable_zombies);
    const anymaker::global_desc* f64s[kF64Count] = {
        &b::g_hunger_seconds, &b::g_thirst_seconds, &b::g_stamina_drain_seconds, &b::g_stamina_recover_seconds,
        &b::g_oxygen_deplete_seconds, &b::g_oxygen_drown_seconds, &b::g_rain_soak_seconds, &b::g_swim_soak_seconds,
        &b::g_dry_seconds_warm, &b::g_dry_seconds_cold, &b::g_corpse_seconds, &b::g_lost_body_seconds,
        &b::g_temperature_min, &b::g_temperature_max, &b::g_survivor_sight, &b::g_survivor_hearing, &b::g_creature_sight,
        &b::g_creature_hearing, &b::g_zombie_sight, &b::g_zombie_hearing, &b::g_spawn_exclusion, &b::g_loot_capacity,
        &b::g_joint_break_force, &b::g_sandbox_cycle_factor, &b::g_server_delta_time};
    for (int i = 0; i < kF64Count; ++i) {
        resolve(g_f64[i], *f64s[i]);
        if (g_f64[i].p && !std::isfinite(g_f64[i].def)) { log(1, "global %s has an unexpected value; ignored", f64s[i]->name); g_f64[i].p = nullptr; }
    }
    {
        char line[640];
        snprintf(line, sizeof line, "defaults: day %d ticks, start offset %d, tick %.4f s, max players %d, hunger %.0f s, thirst %.0f s, "
                 "corpse %.0f s, temperature %.1f..%.1f C, zombie sight %.1f, horde %d, max spawned %d, loot %.2f, joints %.1f",
                 int(g_s32[gDayTicks].def), int(g_s32[gStartOffset].def), g_f64[gDeltaTime].def, int(g_s32[gMaxPlayers].def),
                 g_f64[gHunger].def, g_f64[gThirst].def, g_f64[gCorpse].def, g_f64[gTempMin].def, g_f64[gTempMax].def,
                 g_f64[gZombieSight].def, int(g_s32[gHorde].def), int(g_s32[gMaxSpawned].def), g_f64[gLoot].def, g_f64[gJoints].def);
        log(0, "%s", line);
    }
    fn_start_new_game = function<start_new_game_t>(b::start_new_game);
    fn_set_loot_level = function<set_loot_level_t>(b::set_loot_level);
    fn_prop_bool_set = function<prop_bool_set_t>(b::property_bool_set);
    fn_prop_f64_set = function<prop_f64_set_t>(b::property_f64_set);

    // Hooks only read state until g_ready is set.
    bool ui = hook(h_ui, b::update_ui_new_game, (void*)&hk_update_ui_new_game);
    bool gen = hook(h_generate, b::generate_new_game, (void*)&hk_generate_new_game);
    bool tick = hook(h_tick, b::server_scene_tick, (void*)&hk_server_tick);
    bool loading = hook(h_tick_loading, b::server_scene_tick_loading, (void*)&hk_server_tick_loading);
    bool client = hook(h_client_tick, b::client_scene_tick, (void*)&hk_client_tick);
    bool weather = hook(h_weather, b::get_weather_data, (void*)&hk_get_weather_data);
    bool particles = hook(h_particles, b::weather_particles_create, (void*)&hk_particles_create);
    int sandbox = hook(h_tick_spawn, b::creature_tick_spawn, (void*)&hk_tick_spawn) + hook(h_spawn_zones, b::populate_actor_spawn_zones, (void*)&hk_spawn_zones)
                + hook(h_sleeping, b::spawn_sleeping_actors, (void*)&hk_sleeping) + hook(h_spawn_horde, b::spawn_horde, (void*)&hk_spawn_horde)
                + hook(h_destroy_horde, b::is_destroy_horde, (void*)&hk_destroy_horde);
    g_ready = true;
    log(0, "Ready: menu %d, start %d, world binding %d, world ticks %d/%d, loot tier %d, time override %d, "
           "player weather sync %d, weather lock %d, rain density %d, sandbox enemies %d/5",
        ui, fn_start_new_game != nullptr, gen, tick, loading, fn_set_loot_level != nullptr,
        fn_prop_bool_set && fn_prop_f64_set, client, weather, particles, sandbox);
    if (!ui || !fn_start_new_game) log(2, "the Custom Game button cannot start a game on this build");
}
}  // namespace
}  // namespace customgame

using namespace customgame;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out) || !h->plugin_directory)
        return false;
    host = *h; out->id = kModId;
    g_dir = std::filesystem::path(h->plugin_directory) / L"CustomGame";
    out->shutdown = [](void*) {
        g_stop = true;
        h_destroy_horde.remove((void*)&hk_destroy_horde); h_spawn_horde.remove((void*)&hk_spawn_horde);
        h_sleeping.remove((void*)&hk_sleeping); h_spawn_zones.remove((void*)&hk_spawn_zones); h_tick_spawn.remove((void*)&hk_tick_spawn);
        h_particles.remove((void*)&hk_particles_create); h_weather.remove((void*)&hk_get_weather_data);
        h_client_tick.remove((void*)&hk_client_tick); h_tick_loading.remove((void*)&hk_server_tick_loading);
        h_tick.remove((void*)&hk_server_tick); h_generate.remove((void*)&hk_generate_new_game); h_ui.remove((void*)&hk_update_ui_new_game);
    };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(render, nullptr)) {
        gpu = nullptr; log(2, "GPU drawing unavailable: the Custom Game button cannot be shown");
    } else if (!services->input_filter || !services->input_filter(input, nullptr, 40)) {
        log(2, "input filter unavailable: the Custom Game page cannot take clicks");
    }
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
