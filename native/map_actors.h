#pragma once
// Vehicles, zombies and creatures for AnyMap, read from the game's client scene.
//
// EXPERIMENTAL: uses the experimental SDK (raw game memory), Anymaker 0.1.27 / Steam build 26113663 only.
// Offsets come from the 0.1.27 SDK layouts (metadata, unchanged since 0.1.25). Everything stays off unless anyapi.build
// matches the SDK hashes, so a game update turns the blips off instead of reading the wrong memory.
//
// How: g_client (ref<client>) -> client.m_scene (embedded client_scene) -> m_actors and m_vehicles, both
// vector<ref<T>> containers. Zombies and creatures are told apart by their typeinfo, located through the
// typeinfo slot of their own tick functions. Sampling runs as an anyapi.client_tasks job, so it happens on
// the client actor tick (the thread that owns the scene), never on the render thread.
#include "anyapi_experimental.hpp"
#include "anyapi_client_tasks_v1.h"
#include "vehicle_reads.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace mapactors {
enum Kind : uint8_t { Vehicle, Structure, Zombie, Creature };
struct Blip { double x, z; Kind kind; };

namespace bind {
using anymaker::func_desc;
using anymaker::global_desc;
// ref<client> g_client (python tools/bind.py --reference reference --match g_client)
inline constexpr global_desc g_client{"g_client","48 83 ec 38 48 89 4c 24 20 48 89 54 24 28 48 8b 05 4b 00 00 00 48 83 78 08 00 0f 95 c0 3c 00 0f",96u};
// Typeinfo anchors only; these functions are never called or hooked.
inline constexpr func_desc zombie_tick{"() client_scene.actor_zombie.tick (client_scene.actor_zombie, client, client_scene, const f64, const f64, const vec3)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 08 10 00 00 4c 8b a4 24 70 10 00 00 48 8b 9c 24 78",{"53 55 56 57 48 81 ec 38 01 00 00 48 8b 84 24 80 01 00 00 48 8b ac 24 88 01 00 00 48 89 4c 24 70", {536u}, 1},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 24 03 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",832u,4};
inline constexpr func_desc creature_tick{"() client_scene.actor_creature.tick (client_scene.actor_creature, client, client_scene, const f64, const f64, const vec3)","53 55 56 57 48 81 ec c8 01 00 00 48 8b 84 24 10 02 00 00 48 8b 9c 24 18 02 00 00 48 89 8c 24 e0",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 28 07 00 00 4c 8b a4 24 90 07 00 00 48 8b 84 24 98", {6848u}, 1},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 fd 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05",536u,4};
}  // namespace bind

namespace off {
constexpr ptrdiff_t client_scene = 0xf50;              // client.m_scene (client_scene, embedded)
constexpr ptrdiff_t scene_world_seed = 0x10 + 8;       // client_scene.m_world_seed (replicated property_s32) value
constexpr ptrdiff_t scene_actors = 344 + 8;            // client_scene.m_actors -> _m_elements vector<ref<actor>>
constexpr ptrdiff_t actor_incapacitated = 0x28 + 8;    // actor._m_is_incapacitated (property_bool) value
constexpr ptrdiff_t actor_position = 0x188 + 72;       // actor.m_transform (mat34) .t
constexpr ptrdiff_t vehicle_motion = 0x298 + 8;        // vehicle.m_physics_motion_type value: 0 dynamic, 1 static, 2 kinematic
constexpr ptrdiff_t vehicle_in_scene = 0x2b8;          // vehicle.m_is_added_to_scene
constexpr ptrdiff_t vehicle_transform = 0x310;         // vehicle.m_transform (mat34: three basis vectors, then t)
constexpr ptrdiff_t vehicle_center = 0x408;            // vehicle.m_grid_to_physics_center (vec3, vehicle space)
}  // namespace off

constexpr int kMaxActors = 4096, kMaxVehicles = 65536;
constexpr size_t kMaxBlips = 2048;

struct State {
    std::atomic<uint8_t**> client_ref{};               // address of g_client (ref<client>: control, ptr)
    std::atomic<const uint8_t*> zombie_type{}, creature_type{};
    std::atomic<bool> ready{}, stop{}, pending{}, want_blips{true};
    std::atomic<uint64_t> posted_at{};
    const AnyClientTasksV1* tasks{};
    uint64_t owner{};
    std::mutex lock;                                   // guards blips and the counters below
    std::vector<Blip> blips;
    uint64_t epoch{}, sampled_at{};
    unsigned vehicles{}, zombies{}, creatures{};
    int32_t seed{};                                    // the world seed of `epoch`; 0 until the scene has it
};
inline State state;

inline bool finite3(const double* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The typeinfo the descriptor's typeinfo slot names, accepted only when its method table really holds that
// function at the expected slot.
inline const uint8_t* typeinfo_for(const anymaker::func_desc& d) {
    if (!d.ti_anchor_sig || !*d.ti_anchor_sig || d.method_slot < 0) return nullptr;
    uint8_t* anchor = anymaker::find_gcl_function(d.ti_anchor_sig);
    auto* ti = anchor ? reinterpret_cast<const uint8_t*>(anymaker::read_slot(anchor, d.ti_slot_offset)) : nullptr;
    if (!ti || !anymaker::readable(ti, 0x88)) return nullptr;
    anymaker::gc_typeinfo_view view{ti};
    void* const* table = view.table();
    if (view.kind() != 13 || !anymaker::readable(table, 8 * (size_t(d.method_slot) + 1))) return nullptr;
    void* entry = anymaker::resolve(d);
    return entry && view.method(d.method_slot) == entry ? ti : nullptr;
}

// Background thread: wait for the game code to load, then locate g_client and the two typeinfos.
template <class Log> void resolve(Log log) {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !state.stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "MINIMAP_ACTORS available=0 reason=build_mismatch"); return; }
        auto client = static_cast<uint8_t**>(global(bind::g_client));
        auto zombie = typeinfo_for(bind::zombie_tick), creature = typeinfo_for(bind::creature_tick);
        if (!client || !zombie) { if (attempt == 179) log(2, "MINIMAP_ACTORS available=0 reason=not_located"); continue; }
        state.client_ref = client; state.zombie_type = zombie; state.creature_type = creature;
        state.ready = true;
        log(0, creature ? "MINIMAP_ACTORS available=1 creatures=1" : "MINIMAP_ACTORS available=1 creatures=0");
        return;
    }
}

// Client actor tick (the scene's own thread). Copies positions only; never writes to the game.
inline void sample(const AnyClientTaskFrameV1* frame, void*) {
    using vehicle_reads::rd;
    std::vector<Blip> out;
    unsigned vehicles = 0, zombies = 0, creatures = 0;
    int32_t seed = 0;
    uint8_t** ref = state.client_ref.load();
    uint8_t* client = ref && anymaker::readable(ref, 16) ? ref[1] : nullptr;
    uint8_t* scene = client && anymaker::readable(client + off::client_scene, 13440) ? client + off::client_scene : nullptr;
    if (scene) seed = rd<int32_t>(scene, off::scene_world_seed);
    if (scene && state.want_blips) {
        out.reserve(256);
        const uint8_t *zombie = state.zombie_type, *creature = state.creature_type;
        vehicle_reads::for_each_ref(scene + off::scene_actors, kMaxActors, [&](uint8_t* actor) {
            if (out.size() >= kMaxBlips) return false;
            if (!anymaker::readable(actor, 0x1e8)) return true;
            auto type = anymaker::typeinfo_of(actor);
            if (!anymaker::readable(type.p, 0x30)) return true;
            Kind kind;
            if (anymaker::typeinfo_is(type, zombie)) kind = Zombie;
            else if (creature && anymaker::typeinfo_is(type, creature)) kind = Creature;
            else return true;   // characters: players come from copy_players
            if (rd<bool>(actor, off::actor_incapacitated)) return true;
            double p[3]; std::memcpy(p, actor + off::actor_position, sizeof p);
            if (!finite3(p)) return true;
            out.push_back({p[0], p[2], kind});
            ++(kind == Zombie ? zombies : creatures);
            return true;
        });
        vehicle_reads::for_each_ref(scene + vehicle_reads::off::scene_vehicles, kMaxVehicles, [&](uint8_t* vehicle) {
            if (out.size() >= kMaxBlips) return false;
            if (!anymaker::readable(vehicle, off::vehicle_center + 24) || !rd<bool>(vehicle, off::vehicle_in_scene)) return true;
            double m[12], c[3]; std::memcpy(m, vehicle + off::vehicle_transform, sizeof m); std::memcpy(c, vehicle + off::vehicle_center, sizeof c);
            if (!finite3(m + 9)) return true;
            double p[3]; for (int i = 0; i < 3; ++i) p[i] = m[9 + i] + m[i] * c[0] + m[3 + i] * c[1] + m[6 + i] * c[2];
            if (!finite3(p) || std::fabs(p[0] - m[9]) > 500 || std::fabs(p[2] - m[11]) > 500) std::memcpy(p, m + 9, sizeof p);
            out.push_back({p[0], p[2], rd<uint32_t>(vehicle, off::vehicle_motion) == 1 ? Structure : Vehicle});
            ++vehicles;
            return true;
        });
    }
    {
        std::lock_guard<std::mutex> hold(state.lock);
        state.blips.swap(out); state.epoch = frame ? frame->world_epoch : 0; state.sampled_at = GetTickCount64();
        state.vehicles = vehicles; state.zombies = zombies; state.creatures = creatures; state.seed = seed;
    }
    state.pending = false;
}

// Any thread: queue one sample when none is pending and the last is older than `interval_ms`.
inline void request(uint64_t world_epoch, uint64_t interval_ms) {
    if (!state.ready || !state.tasks || !state.owner) return;
    uint64_t now = GetTickCount64();
    // A job for a world that ended is dropped without a callback; stop waiting for it.
    if (state.pending && now - state.posted_at.load() < 2000) return;
    { std::lock_guard<std::mutex> hold(state.lock); if (state.sampled_at && now - state.sampled_at < interval_ms && state.epoch == world_epoch) return; }
    state.pending = true; state.posted_at = now;
    if (!state.tasks->post(state.owner, world_epoch, sample, nullptr)) state.pending = false;
}

// The world seed sampled in `world_epoch`: it tells saves apart, also for players who joined. False until known.
inline bool world_seed(uint64_t world_epoch, int32_t& out) {
    std::lock_guard<std::mutex> hold(state.lock);
    if (state.epoch != world_epoch || !state.sampled_at || !state.seed) return false;
    out = state.seed;
    return true;
}

// Blips for the current world, or none when the last sample is stale or from another world.
inline void copy(uint64_t world_epoch, std::vector<Blip>& out) {
    std::lock_guard<std::mutex> hold(state.lock);
    if (state.epoch != world_epoch || !state.sampled_at || GetTickCount64() - state.sampled_at > 3000) { out.clear(); return; }
    out = state.blips;
}
}  // namespace mapactors
