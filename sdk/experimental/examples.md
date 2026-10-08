# Examples

All examples are in `examples/`. They share `examples/example_common.hpp`, a small service layer (build check,
game strings, object-graph walk, virtual calls through typeinfo, thread queues) that shows the wrapper boundary
from [guides/api-design.md](guides/api-design.md). They build against `include/anymaker_sdk_runtime.hpp`
(hand-written, runtime-validated rules) and `include/anymaker_sdk_symbols.hpp` (generated per build).

Each example exports plain `extern "C"` start/stop functions, so any loader can drive it. The AnyAPI loader
(ABI 1, `AnyAPI_ModInit`) works, and so does your own.

| # | File | Shows | Thread / authority | Status |
|---|---|---|---|---|
| 1 | `ex01_lookup_entity.cpp` | `g_server` → `server.m_scene` → `m_actors` → `get_actor_by_id` (virtual call through the container's typeinfo); copy the id and transform | server thread, host only | compile-tested, statically reviewed |
| 2 | `ex02_transform_and_mutation.cpp` | `server_scene.entity.get_transform` (96-byte struct return through the hidden pointer); client-local FOV override with save/restore | server (read), main (write) | compile-tested, statically reviewed |
| 3 | `ex03_lifecycle_hooks.cpp` | cell hooks on `server_scene`/`client_scene` create/destroy through the typeinfo route; where to build and drop per-world state | server / main | compile-tested, statically reviewed |
| 4 | `ex04_schedule_work.cpp` | cell hooks on `server.tick` and `client.tick` draining lock-protected queues; `example_post_server` / `example_post_main` | server / main | compile-tested, statically reviewed |
| 5 | `ex05_ui_menu_item.cpp` | immediate-mode UI: emit an `mm_ui.button` from a hooked `client.update_ui_menu_bar`; game string built and freed per frame | main | compile-tested, statically reviewed; menu-bar visibility in retail is unverified |
| 6 | `ex06_server_damage.cpp` | server-authorized operation: build an `actor_damage_src` and call the virtual `server_scene.actor.damage`; refuses when not hosting | server, host only | compile-tested, statically reviewed |
| 7 | `ex07_register_items.cpp` | register inventory definitions from a mod JSON after `inventory_definition_container.load`, using the game's own `inventory_definition_file` ctor/load/dtor and `_add_definitions` | scene creation (server and client) | compile-tested, statically reviewed; all peers need the same file |
| 8 | `ex08_save_sidecar.cpp` | persist mod state beside a save (`server.save_game`) and restore it after `create_scene_from_data`, through `file.write_string`/`read_string`, with a schema version | server, host only | compile-tested, statically reviewed; save path mapping unverified |

**What the labels mean.**

- *Compile-tested*: `tools/validate_compile.py` compiled the file with MSVC 14.51 (`/std:c++17 /W3`); see
  `validation/compile_result.json`.
- *Statically reviewed*: every symbol, offset, slot and route comes from the generated json for this build, and
  the call shapes follow the gc-x64 rules.
- *Runtime-tested*: none of the examples themselves. Their mechanisms are runtime-validated: slots, dispatch
  tables and string layout by the read-only probe; calls with the gc-x64 convention and a cell hook install and
  restore in-process by `validation/calltest`.

**Prerequisites common to all.** The game is started, so the JIT has loaded. Examples 1, 2, 6 and 8 need to
be hosting a world. All examples call `example::build_matches()` first and do nothing on another build.
