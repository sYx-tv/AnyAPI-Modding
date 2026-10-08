// Example 7: register content (inventory definitions from a mod JSON file).
//
// inventory_definition_container.load(container, path, outfit_resources) destroys the container's
// contents, reads rom/data/inventory_definitions.json into an inventory_definition_file and adds the
// definitions (static: its call list contains destroy, inventory_definition_file.load and
// _add_definitions). This hook lets the original load run, then loads the mod's file with the same
// format and adds it with the same _add_definitions call, passing through the original
// outfit_resources. Both the server and the client scene call load, so the mod is applied on both.
// Requirements: every entry's "class" must be an existing implementation class; every player must run
// the same mod (definition ids/indices are used by saves and replication).
// File store: file.e_store values are static inference (embedded, system, rom, user); this uses an
// absolute path with the "system" store - validate before relying on it.
// Ownership: the inventory_definition_file is constructed/destructed with the game's own ctor/dtor; the
// definitions themselves are ref<> and are retained by the container (static).
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

namespace {
using namespace example;
cell_hook h_load;
using load_t = void (*)(void* container, const void* path, const void* outfit);
using ctor_t = void (*)(void* self);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
using add_t = void (*)(void* container, const void* file, const void* outfit);

char g_mod_json[MAX_PATH];

void on_load(void* container, const void* path, const void* outfit) {
    ((load_t)h_load.original)(container, path, outfit);
    auto ctor = (ctor_t)resolve(sym::inventory_definition_file_ctor);
    auto dtor = (ctor_t)resolve(sym::inventory_definition_file_dtor);
    auto fload = (file_load_t)resolve(sym::inventory_definition_file_load);
    auto add = (add_t)resolve(sym::inventory_definitions_add_definitions);
    if (!ctor || !dtor || !fload || !add) { log("[ex07] registration functions not resolved"); return; }
    alignas(16) uint8_t file[sym::size_inventory_definition_file] = {};
    ctor(file);
    game_path p(sym::file_e_store::system, g_mod_json);
    bool ok = false;
    fload(&ok, file, &p);
    if (ok) add(container, file, outfit);
    log("[ex07] mod definitions %s from %s", ok ? "added" : "NOT loaded", g_mod_json);
    dtor(file);
}
}  // namespace

extern "C" bool example_register_start(const char* absolute_json_path) {
    if (!build_matches() || !absolute_json_path) return false;
    strncpy_s(g_mod_json, absolute_json_path, _TRUNCATE);
    return h_load.install(cell_of(sym::inventory_definitions_load), (void*)&on_load);
}
extern "C" void example_register_stop() { h_load.remove((void*)&on_load); }
