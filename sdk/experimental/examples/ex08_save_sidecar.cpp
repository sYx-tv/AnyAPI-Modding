// Example 8: save and restore custom state with a save game.
//
// The save format is a closed set of type ids, so mod state goes into a sidecar file. server.save_game
// receives the save name; after the original returns, the hook writes "<name>.mod.json" through the
// game's own file.write_string native into the same store. Restoring happens after
// server_scene.save_data.create_scene_from_data (the load path) has rebuilt the scene, so ids the mod
// stored can be re-resolved (note: the game remaps some ids on load through save_data.id_map).
// Uncertain (static): which file.e_store holds saves and how the save name maps to a path; this example
// writes beside the game's user store using the name it received. Validate the path in game first.
// Thread: server. Authority: host only. Schema: the sidecar carries its own version field.
// Label: compile-tested, statically reviewed.
#include <string>
#include "example_common.hpp"

namespace {
using namespace example;
cell_hook h_save, h_load;
using save_t = void (*)(void* server, const gc_string_view* name);
using load_t = void (*)(void* save_data, void* scene);
using write_t = void (*)(bool* ret, const void* path, const gc_string_view* text);
using read_t = void (*)(bool* ret, const void* path, gc_string_view* out);

std::string g_last_save;
std::string (*g_produce)() = nullptr;           // mod callback: serialize state
void (*g_consume)(const char*, int) = nullptr;   // mod callback: restore state

void on_save(void* server, const gc_string_view* name) {
    ((save_t)h_save.original)(server, name);
    if (!name || !g_produce) return;
    g_last_save.assign(name->data ? name->data : "", name->length > 0 ? (size_t)name->length : 0);
    std::string file = g_last_save + ".mod.json";
    std::string body = "{\"schema\":1,\"data\":" + g_produce() + "}";
    game_path p(sym::file_e_store::user, file.c_str());
    game_string text(body.c_str());
    bool ok = false;
    ((write_t)native_at(sym::file_write_string_rva))(&ok, &p, &text.s);
    log("[ex08] sidecar %s %s", file.c_str(), ok ? "written" : "NOT written");
}

void on_scene_from_data(void* save_data, void* scene) {
    ((load_t)h_load.original)(save_data, scene);
    if (!g_consume || g_last_save.empty()) return;
    std::string file = g_last_save + ".mod.json";
    game_path p(sym::file_e_store::user, file.c_str());
    game_string out("");                       // the game writes into it; freed by game_string's dtor
    bool ok = false;
    ((read_t)native_at(sym::file_read_string_rva))(&ok, &p, &out.s);
    if (ok) g_consume(out.s.data, out.s.length);
}
}  // namespace

extern "C" bool example_sidecar_start(std::string (*produce)(), void (*consume)(const char*, int)) {
    if (!build_matches()) return false;
    g_produce = produce;
    g_consume = consume;
    return h_save.install(cell_of(sym::server_save_game), (void*)&on_save) &&
           h_load.install(cell_of(sym::save_create_scene_from_data), (void*)&on_scene_from_data);
}
extern "C" void example_sidecar_stop() {
    h_load.remove((void*)&on_scene_from_data);
    h_save.remove((void*)&on_save);
}
