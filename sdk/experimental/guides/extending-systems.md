# Extending systems rather than editing values

For each major system, [../systems/README.md](../systems/README.md) answers seven questions:

1. Can a mod observe an event?
2. Can it change an existing value or behaviour?
3. Can it create and destroy objects?
4. Can it register a new definition or implementation?
5. Can it attach custom behaviour or data?
6. Can it persist custom state?
7. Can it replicate custom state to other players?

Each answer has a status: *ready*, *needs_validation*, *unresolved*, *closed* or *not_present*. This page
explains the four mechanisms those answers use, and the steps every one of them needs.

## The four mechanisms

| Mechanism | What it is | What it can do | Limits |
|---|---|---|---|
| **Cell hook** | swap the 8-byte call cell of a gcl function; call the saved original | observe and change any gcl function, direct or virtual | a base-class hook runs for every subtype that doesn't override; hooking an override covers only that subtype. One owner per cell |
| **Game-function calls** | call the game's own create/destroy/damage/store/set functions on the owning thread | create and destroy objects, apply changes that replicate | needs correct ownership (`ref<T>`), the right thread and server authority |
| **Definition JSON** | add entries to the definition containers right after `<container>.load` | new items, components, props, creatures, zombies, tiles that reuse **existing implementation classes** | new behaviour classes are closed: they would need new gcl types and typeinfo. Every peer must load identical definitions |
| **Sidecar files** | mod-owned files keyed by save name, peer identity or object id | persist mod state | ids may be remapped on load (`save_data.id_map`). Store the mapping you observe after load |

## Construction, registration, ownership, cleanup and authority

- **Construction.** Allocate `size` bytes and call the type's `ctor` (`types.json`) for types with
  `nontrivial_lifetime`. Heap objects the game keeps must come from the game's allocator (`$ref_ctor_alloc`) and be
  handed over as `ref<T>`. That handover is unresolved today; see known gaps item 3.
- **Registration.** Only data-driven registries are open: the definition containers, plus localization tables loaded
  from `rom/*.tsv`, as far as static analysis shows. Enums (`e_*`), message types, save type ids and replicated
  object types are closed sets compiled into game.gcl.
- **Ownership.** A `ptr<T>` returned by the game is borrowed for the current tick. A `ref<T>` returned by the game
  is yours to release (`$ref_dtor`).
- **Cleanup.** Remove hooks in reverse order of installation. Drop all per-world state in the
  `server_scene.destroy` / `client_scene.destroy` hooks before calling the original. Plugins loaded by AnyAPI live
  for the whole process (no hot unload).
- **Authority.** Anything under `server_scene` / `server` exists only on the host and is authoritative. Clients see
  replicated mirrors (`client_scene`). A client-side change to a replicated mirror is overwritten by the next
  replication update; route it through the host instead.

## Replication of custom state: closed

The protocol has a fixed message set (`messages_server_to_client.e_type`, `messages_client_to_server`) and fixed
replicated object and property types. A mod cannot add a message or a replicated property without every peer
running the same modified code. A vanilla peer would receive unknown type ids and desync or disconnect.
Options, in order of preference:

1. Keep mod state host-only and derive client visuals locally from replicated vanilla state.
2. Require the mod on all peers and run a separate mod channel outside the game protocol (own socket or Steam
   networking). This is outside this SDK.
3. Chat messages are the only free-form string channel in both directions. Using them for data would be visible to
   vanilla players and is not recommended.
