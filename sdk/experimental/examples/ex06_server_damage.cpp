// Example 6: perform a server-authorized operation (damage or heal an actor).
//
// Authority: only the host has server_scene. The operation runs on the server thread through
// example 4's queue and uses the game's own virtual server_scene.actor.damage, so the game applies
// incapacitation, AI reactions and replication itself (writing health fields directly would skip
// replication events and desync clients).
// actor_damage_src layout (metadata, 0x40 bytes): m_actor_id_src s32 @0, m_type e_actor_damage_type @4,
// m_position vec3 @8, m_normal vec3 @0x20, m_hit_flesh_effect s32 @0x38. Enum values are static
// inference (position in the recorded name table).
// Uncertain: whether a negative amount heals; the example only applies positive damage.
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

extern "C" void example_post_server(void (*fn)(void*), void* user);

namespace {
using namespace example;
using get_by_id_t = void (*)(void** ret, void* container, const int32_t* id);
using damage_t = void (*)(void* actor, void* scene, const double* amount, const void* src);

struct req { int32_t actor; double amount; };

void do_damage(void* u) {
    req r = *static_cast<req*>(u);
    delete static_cast<req*>(u);
    if (!(r.amount > 0)) return;
    uint8_t* scene = server_scene_object();
    if (!scene) { log("[ex06] not hosting: refusing (server authority required)"); return; }
    void* actors = scene + sym::off_server_scene__m_actors;
    void* actor = nullptr;
    if (auto fn = vmethod<get_by_id_t>(actors, sym::actor_container_get_actor_by_id_vslot)) fn(&actor, actors, &r.actor);
    if (!actor) return;
    alignas(8) uint8_t src[sym::size_server_scene_actor_damage_src] = {};
    *reinterpret_cast<int32_t*>(src + sym::off_server_scene_actor_damage_src__m_actor_id_src) = -1;  // no attacker
    *reinterpret_cast<int32_t*>(src + sym::off_server_scene_actor_damage_src__m_type) = 0;            // first e_actor_damage_type
    *reinterpret_cast<int32_t*>(src + sym::off_server_scene_actor_damage_src__m_hit_flesh_effect) = -1;
    if (auto dmg = vmethod<damage_t>(actor, sym::actor_damage_vslot)) dmg(actor, scene, &r.amount, src);
}
}  // namespace

extern "C" void example_damage_actor(int32_t actor_id, double amount) {
    example_post_server(&do_damage, new req{actor_id, amount});
}
