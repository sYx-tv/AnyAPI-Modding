# Compatibility and validation

## Current release

AnyAPI and all five mods are released as 0.27.0 for Windows x64, Anymaker
0.1.23 / Steam build 25755694. The build manifest records exact executable and
game-data hashes. The complete native suite passes 32 checks covering contracts,
menus, inventories, storage, map rendering and graphics shader output.

Local gameplay acceptance was confirmed by the author on October 6, 2026 for
all five mods and the manager. Runtime logs confirmed the build guard matched,
all five plugins initialized, storage moves were confirmed, map routes were
selected and graphics settings were applied. Joining-client and dedicated-server
acceptance require separate coverage; local host success does not establish them.

The profile audit reviewed 112 patterns and refreshed 17. New build identity and
client task services have automated coverage; the client task example compiles.
Gameplay confirmation does not imply every API service was individually exercised.
Graphics tests check shader output and settings behavior; they do not establish
GPU/FPS cost across different hardware.

## Published resources

The stable catalog provides API 0.27.0 and five independent mod downloads.
Manager 1.3.1 bundles the API-only archive and matching offline guide, with 25
headers and 100 guide articles. The manager fixture suite passes 93 checks,
including installation rollback, local mod discovery, launch modes and verified
EXE self-updates. Public download checks compare each archive and DLL checksum.

## Rechecking a build

Use the commands in [Building](building.md). Inspect failures before packaging.
Do not change a game fingerprint without reviewing the corresponding native
bodies, dependencies and layouts. Validate joining-client/server operations
separately and measure graphics cost in a fixed scene with effects on and off.

Historical records are retained under [releases](../releases/).
