# API design guidance: stable wrapper boundaries

The game has no modding API. Everything a mod touches is an implementation detail that changes between
builds. The build comparison makes the pattern clear:

- Between the previous SDK's build and this one, **771 of 777 native RVAs moved** with identical code.
- **233 gcl functions changed instructions**.
- **369 code signatures stopped being unique**.
- One type grew a field and shifted **22 virtual slots** (`server_scene.dungeon`).

A general-purpose modding API should therefore hide offsets, slots and signatures behind a small set of
versioned services, and regenerate that layer per build from `json/`.

## Layers

```
mods            -> public API (C ABI, versioned structs, handles/ids, copies)        stable across builds
public API      -> services (world, players, items, ui, save, scheduler, hooks)     stable across builds
services        -> generated binding layer (anymaker_sdk_symbols.hpp + types.hpp)   regenerated per build
binding layer   -> game memory (cells, slots, offsets, natives)                     changes every update
```

## Rules for the public API

1. **Hand out ids and handles, never game pointers.** Actors, vehicles, entities and peers have `s32` ids, and
   items have world-unique ids. A handle is `{kind, id, world_generation}`. Every call re-resolves it through
   `get_*_by_id` on the owning thread and fails cleanly when the object is gone or the world generation changed
   (bump it in the `server_scene.destroy` and `client_scene.destroy` hooks).
2. **Return copies.** Transforms, names and stats go into mod-owned structs (`AnyTransform{double m[12];}`), never
   pointers into game memory.
3. **Own the threads.** Public calls are allowed from any thread. The service posts the work to the server or main
   queue (example 4) and returns a future or callback. Server-authoritative operations are only offered when hosting.
4. **Own all hooks.** One hook per cell, installed by the service, with a fan-out list of mod callbacks. Two mods
   swapping the same cell independently will break each other's restore order (`cell_hook::remove` refuses to
   restore when someone hooked on top).
5. **Version every struct.** `{uint32 struct_size; uint32 version; …}` like AnyAPI ABI 1. Add fields at the end and
   never reorder.
6. **Check the build first.** Compare the PE timestamp and game.gcl size or hash (`sym::*`). On a mismatch, disable
   only the services whose generated bindings failed to resolve, instead of guessing.
7. **Prefer game functions to field writes.** Damage through `server_scene.actor.damage`, items through the
   inventory functions, replicated properties through their setters. Field writes skip replication, events and
   invariants.
8. **Make "closed" explicit.** For anything the system map marks *closed* (new message types, new save type ids,
   new enum values, new implementation classes), the API should say "not supported" rather than offer something
   that desyncs vanilla players.

## Service boundaries suggested by the system map

| Service | Backed by | Status today |
|---|---|---|
| `build` | `build_identity.json`, PE timestamp, gcl hash | ready |
| `resolve` | symbols header routes, natives, globals | ready (runtime-validated layout) |
| `hooks` | cell hooks with fan-out | needs validation (calltest step 4) |
| `scheduler` | hooked `server.tick` / `client.tick` queues | needs validation |
| `lifecycle` | `server_scene`/`client_scene` create/destroy, `state_manager.queue_state` | needs validation |
| `world` | containers + `get_*_by_id`, transforms | needs validation (world phase) |
| `players` | peers, authority actor, damage | needs validation |
| `items` | inventory functions, definitions | items: unresolved ref ownership; definitions: needs validation |
| `ui` | immediate-mode hooks or an external overlay | needs validation |
| `save` | sidecar files keyed by save name | needs validation |
| `net` | none inside the game protocol | unresolved / closed |

See [../capability_matrix.md](../capability_matrix.md) for the symbols, locating method and evidence behind each row.

## What to keep out of the public API

- Raw offsets, slot numbers, RVAs and code signatures. They live only in the generated layer.
- Enum numeric values. Map names to values at bind time; `values_renumbered` in the build diff catches changes.
- Anything in the excluded scope (anti-piracy, bans, auth tickets, telemetry, crash reporting, HTTP).
