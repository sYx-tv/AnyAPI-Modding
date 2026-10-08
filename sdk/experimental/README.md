# Experimental game access

This layer exposes discovered Anymaker internals for advanced mod development,
including pathways that have not been exercised in a live game. It complements
the versioned [AnyAPI services](../../docs/api/README.md).

It targets **Anymaker 0.1.23, Steam build 25755694**. It is a reference and native
integration toolkit, not a claim that every declared operation works correctly.

## What mod authors can use

- Generated layouts, field offsets, inheritance, enums and virtual method slots.
- Function signatures, locating patterns, call-cell routes and native RVAs.
- Managers and registries, entity lookup, transforms, items and inventories.
- Vehicle, physics, mechanical, electrical, fluid and logic reference data.
- Player, input, audio, UI, rendering and asset integration pathways.
- World lifecycle, update hooks, save/load and networking reference data.
- Eight compile-checked examples covering lookup, transforms, lifecycle hooks,
  scheduling, UI, damage, definition registration and save sidecars.

The complete machine-readable reference is distributed in the release's
**AnyAPI-Experimental-SDK.zip**. Extract it to access `reference/json` and the
searchable documentation. Game executables and assets are not included.

## Generate bindings for a mod

From this directory, with Python 3 installed:

```powershell
python tools/bind.py --reference reference --match audio_manager --out generated/audio.hpp
python tools/bind.py --reference reference --match server_scene.vehicle --out generated/vehicles.hpp
python tools/bind.py --reference reference --all --out generated/all.hpp
```

`--signature` selects an exact overload. Each header has a companion JSON index
mapping its collision-resistant C++ symbols to the original signatures, known
thread information and locating availability. Records without a locating route
remain visible as unresolved; the generator does not invent an address.

Include `include/anyapi_experimental.hpp` in a Windows x64 C++17-or-newer mod.
Use `anymaker::experimental::function`, `hook_cell`, `global`, and `native` to
resolve entries after AnyAPI is ready and the game's JIT has loaded. These
helpers require both game file hashes to match the reference through the existing
`anyapi.build` service. This works with the published API; adding a generated
binding does not itself require a new API DLL.

Resolution can scan executable memory: resolve once during initialization, never
every frame. Null means unavailable or ambiguous. Runtime-generated objects and
their addresses still have scene and thread lifetimes; reacquire them as needed.

Read [Calling into the game](guides/calling-into-the-game.md) before invoking a
resolved address. Ordinary GCL calls pass arguments by pointer and use hidden
return storage; runtime helpers can differ. A C++ function-pointer cast is not an
ABI check. Use the exact signature, owner thread and authority requirements.

The lower-level imported runtime header remains available for standalone research.
Its direct helpers do not automatically perform the AnyAPI hash check; prefer the
guarded entry points above in distributed mods. Borrowed refs cannot be copied as
owned references, and server operations require the authoritative host.

## Evidence and limits

[Capabilities](capability_matrix.md), [system guides](systems/README.md), and
[known gaps](known-gaps.md) retain the reference author's metadata, static,
runtime and unknown evidence labels. Runtime-located does not mean call-tested.
The original snapshot's readiness labels are historical evidence, not a current
AnyAPI service guarantee.

Custom network messages, arbitrary replicated classes and shader-library creation
still have unresolved parts. The toolkit exposes what is known about them; it
does not provide fabricated working wrappers. Game updates require regenerating
and rechecking the affected bindings and layouts.

## Search the complete reference

```powershell
python tools/sdk_query.py --json reference/json --db reference/index.sqlite build
python tools/sdk_query.py --db reference/index.sqlite find "vehicle"
```

The reference was supplied for this project from the installed game. The runtime
helpers are maintained here with malformed-pattern, container bounds, null,
build-mismatch and hook-cell checks. Examples are compiled in the native build;
their live-game behavior remains experimental unless individually documented.
