# Porting to a new game build

AnyAPI only runs on the exact Anymaker build it was reviewed for: the loader compares
the SHA-256 of `game.exe` and `bin/game.gcl` with
[`runtime_build_identity.h`](../../native/runtime_build_identity.h) and turns itself off
on any other build. When Steam updates the game, work through this page. Most of it
is one script; the rest is review, a Windows build and an in-game test.

The last port, 0.1.23 / Steam build 25755694 to 0.1.24 / Steam build 25826614
(API 0.34.1 to 0.35.0), is the worked example below.

## 1. Capture the new build

You need the new `game.exe`, `bin/game.gcl` and Steam's
`steamapps/appmanifest_4435340.acf`. Never commit them. Either point the tools at the
Steam install, or copy the three files into a folder outside the repository
(`game.exe`, `bin/game.gcl`, and pass the manifest with `--manifest`).

You also need the SDK root of the build the repository targets now: the folder with
`json/`, `tools/` and `validation/`. For 0.1.23 that is `sdk/` from the
`sdk-reference-2026.10.08` release. For later builds it is the `<work>/sdk` folder
from the previous run of the script below; keep it.

Requirements: Python 3 with `pip install capstone pefile`. The script runs on Windows
or Linux.

## 2. Run the update script

```powershell
python native/tools/game_update.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Anymaker" `
    --previous-sdk <previous SDK root> --work build/game-update `
    --api-version 0.35.0 --api-revision 36
```

Run it once without `--apply` to read the report, then again with `--apply` (add
`--skip-sdk` to reuse the SDK it just generated). It:

1. identifies the build: the version string in `game.exe`, the Steam build id from the
   app manifest, and both SHA-256 hashes;
2. regenerates the experimental SDK into `<work>/sdk`
   ([`regenerate.py`](../../sdk/experimental/tools/regenerate.py)). Runtime evidence
   from the old build is set aside. Runtime-only native RVAs are carried over only when
   the nearest static natives on both sides kept their RVAs
   ([`carry_natives.py`](../../sdk/experimental/tools/carry_natives.py));
3. audits every exact byte pattern in `native/`
   ([`game_update_audit.py`](../../native/tools/game_update_audit.py)) and, with
   `--apply`, rewrites the ones that are safe to refresh;
4. regenerates the legacy event-route offsets and dispatcher bytes
   ([`refresh_legacy_routes.py`](../../native/tools/refresh_legacy_routes.py));
5. re-binds EngineSound, AnyMirror and AnyLights from the new SDK
   ([`rebind_mod_bindings.py`](../../native/tools/rebind_mod_bindings.py));
6. copies the generated SDK headers and system docs into `sdk/experimental/`;
7. writes the build identity: `runtime_build_identity.h`, `runtime_build_guard.inc`,
   `BUILD_MANIFEST.json` and, with `--api-version`, the AnyAPI DLL version in
   `native/CMakeLists.txt`.

Re-running it is safe: a second `--apply` on an up-to-date tree changes nothing.

## 3. Review by hand

Open `<work>/audit/pattern_audit.md`. Each pattern has one state:

| State | Meaning | What to do |
| --- | --- | --- |
| unchanged | Exact bytes still match | Nothing |
| refreshable | Same code; only pool displacements moved | Applied by the script |
| shifted | Same instruction layout, so return-address and call-site offsets hold; struct, stack or enum numbers changed | Applied by the script. Check each **offset hint**: our code may hard-code one of the old numbers. Most hints are coincidences (UI sizes, colours) |
| resized | Same code, different size | Bytes applied. Every `code_end`-relative offset used with it (dependency cells `code_end + 8*index`, `*_END`) must move by the size change |
| changed | Instructions or dependencies changed, or the old bytes now match a different function (the note names both) | Read `<work>/audit/diffs/`; re-derive the pattern and any offsets from the new function |
| moved | `game.exe` code at a new RVA | Update the `*_RVA` constant after confirming the function |
| stale | Did not match the previous build either | Nothing new; legacy telemetry |
| missing | Not found, no previous function identified | Usually stale; check the name |

Then check these, which the audit cannot see:

- **Field offsets in C++.** Compare layouts between the two SDKs
  (`<work>/sdk/reports/diff-previous-to-current/README.md` lists every type whose layout
  changed). Search `native/` for hard-coded offsets into those types. In 0.1.24 the
  changed types were `settings.controls` (+8 from the new `m_bindings_version`),
  `client` after offset 3768, `client_scene.actor_character` after 5088,
  `main_menu`, gamepad data and creatures. None of AnyAPI's or the mods' offsets fell
  in the moved ranges.
- **Hook slots.** Wherever code validates a dependency cell and then replaces a cell, both offsets
  must move together. In 0.35.0 the inventory title dependency moved to `+0x6430` but its hook stayed
  on `+0x6438` (now a one-argument helper), which crashed the game when a container inventory
  opened. `inventory_ui::prepare` now refuses to hook a cell that does not lead to the validated function.
- **Twin functions.** Identical editor-tool functions can diverge by a byte between builds. In
  0.1.24 AnyBalance's `HOVER_OVERLAY` bytes went on matching the microcontroller tool instead
  of the Properties tool, so the API hooked the wrong tool; the audit now reports that as `changed`.
- **Enum values** the mods hard-code (`e_localization_string`, `e_audio_effect`): check
  them in the new `json/types.json`.
- **`game.exe` addresses** that are not byte patterns, such as the graphics context
  and command-list offset in `anyapi_scene_antialiasing.inc`. In 0.1.24 the context
  moved from RVA 0x5e3340 to 0x5e5340 and the command list from +0x1d48 to +0x2648.
  Find them again through their accessor and a caller, then update
  [scene antialiasing](scene-antialiasing.md).
- **Changed game.exe code** (`changed` rows naming a `*_RVA` constant): find the
  function again by name in `<work>/sdk/json/natives.json` or by its callers.

## 4. Versions and docs

- Bump the experimental-SDK mods, because their DLLs embed the build hashes
  (`anyapi_experimental.hpp`): give each released one a patch version in
  `native/CMakeLists.txt`.
- Mods that only use AnyAPI services do not change. Their existing ZIPs get the new
  game fingerprint in the catalog at release time.
- Update the "current target" lines in the README and docs (search for the old build
  id). Leave release notes and history as they were.
- Keep the new `<work>/sdk` folder: it is the previous SDK for the next update.

## 5. Build, test and release

On Windows, from a fresh clone (see [Building](building.md)):

```powershell
cmake -S native -B build -A x64 -DANYAPI_GAME_DIRECTORY="$gameDirectory"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Then install the candidate and test each mod in game. The first launch log
(`anymaker_modding.log`) should show the build guard matching and every service
installing; a missing hook names its pattern.

At release (see [Publishing](publishing.md)):

- Publish the ported API as `vX.Y.Z`. Its `release.json` must override `GameBuilds`
  with the new fingerprint only, or it inherits the old one. Players who have not
  updated the game keep the previous API only through a manager that bundles it; the
  catalog then lists the new API alone.
- Publish the re-bound experimental-SDK mods in a `mods-` release with the same
  `GameBuilds` override.
- Add the new fingerprint to every unchanged mod:

  ```powershell
  python native/tools/add_game_build.py --version 0.1.24 --steam-build 25826614 `
      --exe <game.exe sha256> --gcl <game.gcl sha256> --release-json AnyAPI,EngineSound,AnyMirror --apply
  ```

  `--release-json` prints the `Packages` overrides for the releases above.
- The manager downloads whatever API the live catalog lists, so it needs no release for
  a game update. Its bundled API (the offline fallback) refreshes with the next manager
  release: copy the root `catalog.json` to `manager/publishing/catalog.json`, put the API
  ZIP in `manager/publishing/assets/` and run `python manager/prepare_guide.py`
  ([manager README](../../manager/README.md)).
