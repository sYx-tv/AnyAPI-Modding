// Example 1: resolve a manager and look up an entity / actor by id.
//
// Object graph (metadata offsets from anymaker_sdk_symbols.hpp):
//   g_server : ref<server>  ->  server.m_scene : ref<server_scene>  ->  server_scene.m_actors
//   (server_scene.actor.container, embedded)  ->  get_actor_by_id (virtual, called through the
//   container's typeinfo; dispatch rule runtime-validated).
// Prerequisites: hosting a game (g_server non-null); must run on the server thread, so the lookup is
// posted through example 4's queue.
// Failure handling: null checks at every hop; ids that are not present return nullptr from the game.
// Lifetime: the returned ptr<server_scene.actor> is borrowed and valid only during this tick. Copy
// the fields you need (id, transform) instead of keeping the pointer.
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

extern "C" void example_post_server(void (*fn)(void*), void* user);

namespace {
using namespace example;
using get_by_id_t = void (*)(void** ret, void* container, const int32_t* id);

struct lookup_request { int32_t actor_id; };

void do_lookup(void* user) {
    auto* req = static_cast<lookup_request*>(user);
    uint8_t* scene = server_scene_object();
    if (!scene) { log("[ex01] no server scene (not hosting?)"); delete req; return; }
    void* actors = scene + sym::off_server_scene__m_actors;
    auto fn = vmethod<get_by_id_t>(actors, sym::actor_container_get_actor_by_id_vslot);
    void* actor = nullptr;
    if (fn) fn(&actor, actors, &req->actor_id);
    if (!actor) { log("[ex01] actor %d not found", req->actor_id); delete req; return; }
    auto* a = static_cast<uint8_t*>(actor);
    int32_t id = *reinterpret_cast<int32_t*>(a + sym::off_server_scene_actor__m_id);
    const double* t = reinterpret_cast<const double*>(a + sym::off_server_scene_actor__m_transform);
    // mat34 = mat33 m (3 x vec3 rows) + vec3 t at +0x48  -> translation is t[9..11]
    log("[ex01] actor %d at (%.2f, %.2f, %.2f)", id, t[9], t[10], t[11]);
    delete req;
}
}  // namespace

extern "C" void example_lookup_actor(int32_t actor_id) {
    example_post_server(&do_lookup, new lookup_request{actor_id});
}
