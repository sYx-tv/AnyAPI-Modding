# Compatibility and validation

## Current source

The 0.27.0 candidate targets Windows x64, Anymaker 0.1.23 / Steam build
25755694. The [build manifest](../../native/BUILD_MANIFEST.json) records executable
and game-data hashes. The functional suite contains 32 automated checks covering
native contracts, menus, inventories, storage, map rendering and graphics.

The profile audit reviewed 112 patterns and refreshed 17. The new build identity
and client task services have automated coverage; the client task example also
compiles in the native build. In-game acceptance of the 0.1.23 candidate remains
pending. Earlier user confirmations below refer to earlier game profiles.

The existing four mods were confirmed working by the author in host gameplay.
The initial AnyGraphics effects were also confirmed working. The new compact
presets/settings interface awaits its next in-game check. Joining-client behavior
and server acceptance require separate coverage; host success does not establish them.

Graphics tests verify actual shader output, Apply/Cancel, preset parameters,
compact/advanced row counts and restoration of saved custom settings. They do
not establish visual preference or GPU/FPS cost on a player's hardware.

## Published resources

The stable catalog provides API 0.25.0 and four optional mods. Manager 1.2.0 bundles
that API profile and its offline guide. Its published evidence records 73 manager
checks plus seven anonymous download checks. Those results describe that release,
not future source changes.

## Rechecking a build

Use the build/test commands in [Building](building.md). Inspect failures before
packaging. Do not change a game fingerprint without reviewing the corresponding
native bodies, dependencies and layouts. Validate joining-client/server operations
separately, and measure graphics cost in a fixed scene with effects enabled/disabled.

Historical test and release records are retained under [releases](../releases/).
