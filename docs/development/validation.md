# Compatibility and validation

All current packages target Windows x64, Anymaker **0.1.23** / Steam build
**25755694**. [`BUILD_MANIFEST.json`](../../native/BUILD_MANIFEST.json) records the
exact `game.exe` and `bin/game.gcl` hashes. The loader refuses to enable native
hooks on any other build.

## Stable release

The stable catalog ([`catalog.json`](../../catalog.json)) provides:

| Package | Version | Released in | Automated checks at release |
| --- | --- | --- | --- |
| AnyAPI | 0.32.0 | [0.32.0 notes](../releases/release-0.32.0.md) | 47 |
| AnyMap, AnyGraphics | 0.27.2, 0.29.3 | [2026-10-08 SDK and mod update](../releases/sdk-and-mods-2026-10-08.md) | 48 |
| AnyQuickWheel | 1.1.0 | [0.31.0 notes](../releases/release-0.31.0.md) | 47 |
| AnyClock | 1.0.0 | [0.30.0 notes](../releases/release-0.30.0.md) | 42 |
| AnyHelpers, AnyInventory, AnyStorage | 0.27.0 | [0.27.0 notes](../releases/release-0.27.0.md) | 32 |

Each release's notes say what the author confirmed in game and what is still
pending. The headline items still open across the stable release:

- **Visual retest of 0.32.0 lighting** in a native world (published at the
  author's request before the retest).
- **AnyGraphics 0.29.3 retry fix** passed fixtures only; no new live visual check.
- **Controlled FPS measurements** for graphics, lighting and map rendering. No
  release claims a measured frame-rate cost or saving.
- **HUD control hints** (`hud.hints.<mod>`) did not appear in the author's live
  test and stay experimental.

## AnyAPI 0.34.0 and AnyBalance 1.1.0

Tank fluid mass passed native fixtures (server sampling, scene check, weighted centre,
stale-sample rejection) and the author confirmed in game that filling and draining a
tank changes the mass and moves the marker as expected.

## AnyAPI 0.33.0 and AnyBalance 1.0.0

The release build passed **51 native checks** on Windows and the eight experimental
SDK examples compile. The author confirmed in a locally hosted world that the
AnyBalance marker stays anchored, targets a bare chassis, plate edge, door handle and
tyre, hides when looking away or switching tools, leaves Properties actions working
and does not slow the simulation. Keep untested development builds out of
`catalog.json`.

## Multiplayer coverage

Almost all gameplay acceptance so far was done while **hosting a world locally**.
Local-host success does not establish joining-client or dedicated-server behavior.

| Area | Local host | Joining client | Dedicated server |
| --- | --- | --- | --- |
| Inventory actions (AnyInventory Add) | Confirmed | Permissions and rejection not verified | Not tested |
| Storage transfers and sorting (AnyStorage) | Confirmed | Not verified | Not tested |
| Session mode | Confirmed | Not asserted | Not tested |
| Equipment wheel (AnyQuickWheel) | Confirmed | Not verified | Not tested |
| Creation balance (AnyBalance) | Connected bodies merged | Targeted body only; topology not exposed to clients | Not tested |
| Rendering, menus, map, clock | Confirmed | Client-side; not separately tested | Not applicable |
| Experimental SDK probes | Hosted-world probe done | Pending ([known gaps](../../sdk/experimental/known-gaps.md)) | Not tested |

Requests that go through native replicated events (inventory, storage, hotbar)
report "submitted", not "server confirmed". Treat a missing confirmation as unknown.

## Published resources

Manager 1.3.2 bundles the API 0.33.0 archive and a matching offline guide with 35
headers and 124 guide articles; newer API and mod versions come from the catalog.
Its fixture suite passes 94 checks.
The manager fixture suite (`--self-test`) covers installation rollback, local mod
discovery, launch modes and verified EXE self-updates. Public download checks
compare each archive and DLL checksum.

## Rechecking a build

Use the commands in [Building](building.md). Inspect failures before packaging.
Do not change a game fingerprint without reviewing the corresponding native
bodies, dependencies and layouts. Validate joining-client and server operations
separately, and measure graphics cost in a fixed scene with effects on and off.

The repository also runs a Linux check on every push and pull request
(`.github/scripts/check_repo.py`). It validates Markdown links, text encoding,
`catalog.json` and `manager-update.json`. It does not build or test native code;
that still needs Windows and the installed game.

## History

Older acceptance records, test reports and evidence JSON are kept under
[releases](../releases/README.md). Highlights:

- **October 6, 2026 (0.27.0):** local gameplay acceptance for the first five mods
  and the manager. Runtime logs confirmed the build guard, plugin initialization,
  storage moves, map routes and graphics settings. The 0.1.23 profile audit
  reviewed 112 patterns and refreshed 17.
- **October 7, 2026 (0.29.0):** the author confirmed visible volumetric fog, sun
  shafts and local-light scattering and authorized publication. All 39 native
  checks passed at the time, including 54 synthetic D3D12 lighting cases.
  Spotlights use matching native shadows; point lights without shadow maps stay
  unshadowed. No temporal AA or temporal volumetric accumulation is advertised.
