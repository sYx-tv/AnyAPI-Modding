# AnyAPI 0.39.0 and the mod bundles

For Anymaker 0.1.25 (Steam build 25904830). Update the manager to 1.5.0 first, then update in the manager and restart the game.

Eleven mods are now four. Everything they did is still there, and each piece can still be switched off on its own.

| Mod | Now also carries |
| --- | --- |
| AnyEngineParts 1.0.0 | EngineSound 0.4.0, GpsPart 1.0.3 (with AnyEngineParts 0.2.1) |
| AnyHelpers 1.0.0 | AnyInventory 0.28.0, AnyMirror 1.1.1, AnyBalance 1.2.0, MCCode 0.2.0 (with AnyHelpers 0.28.0) |
| AnyGraphics 1.0.0 | ThirdPerson 0.1.3 (with AnyGraphics 0.31.0 and photo mode) |
| AnyMap 1.0.0 | AnyMap 0.28.0 with the minimap; ready to carry CustomMap and CustomGame later |

AnyClock, AnyQuickWheel, AnyStorage and AnyCombat stay separate.

## What happens to your mods

- **Installing a bundle removes the mods it replaces.** Manager 1.5.0 deletes the old DLLs (it keeps a backup) and drops them from the library, so you see four mods where there were eleven.
- **Nothing is lost.** Part ids, saved vehicles and worlds, keybinds and every mod's settings stay exactly as they were.
- **Switch pieces on and off** in Mod Settings: each bundled mod has an Enabled row under its own heading. The change applies after a restart.
- **No double loading.** If an old standalone DLL is still in the mods folder (an older manager, or a hand install), AnyAPI loads that one and skips the matching piece of the bundle.

## AnyAPI 0.39.0

- **Mod bundles.** A DLL can export `AnyAPI_BundleParts` to carry several mods. Each part gets its own slot, as if it were its own DLL: renderer, textures, input, menu sections, services and fault isolation. A part that fails is turned off alone. See [Bundles](../api/bundles.md) and `anyapi_bundle_v1.h`.
- **`anyapi.bundle_parts` service** lists every part found this launch and switches parts on or off for the next launch. The choice is saved in `AnyAPI and Modding/bundle-parts.tsv`.
- **Menu v4** (`anyapi_menu_v4.h`), used by AnyHelpers' Favorites and ADMIN tabs.
- Catalog revision 43. Mods that need 0.39.0 set `MinimumApi` 43.

## AnyHelpers 1.0.0

Besides the pieces above, this brings AnyHelpers 0.28.0:

- **Favorites** in the Duplicate tool's load screen.
- A host **ADMIN** tab: heal, teleport, time, weather and clear zombies.
