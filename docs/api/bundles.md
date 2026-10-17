# Bundles (0.39.0)

A bundle is one mod DLL that carries several mods, called parts. AnyAPI 0.39.0 and
newer loads each part as if it were its own DLL: every part gets its own host
structure, callbacks, GPU renderer and texture budget, input callback and input
capture, menu sections, published services and fault isolation. A part that throws
is disabled alone; the bundle's other parts keep running.

The shipped bundles are built from the standalone mods' sources, unchanged, by the
files in [native/bundles](../../native/bundles/):

| Bundle DLL | Parts (mod id) |
| --- | --- |
| `AnyEngineParts.dll` | `anyengineparts` (turbo, superchargers, rotary, Dyno), `enginesound`, `gpspart` |
| `AnyHelpers.dll` | `anyhelpers` (required), `anyinventory`, `anymirror`, `anybalance`, `mccode` |
| `AnyGraphics.dll` | `anygraphics`, `thirdperson` |
| `AnyMap.dll` | `anymap` |

Parts keep their mod ids, settings ids, keybind ids, data folders and game part ids,
so settings, keybinds, saved vehicles and worlds carry over from the standalone mods.

## Writing a bundle

Header: `anyapi_bundle_v1.h`. Export one function that returns a static part table:

```cpp
extern "C" __declspec(dllexport) uint32_t AnyAPI_BundleParts(const AnyBundlePartV1** parts);
```

Each `AnyBundlePartV1` gives the part's `id` (the same id its `ModInit` returns), a
display `name`, its `init` and optional `ready` functions, an optional `legacy_file`
and `flags`. At most 16 parts per bundle; every part takes one of AnyAPI's 32 mod
slots. Parts start in table order, with the usual two phases: `ModInit` for every
part of every DLL, then `ModReady`.

AnyAPI prefers `AnyAPI_BundleParts` over `AnyAPI_ModInit`. Older AnyAPI builds only
call `AnyAPI_ModInit`, so a bundle exports one that logs that it needs 0.39.0 and
returns false. [`bundle_parts.h`](../../native/bundles/bundle_parts.h) has macros
for the table and both exports, and explains how to add a mod to a bundle.

All parts share one DLL, so their sources must not define the same non-namespaced
symbol, or more than one `DllMain` (the part wrappers rename it).

## When a part does not load

- **The old standalone DLL is still installed.** If `legacy_file` (for example
  `AnyInventory.dll`) exists in the mods folder, or a loaded mod already uses the
  part's id, AnyAPI skips the part and the old DLL runs instead, so nothing loads
  twice. The log says `BUNDLE_PART_SKIPPED ... reason=standalone_dll_installed`.
  The AnyAPI Manager (1.5.0) removes the old DLLs when it installs the bundle.
- **The player switched it off.** The log says `reason=switched_off`. Parts
  flagged `ANY_BUNDLE_PART_REQUIRED` cannot be switched off.

## Service `anyapi.bundle_parts` version 1

Lists every part AnyAPI found this launch and switches parts on or off for the next
launch. Callable from any thread.

| Call | Result |
| --- | --- |
| `count()` | Number of parts found, loaded or not |
| `copy(index, AnyBundlePartInfoV1*)` | The part's `id`, `name`, `bundle` (DLL name without `.dll`), `state` (LOADED, OFF, LEGACY, FAILED), `flags` and `enabled_next` |
| `set_enabled(id, enabled)` | Saves the switch for the next launch; false for unknown or required parts, or when saving failed |

Switched-off parts are saved in `AnyAPI and Modding/bundle-parts.tsv`, one
`<id>` TAB `0` line each. Hooks and services cannot be unloaded, so a switch applies
after a restart.

AnyHelpers uses this service to show an **Enabled** row at the top of each
switchable part's group in Mod Settings. The saved row is the truth: Apply passes it
to AnyAPI.
