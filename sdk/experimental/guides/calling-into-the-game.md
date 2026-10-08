# Calling into the game reliably

Everything here applies to Steam build 25755694 (game.exe `97ea559f…`, game.gcl `17cc5526…`) and was
checked again for that build. Each claim is tagged:

- **[metadata]**: read from a recorded structure in game.gcl or game.exe.
- **[static]**: inferred from code patterns or static call relationships.
- **[runtime]**: observed in the running game by `validation/probe.py`, which only reads memory
  (results in `validation/runtime_menu.json`, summarised in `json/runtime_evidence.json`).
- **[runtime-call]**: exercised in-process by `validation/calltest` (`validation/calltest_result_menu.log`, 10 of 11 checks passed;
  the failure revealed the `$`-helper rule below).

## 1. Two kinds of code

| | gcl ("geocode") functions | game.exe natives |
|---|---|---|
| Where | JIT-linked from `bin/game.gcl` at startup into private executable memory | in the game.exe image |
| Count (this build) | 36,990 with code, 19,349 native declarations | 1,184 registered by signature; 8,593 gcl declarations observed bound at runtime |
| Address | none static; found per process | game.exe base + RVA |
| How to find | code signature, caller slot, chain, typeinfo table ([§3](#3-resolving-functions-globals-and-virtual-methods)) | `json/natives.json` (static RVA), `json/native_bindings.json` (runtime-observed RVA) |

**[runtime]** The loader copies gcl code unchanged. 26,706 functions were found in memory, and all of
them were byte-identical to game.gcl. Each starts on a 64-byte boundary in `MEM_PRIVATE` memory with
`PAGE_EXECUTE_READWRITE` (12 regions) or `PAGE_EXECUTE_READ` (1 region).

## 2. The gc-x64 calling convention

| Rule | Evidence |
|---|---|
| Every argument is passed **by pointer**, scalars included (`const s32` arrives as `int32_t*`) | [metadata] the prologues of 34,405 functions spill each register parameter to the stack slot game.gcl records for it; [static] the body dereferences it (`cmp dword ptr [r8], eax`) |
| A non-void function returns through a **hidden pointer in rcx**, and the real arguments shift one register right | [static] `movsd [rcx], xmm0` at return sites; the `vector<$>.size` native does `mov eax,[rdx+0xc]; mov [rcx],eax` |
| Register order is rcx, rdx, r8, r9, then the stack from `[rsp+0x28]`, with 0x20 bytes of shadow space | [metadata] spill offsets; [static] fifth argument read from the stack |
| The stack is 16-byte aligned at every call | [static] after the pushes and `sub rsp`, rsp ≡ 8 (mod 16) in 35,724 prologues; the other 1,287 make no calls |
| Callee-saved GPRs (rbx, rbp, rsi, rdi, r12–r15) are pushed before use | [static] 0 functions write a non-volatile GPR without pushing it (`json/abi_facts.json`) |
| **xmm6–xmm15 are never touched by gcl code** | [static] full disassembly of all 37,011 code blobs: 0 uses. Calling gcl cannot clobber them. Natives are MSVC code and follow Win64. |
| Struct returns: the caller provides storage at `*rcx`, and the callee may **assign** into it (copy) rather than construct it | [static] |

In C++ (MSVC/clang-cl, x64) declare gcl functions and natives with pointer parameters and an explicit return
pointer:

```cpp
// (ptr<server_scene.actor>) server_scene.actor.container.get_actor_by_id (server_scene.actor.container, const s32)
using get_actor_by_id_t = void (*)(void** ret, void* container, const int32_t* id);
// (f64) math.sqrt (const f64)        -- a native, same convention
using sqrt_t = void (*)(double* ret, const double* x);
// (mat34) server_scene.entity.get_transform (const server_scene.entity) -- 96-byte struct return
using get_transform_t = void (*)(double* ret_mat34, const void* entity);
```

**Argument kinds.**

- **Scalars** (`s32`, `f64`, `bool`, enums): pass a pointer to a local holding the value. A `bool` is 1 byte, an enum 4 bytes **[metadata]**.
- **Structs** (`vec3`, `mat34`, `collision.ray`…): pass a pointer to caller storage with the layout in `include/anymaker_sdk_types.hpp`. `const` struct arguments are read-only. Non-const struct arguments can be written; for example, `server_scene.inventory.get_items` fills a `vector<…>` **[static]**.
- **Objects** (`server_scene`, `client`…): pass the object pointer itself. `ptr<T>` is already a pointer, so passing a `ptr<T>` *argument* means a pointer to the 8-byte pointer **[static]**.
- **`string` arguments**: pass a pointer to a 16-byte game string. Read-only natives accept any `{char*, int32 length}` view; [runtime] the menu probe decoded all 15 string globals this way. For anything the game may keep or free, build the string with `$string_ctor_cstr` and release it with `$string_dtor`. Their RVAs were resolved at runtime; the previous SDK left them unknown. See `examples/example_common.hpp` `game_string`.

**Validation status [runtime-call].** Natives (`math.sqrt`, `math.clamp`, `string.length`, `string.find`), a
string return through a caller-constructed slot (`application.get_version` → `v0.1.23`) and JIT functions
(`math.divide_s32_ceil`, `math.sign`, `math.wrap`) all returned the expected values with this convention. A
caller's double survived 1,000 gcl calls.

**Exception: `$`-prefixed runtime helpers** (`$string_*`, `$ref_*`, `$heap_allocate`…) take scalar arguments
**by value**. Calling `$string_ctor_cstr(string*, const char**)` built a 6-byte garbage string; the helper reads its
second argument as the C-string pointer itself [runtime-call]. Object arguments (`string*`) are still pointers.
The corrected call is in `examples/example_common.hpp` but has not been re-run.

## 3. Resolving functions, globals and virtual methods

### Relocation slots **[runtime]**

Each gcl function's code is followed by 8-byte slots, one per relocation, in the order game.gcl records them.
`json/functions.json` gives `gcl.code_size`; slot *k* is at `entry + code_size + 8*k`. `json/anchors.json` gives
`slot_offset` directly.

| Slot kind (gcl reloc kind) | Holds | Runtime check |
|---|---|---|
| 3 global | the global's address | 870 globals resolved; every slot naming the same global agreed |
| 4 call | the address of the callee's **cell**; the cell holds the entry point | 52,511 call sites; for every located gcl callee, cell → entry was the callee, 0 mismatches |
| 5 typeinfo | the type's typeinfo record | 2,829 records; the kind dword is 13 (struct) or 14 (enum) |
| 6 virtual | the byte offset `8 × slot` into the method table | 4,973 methods, all equal to 8 × their zero-based slot |
| 1 (no symbol) | one process-wide address shared by every function that has one | purpose unknown |

### One cell per function: patch-free hooks **[runtime]**

- Every callee had **exactly one cell** (20,119 checked).
- Method tables point at **the same cell** that direct callers use (5,072 / 5,072).
- Cells live in `PAGE_READWRITE` private memory.

So swapping the 8-byte value of a function's cell redirects every direct and virtual call to it, without
patching code or changing page protection. `anymaker::cell_hook` in `include/anymaker_sdk_runtime.hpp`
does this with an interlocked compare-exchange and keeps the original for call-through.
**[runtime-call]** calltest swapped the cell of `state_manager.tick` (reached through `on_update`'s call slot) for 5 s at
the main menu. The hook saw 300 calls (60 per second), called through to the original, and restored the cell; the game
kept running normally.

### Routes to a function (`json/anchors.json`)

| Route | Meaning | Functions (this build) |
|---|---|---|
| `direct` | code prefix unique among all gcl code; scan for it | 12,682 |
| `via_caller` | uniquely-signed caller, slot offset → cell → entry | +4,980 |
| `chain` / `cell_chain` | repeat slot → cell → entry from a uniquely-signed root (hooks use the last cell) | (part of the next row) |
| `via_typeinfo` | typeinfo slot of a uniquely-signed function → `[typeinfo+0x80] + 8×slot` → cell → entry | total with any route: **23,029** of 36,990 |
| object dispatch | for a method of an object you already have: `object+0 → typeinfo → +0x80 → +8×slot → cell → entry` | every virtual method |

`include/anymaker_sdk_symbols.hpp` (generated) packages these routes per function, and
`anymaker::resolve(desc)` / `anymaker::cell_of(desc)` follow them.

### Object model for dispatch **[runtime]**

```
object+0x00         -> typeinfo of the dynamic type (14/14 class-typed globals checked)
typeinfo+0x00 (u32) =  13 struct / 14 enum
typeinfo+0x28       -> parent typeinfo (1,779 parent links matched, 0 differed; null for roots)
typeinfo+0x80       -> method table; table[slot] = cell; *cell = entry (53,916 entries matched, 0 differed)
```

`slot` is the zero-based position in the dynamic type's method table, which is the `vtable` order in
`json/types.json` and the `*_vslot` constants in the symbols header.

### Globals

Read the global slot of an anchor function (`anchors.json` `globals`, or `global_desc` in the symbols header).
Globals are `PAGE_READWRITE` private memory. They are allocated before `on_create` and constructed in the recorded
`init_order` **[metadata]**.

### Natives

`game.exe base + RVA`. Static RVAs come from the registration code (777). The runtime probe confirmed 563 of them
and found **0 mismatches**. It resolved 325 of the 407 natives that static analysis could not, including every
`$string_*` and `physics.*` native it saw called. The remaining 82 are listed in `docs/known-gaps.md`.

## 4. Construction, destruction, copying and ownership

| Topic | Rule | Evidence |
|---|---|---|
| Constructors | types with `nontrivial_lifetime` (flags bit 1) have a `ctor`, and usually `dtor`/`copy`/`copy_ctor`, named in `types.json`. Call them on raw storage of `size` bytes | [metadata] names; [static] gcl codegen calls `ctor` on every local of such types |
| Object header | types with flags bit 0 start with the typeinfo pointer. `ctor` stores it; never construct such a type by `memset` | [runtime] header → typeinfo; [static] ctor stores [rip+typeinfo slot] |
| Allocation | heap objects are created through `$ref_ctor_alloc` and owned by `ref<T>`. Containers own their elements (`push(ref<T>)`) | [static] |
| `ref<T>` | 16 bytes; `+0x08` points at the object or value; `+0x00` is a control/allocation pointer with an unknown layout | [runtime] `ref<f64>`, `ref<s32>`, `ref<main_menu>` globals |
| Copying a `ref` | only through `$ref_copy` / `$ref_ctor_copy` (RVAs runtime-resolved); a bytewise copy skips the reference count | [static] |
| Releasing | `$ref_dtor`. A function returning `ref<T>` (for example `server_scene.inventory.destroy_item`) hands the caller a reference to release | [static] |
| `ptr<T>` | 8-byte raw, non-owning pointer | [metadata] |
| Struct return slots | the caller constructs the slot (`ctor`) before the call and destroys it after (`dtor`), because callees assign into it | [static] |

## 5. Strings, arrays, vectors, maps and handles

| Type | Layout | Evidence | Mutate through |
|---|---|---|---|
| `string` (16) | `+0` `char*` UTF-8, NUL-terminated; `+8` `int32` byte length; `+0xC` unknown | [runtime] 15/15 string globals; [metadata] `string.length` thunk | `$string_*` natives |
| `vector<T>` (40) | ring buffer: `+0` buffer, `+8` start offset, `+0xC` count, `+0x10` capacity, `+0x14` element size | [static] `vector<$>.value` thunk; [runtime] count ≤ capacity and element size = sizeof(T) for every vector global | `vector<$>.*` natives (`native_bindings.json`) |
| `array<T>` (32) | `+0` buffer, `+8` count, `+0xC` element size | [static] `array<$>.value` thunk; not yet seen at runtime | `array<$>.*` natives |
| `map<K,V>` | layout unknown | — | only the `map<$>.*` natives (begin/next/key/value/insert/erase), all bound at runtime |
| `bitset` | 4 bytes | [metadata] size | `bitset.*` |

Read-only accessors are `gc_string_view`, `gc_vector_raw::at<T>`, `gc_array_raw::at<T>` and `gc_ref_raw<T>` in the
runtime header.

## 6. Lifetime, invalidation and safe lookup

- **Address by id, re-resolve every tick.** Actors, vehicles, entities, peers and items carry `s32` ids (`m_id` at
  `+0x40` for actor, entity and vehicle **[metadata]**). Containers expose `get_*_by_id`. A `ptr<T>` you get back is
  valid until that object is destroyed, so do not keep it past the current tick **[static]**.
- **World boundaries invalidate everything.** `server_scene.destroy` and `client_scene.destroy` free all scene
  objects. Clear caches in a hook on those (example 3) **before** calling the original.
- **Ids can be remapped across save/load** (`server_scene.save_data.id_map`) **[static]**.
- **Defensive reads:** `anymaker::readable(p, n)` avoids an access violation when a pointer you didn't just get from
  the game has gone stale. It does not make a dangling pointer valid.

## 7. Initialization, world load/unload and shutdown order

The order comes from the relocation order in the entry points **[static]** and the global `init_order`
**[metadata]**.

1. Globals are constructed in `init_order` (929 globals; `g_server` 29, `g_client` 30, `g_settings` 898 …).
2. `on_create`: `settings.create` → window/graphics setup → `audio_manager.create` → `mm_ui.create` →
   `frontend_ui.create` → `renderer.create` → `color_palette.create` → `input.create_input_bindings` →
   `state_manager.create` → `g_main_menu` (`$ref_ctor_alloc`, `main_menu.create`) → `localization.create` / `load_tables`.
3. Per frame, `on_update` (main thread) runs `state_manager.tick`, `frontend_ui.begin`, `client.tick` (only while
   not loading), `frontend_ui.end`, `main_menu.tick`, `audio_manager.tick`, `audio_music.tick` and `renderer.tick`.
   It also tears down the server (`application.server_stop`, `server.destroy`) when the state machine asks.
4. World load: `state_manager` states (`state_end_main_menu_begin_host_*`, `state_load_game`,
   `connect_to_game`) create `g_server` (host: `server.create` → `server_scene.create`, which loads the definition
   containers) and `g_client` (`client.create` → `client_scene.create`).
5. The server loop (`server_tick`, its own thread) calls `server.tick` and sleeps (`system.sleep`).
6. Unload: `state_unload_game_*` → `client.destroy` / `client_scene.destroy`, `server.destroy` /
   `server_scene.destroy`.
7. `on_destroy`: `settings.save` → `audio_manager.destroy` → `client.destroy` → `application.server_stop` →
   `server.destroy` → `main_menu.destroy` → `state_manager.destroy` → `frontend_ui.destroy` → `mm_ui.destroy` →
   `renderer.destroy`.

**[runtime, hosted world]** `g_server`/`g_client` refs, `server.m_scene` and the scene containers were found at their
metadata offsets with the right typeinfo (`validation/world_host_observations.md`). The order of steps 4–6 itself is
static; a joined (client-only) session has not been observed.

## 8. Threads, synchronisation and where to run work

| Thread | Entry | Owns | Evidence |
|---|---|---|---|
| main | `on_update`, `on_render`, input entry points | client, client_scene, UI, audio, renderer | [static] reachability from game.exe entry points |
| server loop | `server_tick` → `server.tick` | server, server_scene and everything under it | [static] |
| Client Tile Worker | `client_scene.environment_tile_task.execute` | tile build jobs | [static] thread names in game.exe |
| Client Vehicle Worker | `client_scene.vehicle_task.execute` | vehicle integration jobs | [static] |
| Server Tile Worker | `server_scene.environment_tile_task.execute` | tile generation/save jobs | [static] |
| loader worker | AnyAPI plugin init (outside the game) | — | AnyAPI docs |

- `server_tick` only takes `g_debug_render_server_mutex` around the debug-render buffer swap **[static]**. There is
  **no lock between server state and the main thread**, so never touch `server_scene` from the main thread or
  `client_scene` from the server thread.
- **Safe places to schedule work:** inside a hooked `server.tick` (server data) or `client.tick` (client data).
  Post with a lock-protected queue and drain in the hook (example 4).
- `system.work`, `system.signal`, `system.atomic_counter` and `system.mutex` are the game's own primitives
  (natives, RVAs known). Their ownership rules are unknown, so mods should use their own.
- Thread tags in the JSON come from call-graph reachability that expands every virtual call to all
  implementations. They are a **superset**.
