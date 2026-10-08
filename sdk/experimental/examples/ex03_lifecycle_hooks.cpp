// Example 3: subscribe to lifecycle events (world created / destroyed, on both sides).
//
// Hooks the cells of server_scene.create/destroy and client_scene.create/destroy. These are virtual
// methods; their cells come from the owning type's dispatch table (typeinfo route), which is the same
// cell direct callers use (runtime: 5,072/5,072 dispatch cells were the call cell).
// Use: clear every cached pointer and per-world mod state in the destroy hooks; (re)build it in the
// create hooks *after* calling the original.
// Prerequisites: game started. Thread: server_scene.* on the server thread, client_scene.* on main.
// Cleanup: example_lifecycle_stop().
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

namespace {
using namespace example;
cell_hook h_sc, h_sd, h_cc, h_cd;
std::atomic<int> g_worlds{0};

using sc_create_t = void (*)(void* scene, const gc_string_view* name, const int32_t* gamemode);
using sc_destroy_t = void (*)(void* scene);
using cc_create_t = void (*)(void* cscene);
using cc_destroy_t = void (*)(void* cscene, void* client);

void on_server_scene_create(void* s, const gc_string_view* name, const int32_t* gm) {
    ((sc_create_t)h_sc.original)(s, name, gm);
    g_worlds++;
    log("[ex03] server scene created: '%.*s' gamemode %d", name ? name->length : 0, name && name->data ? name->data : "", gm ? *gm : -1);
}
void on_server_scene_destroy(void* s) {
    log("[ex03] server scene destroying");   // drop server-side caches BEFORE the original frees objects
    ((sc_destroy_t)h_sd.original)(s);
    g_worlds--;
}
void on_client_scene_create(void* s) {
    ((cc_create_t)h_cc.original)(s);
    log("[ex03] client scene created");
}
void on_client_scene_destroy(void* s, void* c) {
    log("[ex03] client scene destroying");
    ((cc_destroy_t)h_cd.original)(s, c);
}
}  // namespace

extern "C" bool example_lifecycle_start() {
    if (!build_matches()) return false;
    bool ok = h_sc.install(cell_of(sym::server_scene_create), (void*)&on_server_scene_create) &&
              h_sd.install(cell_of(sym::server_scene_destroy), (void*)&on_server_scene_destroy) &&
              h_cc.install(cell_of(sym::client_scene_create), (void*)&on_client_scene_create) &&
              h_cd.install(cell_of(sym::client_scene_destroy), (void*)&on_client_scene_destroy);
    if (!ok) log("[ex03] could not hook every lifecycle cell (route missing for this build?)");
    return ok;
}

extern "C" void example_lifecycle_stop() {
    h_cd.remove((void*)&on_client_scene_destroy);
    h_cc.remove((void*)&on_client_scene_create);
    h_sd.remove((void*)&on_server_scene_destroy);
    h_sc.remove((void*)&on_server_scene_create);
}
