# Known gaps and prioritized next investigations

Build: Steam 25755694. "Runtime" below means `validation/probe.py`, which only reads memory, run at the main
menu. Nothing has run inside a loaded world yet, and nothing has called game code from a mod yet.

## Closed since the previous SDK

| Previous gap | Now |
|---|---|
| Game version, Steam build ID unknown | Steam build ID **25755694** (metadata, appmanifest); game version **v0.1.23** (runtime, `application.get_version` called in-process) |
| JIT code assumed unchanged after load | **runtime:** 26,706 functions located, all byte-identical; 64-byte aligned |
| Slot/cell semantics inferred | **runtime:** global slots (870), call cells (52,511), typeinfo records (2,829), virtual slot values = 8 × slot (4,973), method tables (53,916 entries) all matched |
| Object header / vtable stride unknown | **runtime:** object+0 → typeinfo; typeinfo+0x28 parent; +0x80 table; stride 8 |
| String natives, physics natives unresolved | **runtime:** 325 of 407 previously unknown natives resolved through call cells, including every `$string_*`, `$ref_*` and `physics.*` native that was called |
| xmm6–xmm15 preservation unknown | **static, complete:** no gcl function uses xmm6–xmm15 |
| Hooking needed code patches | **runtime:** one cell per function, shared by dispatch tables, PAGE_READWRITE; a cell hook on `state_manager.tick` ran 5 s and restored cleanly |
| Calling convention only static | **runtime:** natives, JIT functions, a struct/string return and xmm preservation behaved as documented |
| Generators only ran in the cloud with fixed paths | all tools take paths, run on Windows, one-command `tools/regenerate.py` |

## Open, in priority order

1. **`$`-helper calling convention.** The in-process call test passed 10 of 11 checks (natives, JIT calls, struct
   return, version, cell hook). It also showed that `$string_ctor_cstr` takes its C string by value. Re-run the
   corrected call and check `$ref_*` ownership (`ref<T>` copy/release) the same way. Running the test needs the DLL
   loaded into the game; Nate approved that for the main menu.
2. **Joined-client world probe.** The hosted-world probe is done (`validation/world_host_observations.md`: object graph,
   container typeinfo, vector layout confirmed). Still open from a client-only process and deeper inside live objects: `g_server`/`g_client` refs non-null, `server.m_scene` ref layout, container layouts
   (`server_scene.actor.container` etc.), vectors and maps inside live objects, which of the 13,961 functions with
   no static route get located once the world code runs, and client-only vs host process differences.
3. **`ref<T>` control block and ownership transfer.** `+0x00` is unknown. This blocks creating items and giving them
   to players (`items.give` is *unresolved*) and anything else that passes `ref<T>` into the game.
4. **`map<K,V>` layout.** Unknown. Only usable through the `map<$>.*` natives today.
5. **82 registered signatures with no RVA**, static or runtime. 13 are engine callbacks that game.exe calls *into*
   gcl (`on_create`, `on_update`, `server_tick`, `on_action_*`, `on_input_*`, `on_physics_contact_*`, …), so they have
   no thunk. The real gaps are 41 `application.*` (mostly graphics command-buffer) natives, 17 `physics.*` natives,
   eight `$` runtime helpers (`$heap_allocate`, `$ref_ctor`, `$ref_cmp`, `$string_append`, `$string_assign_buffer`,
   `$string_comparison_order`, `$string_ctor_buffer`, `$task_exec`) and three others. None was called from code
   that ran at the menu or in the hosted world. They are listed in `json/natives.json` (`thunk_rva == null` and no
   `runtime` block).
6. **13,961 gcl functions without a static locator route.** At runtime, 26,706 were reached through cells and
   dispatch tables; the rest belong to code that hadn't run or wasn't referenced from located code. A world-phase
   run and a "transitive through the object graph" pass would add more.
7. **Client/server classification** is static (namespace, source path, reachability). `shared` code under `mm6/` and
   template families is not split. A per-thread runtime sampler (hook counters on cells, after item 1) would make
   it runtime-observed.
8. **Enum numeric values** are positions in the recorded name table (static). Checking a few against live values
   (gamemode, damage type) in the world phase would confirm the rule.
9. **UI toolkit semantics** (`mm_ui` begin/end scoping, whether the menu bar exists in retail builds) are inferred
   from names; example 5 depends on them.
10. **Save store and path mapping** (`file.e_store` meaning, where a save name lands on disk) are static; example 8
    depends on them.
11. **Render pipelines and shader libraries** (`rom/*.slib`): format and binding layouts unknown. Custom
    materials/passes stay *unresolved*.
12. **No in-game channel for mod data between players.** The protocol is closed. A mod network channel would have
    to live outside the game protocol (own socket or Steam networking), which is out of scope here.

## Not present in this build

- Crafting: no recipe, craft, fabricator, workbench or assembly types or functions (name search; static).

## Deliberately excluded

Anti-piracy, ban, auth-ticket, support-tracker, crash-report, telemetry and HTTP symbols are dropped from every
output. Fields of that kind keep their offsets with name and type withheld, so surrounding layouts stay correct.
