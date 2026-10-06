# AnyAPI 0.27.0 candidate

Targets Anymaker 0.1.23, Steam build 25755694. Existing plugin ABI 1 is preserved.

## Changes

- Refreshed 17 exact native patterns after auditing 112 patterns against current
  game.gcl. Changed bodies retain instruction shape (except RIP-relative data
  offsets), sizes and documented dependency sets. Runtime dependency checks
  remain enabled; matching file hashes alone do not enable unresolved hooks.
- Added copied build identity through anyapi.build v1.
- Added owned, bounded client-tick scheduling through anyapi.client_tasks v1.
- Added an internal call-cell primitive with original-before-publication ordering,
  conditional restoration and writable/alignment checks. It is a foundation for
  future generated bindings, not a public arbitrary pointer hook service.
- Added a repeatable profile audit tool and tests for hook conflicts, publication,
  task limits, cancellation, stale epochs and provider ownership.

## Validation status

Automated results and package fingerprints are recorded with the candidate.
The complete 32-check suite passes, and the client-task SDK example compiles.
Menu/world gameplay acceptance on 0.1.23 remains pending. Stable catalog and
manager downloads must not claim acceptance merely because this candidate builds.

Server scheduling, new object creation, reference ownership and custom replication
are not newly exposed in this iteration.

The local candidate includes the API loader, separately packaged optional mods,
matching SDK headers, an offline guide, file checksums and the test result record.
It is not installed automatically. Manager receipts and the stable download feed
still describe their published versions.

## Mod compatibility update

AnyHelpers, AnyInventory, AnyStorage, AnyMap and AnyGraphics are rebuilt as 0.27.0
compatibility candidates. Existing features and saved settings are retained.
Each DLL now includes readable Windows product/version information. Separate
manager-compatible packages declare the current game fingerprints and API 27
requirement; the stable online catalog remains unchanged during validation.

The candidate installer uses the manager's checksum-checked transactions, migrates
imported copies of these same mods to package identities, and backs up the loader,
mod files and receipts together. A failed batch restores that snapshot. The
installer is rehearsed on a copy before use against the real game directory.

The local installation was upgraded and all six DLLs matched their package
checksums. All 3,540 previously existing non-DLL framework files were preserved.
A forced mid-batch downgrade rejection restored the copied installation's original
DLLs and receipts. These installation checks do not replace in-game validation.
