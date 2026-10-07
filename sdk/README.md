# AnyAPI developer SDK

Public declarations are in [include](include/). Working examples are in
[native/examples](../native/examples/). The [service index](../docs/api/README.md)
links each integration surface to its contract and header.

Mods are Windows x64 C++ DLLs exporting `AnyAPI_ModInit`. Check the host ABI and
structure size. Resolve versioned services during `AnyAPI_ModReady`, after providers
have initialized. Optional providers may be absent; keep working defaults.

This SDK is the **AnyAPI mod interface**, not the game's original source or a
complete game-engine SDK. Native pointers remain inside the framework; public
services use copied data, owned tokens and guarded native operations.

`anymaker_mod_api.h` and `anymaker_mod_extension.h` are retained legacy references.
Their historical native action/hook registry is disabled in the current loader.
They should not be treated as active services merely because declarations exist.

See [First mod](../docs/development/first-mod.md) for a minimal build and
[Build instructions](../docs/development/building.md) for the complete projects.

AnyAPI 0.29.0 includes `anyapi.scene_lighting` v1/v2 for HDR fog, sun
shafts and local-light scattering before the HUD. See [Scene lighting](../docs/scene-lighting.md)
for the public policy contract, tested behavior and limitations.

AnyAPI 0.30.0 adds `anyapi.world_time` v1 for copied native day/night time. See [World time](../docs/api/world-time.md). The native clock readout was confirmed in a loaded world.

AnyAPI 0.31.0 adds `anyapi.equipment` v1/v2 for copied hotbar and carried construction-tool snapshots, with queued native hotbar assignment/selection. See [Equipment](../docs/api/equipment.md). The inventory wheel and equipping were confirmed in a loaded world.
