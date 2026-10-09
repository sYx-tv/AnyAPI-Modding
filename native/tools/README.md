# Native inspection tools

These utilities support reviewed native contracts. The normal build needs only
`native/generate_road_graph.py`; see [Building](../../docs/development/building.md).

## Inspection and verification

| Tool | Purpose | Required inputs |
| --- | --- | --- |
| `gcl_metadata.py` | Library for reading `bin/game.gcl` records, used by the other tools | Compatible game.gcl |
| `parse_gcl.py` | Parse native code records | Compatible game.gcl and output path |
| `inspect_native.py` | Decode selected native bodies | Game data and parsed records |
| `game_update.py` | Port to a new game build in one run: SDK, pattern audit, legacy routes, mod bindings, build identity. See [game updates](../../docs/development/game-updates.md) | Game files, previous SDK root; Capstone, pefile |
| `game_update_audit.py` | Sort every byte pattern into unchanged/refreshable/shifted/resized/changed/moved/stale/missing; `--apply` refreshes the safe ones | Game files, previous SDK JSON; Capstone, pefile |
| `refresh_legacy_routes.py` | Regenerate the legacy event-route offsets and dispatcher bytes | game.gcl |
| `rebind_mod_bindings.py` | Regenerate the experimental-SDK mod bindings from a new SDK | New SDK root |
| `add_game_build.py` | Add a new game fingerprint to catalog mods whose DLL did not change; print `release.json` overrides | Catalog, new hashes |
| `refresh_native_profile.py` | Audit recorded patterns against a new game build; `--apply` updates only unchanged-shape bodies | Previous SDK JSON, game.gcl, output path; Capstone |
| `verify_menu_contracts.py` | Check menu dependency patterns | Game data, parsed records and `MENU_NATIVE_CONTRACTS.json` (see note) |
| `verify_inventory_contracts.py` | Check inventory layouts and patterns | Game data and parsed records |
| `verify_player_math.py` | Check reviewed camera/player math | Game data and reviewed contract records |
| `audit_current_build.py` | Gather build evidence | Framework, executable, game data and records |
| `review_log.py` | Summarize runtime logs | Log and output path |
| `review_network_evidence.py` | Review captured network observations | Logs and a matching build identity |
| `build_manual.py` | Reconcile historical API audits | Historical audit/coverage datasets |

Some inspection tools require Python Capstone and locally generated audit datasets.
Those datasets and proprietary game files are not included. Historical audit tools
are reference utilities, not automated compatibility repair or active API services.
Pass explicit inputs where supported and inspect each script's argument declarations
before running it. None of these checks substitutes for gameplay validation.

**Missing inputs from a fresh clone.** `verify_menu_contracts.py` reads
`native/MENU_NATIVE_CONTRACTS.json`, and the `gcl_metadata.py` docstring points to
`gcl_format.md`. Neither file is in this repository, so the menu verifier does not
run from a fresh clone. The reviewed menu bytes themselves are in
`native/anyapi_menu_patterns.h`.

## Packaging

| Tool | Purpose | Notes |
| --- | --- | --- |
| `package_source.py` | Create a source-only archive | Destination ZIP path |
| `package_experimental_sdk.py` | Build `AnyAPI-Experimental-SDK.zip` | `--reference` pointing at the extracted reference data |
| `package_*_candidate.py`, `package_mod_updates.py` | Local test packages for one specific release | Historical; see below |

The candidate packagers were written for one release each. They hard-code that
release's version, its expected test count (for example 32, 35, 39, 42 or 47) and
a test record under `build/`. `package_balance_candidate.py` also copies
published ZIPs from a sibling `graphics_clear_air_release` checkout. None of them runs from a fresh clone.
Use them as a template, and follow [Publishing](../../docs/development/publishing.md)
for real releases.
