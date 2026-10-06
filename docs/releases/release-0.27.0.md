# AnyAPI and mods 0.27.0

For Anymaker 0.1.23, Steam build 25755694, Windows x64. Plugin ABI 1 is preserved.

## Included downloads

- AnyAPI: the in-game framework, build guard, copied build identity and bounded client-tick scheduling.
- AnyHelpers: native Mod Controls and Mod Settings.
- AnyInventory: item lookup, previews, favorites and Add in Sandbox/Creative.
- AnyStorage: deposit/withdraw, matching-item transfers and storage sorting.
- AnyMap: world map, smooth minimap, placed markers and road guidance.
- AnyGraphics: graphics presets, bloom, sharpening and color controls.

Manager 1.3.1 bundles only the API, matching guide and source-only starter SDK.
Optional mods download separately. Existing Manager 1.3.0 users can choose
Settings > Check for updates > Update & restart.

## Compatibility work

The audit reviewed 112 native patterns and refreshed 17. Changed bodies retained
reviewed instruction shapes, sizes and dependency sets. Runtime dependency checks
remain enabled; matching file hashes alone do not enable unresolved hooks.
The internal call-cell primitive is groundwork for future generated bindings,
not a public arbitrary-pointer hook service.

## Validation

All 32 native checks and 93 manager checks pass. The author confirmed all mods
and the manager worked locally on October 6, 2026. Release DLLs match the installed
files used for testing. Logs recorded successful plugin initialization, confirmed
storage moves, map routing and graphics settings. The exact packaged map DLL also
passed initialization, rendering and visibility lifecycle fixtures.

Installation rehearsals preserved saved data and restored DLLs and receipts after
a forced mid-batch failure. Downloads contain only their declared DLL paths.
Joining-client and dedicated-server validation remain separate work. This release
does not add server scheduling, object creation, ownership or custom replication.
