// Example 4: schedule work on the correct thread.
//
// What it shows: cell hooks on server.tick (server loop thread) and client.tick (main thread) that
// drain thread-safe queues, so any mod code can post work from anywhere and have it run where the
// game data it touches is owned.
// Prerequisites: game started (JIT loaded). server.tick only runs while hosting a game.
// Failure handling: every resolve is checked; nothing is hooked if the build differs.
// Cleanup: example_schedule_stop() removes both hooks (only if no other mod re-hooked on top).
// Evidence: cell layout/sharing runtime-validated; hook install not yet exercised (calltest step 4
// exercises the same mechanism on state_manager.tick). Label: compile-tested, statically reviewed.
#include "example_common.hpp"

namespace {
using namespace example;
work_queue g_server_q, g_main_q;
cell_hook g_server_hook, g_client_hook;

using server_tick_t = void (*)(void* server);
using client_tick_t = void (*)(void* client, const double* dt, const bool* b, void* frontend_ui, void* settings);

void hooked_server_tick(void* server) {
    ((server_tick_t)g_server_hook.original)(server);   // game first: our work sees this tick's state
    g_server_q.drain();
}
void hooked_client_tick(void* client, const double* dt, const bool* b, void* fui, void* settings) {
    ((client_tick_t)g_client_hook.original)(client, dt, b, fui, settings);
    g_main_q.drain();
}
}  // namespace

extern "C" bool example_schedule_start() {
    if (!build_matches()) return false;
    void** sc = cell_of(sym::server_tick);
    void** cc = cell_of(sym::client_tick);
    if (!sc || !cc) { log("[ex04] tick cells not found"); return false; }
    if (!g_server_hook.install(sc, (void*)&hooked_server_tick)) return false;
    if (!g_client_hook.install(cc, (void*)&hooked_client_tick)) { g_server_hook.remove((void*)&hooked_server_tick); return false; }
    return true;
}

extern "C" void example_post_server(void (*fn)(void*), void* user) { g_server_q.post([=] { fn(user); }); }
extern "C" void example_post_main(void (*fn)(void*), void* user) { g_main_q.post([=] { fn(user); }); }

extern "C" void example_schedule_stop() {
    g_client_hook.remove((void*)&hooked_client_tick);
    g_server_hook.remove((void*)&hooked_server_tick);
    // Work still queued is dropped on purpose: it may reference a world that no longer exists.
}
