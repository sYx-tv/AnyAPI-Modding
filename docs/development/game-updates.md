# Porting to a new game build

AnyAPI only runs on the exact Anymaker build it was reviewed for: the loader compares
the SHA-256 of `game.exe` and `bin/game.gcl` with
[`runtime_build_identity.h`](../../native/runtime_build_identity.h) and turns itself off
on any other build. When Steam updates the game, work through this page. Most of it
is one script; the rest is review, a Windows build and an in-game test.

The last port, 0.1.24 / Steam build 25826614 to 0.1.25 / Steam build 25904830
(API 0.36.0 to 0.37.0), is the worked example below. Its SDK root is kept in
the project files as `sdk/reference-0.1.25`, the previous SDK for the next update.

## 1. Capture the new build

You need the new `game.exe`, `bin/game.gcl` and Steam's
`steamapps/appmanifest_4435340.acf`. Never commit them. Either point the tools at the
Steam install, or copy the three files into a folder outside the repository
(`game.exe`, `bin/game.gcl`, and pass the manifest with `--manifest`).

You also need the SDK root of the build the repository targets now: the folder with
`json/`, `tools/` and `validation/`. For 0.1.23 that is `sdk/` from the
`sdk-reference-2026.10.08` release. For later builds it is the `<work>/sdk` folder
from the previous run of the script below; keep it.

From 0.2.0 the game ships a stripped `game.gcl` (format 2): function records keep only a
64-bit FNV-1a hash of their signature, and type records lose their field lists. Keep the
previous build's `game.exe` and `bin/game.gcl` too, and pass that folder as
`--previous-build`. The reader ([`gcl.py`](../../sdk/experimental/tools/gcl.py)) then
names functions by matching hashes against the previous SDK (`ANYMAKER_GCL_NAMES`, set
from `--previous-sdk`), and
[`infer_stripped.py`](../../sdk/experimental/tools/infer_stripped.py) recovers what the
hashes can't: signatures that changed by one parameter or type (a one-edit search over
known types), and field layouts of types whose size changed (by lining up how the old and
new code address each field in the same functions). Its output,
`<work>/sdk/json/stripped_inferred.json`, is read through `ANYMAKER_GCL_INFERRED`; fields
it fills carry the basis `carried` (type kept its size, align and parent) or `inferred`.
Treat inferred offsets as strong evidence, not proof. Runtime-only natives whose
neighbours moved are found in the new `game.exe` by their masked code bytes.

Requirements: Python 3 with `pip install capstone pefile`. The script runs on Windows
or Linux.

## 2. Run the update script

```powershell
python native/tools/game_update.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Anymaker" `
    --previous-sdk <previous SDK root> --previous-build <previous game files> `
    --work build/game-update --api-version 0.37.0 --api-revision 40
```

Run it once without `--apply` to read the report, then again with `--apply` (add
`--skip-sdk` to reuse the SDK it just generated). It:

1. identifies the build: the version string in `game.exe`, the Steam build id from the
   app manifest, and both SHA-256 hashes;
2. regenerates the experimental SDK into `<work>/sdk`
   ([`regenerate.py`](../../sdk/experimental/tools/regenerate.py)). Runtime evidence
   from the old build is set aside. Runtime-only native RVAs (`$string_*`, `$ref_*`)
   are carried over when the nearest static natives on both sides kept their RVAs or all
   moved by the same amount, including RVAs that were themselves carried last time
   ([`carry_natives.py`](../../sdk/experimental/tools/carry_natives.py));
3. audits every exact byte pattern in `native/`
   ([`game_update_audit.py`](../../native/tools/game_update_audit.py)) and, with
   `--apply`, rewrites the ones that are safe to refresh;
4. regenerates the legacy event-route offsets and dispatcher bytes
   ([`refresh_legacy_routes.py`](../../native/tools/refresh_legacy_routes.py));
5. re-binds EngineSound, AnyMirror, AnyLights, GpsPart, ThirdPerson and CustomGame from the new SDK
   ([`rebind_mod_bindings.py`](../../native/tools/rebind_mod_bindings.py));
6. copies the generated SDK headers and system docs into `sdk/experimental/`;
7. writes the build identity: `runtime_build_identity.h`, `runtime_build_guard.inc`,
   `BUILD_MANIFEST.json` and, with `--api-version`, the AnyAPI DLL version in
   `native/CMakeLists.txt`;
8. with `--api-version`, plans the release
   ([`port_release_plan.py`](../../native/tools/port_release_plan.py)): every catalog mod
   that must be rebuilt (it uses the experimental SDK, or includes a file whose patterns
   the port rewrote) gets a patch version bump with `--apply`, and
   `<work>/release-plan.json` lists the tag, every package to ship, the `release.json`
   `Packages` overrides and the `add_game_build.py` command for the other mods.
   Bundles count as one mod each, under their release name: a bundle is rebuilt when any
   of its parts must be, ships from `build/bundles/<config>/`, and gets its own version block
   if it had none. A standalone target with the same release name (AnyHelpers, AnyGraphics,
   AnyEngineParts, AnyMap) is never released, because the bundle replaces it. Before 0.1.27
   the plan ignored bundles and bumped the standalone versions instead.

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

The report ends with **Offsets to check**: number literals in our code that equal a
dependency cell (`code_end + 8*index`) of a function whose size changed, in files that use
that pattern. Each one is probably a cell that must move by the size change. Write such
offsets as `NAME_END + index*8` with a `NAME_END` constant next to the pattern (for
example `ST_CONTAINER_END`): `--apply` rewrites `NAME_END` constants of resized and
shifted patterns, so the offsets follow by themselves.

Then check these, which the audit cannot see:

- **Field offsets in C++.** Compare layouts between the two SDKs
  (`<work>/sdk/reports/diff-previous-to-current/README.md` lists every type whose layout
  changed). Search `native/` for hard-coded offsets into those types. In 0.1.24 the
  changed types were `settings.controls` (+8 from the new `m_bindings_version`),
  `client` after offset 3768, `client_scene.actor_character` after 5088,
  `main_menu`, gamepad data and creatures. None of AnyAPI's or the mods' offsets fell
  in the moved ranges.
- **Field offsets of the peer.** 0.1.25 added a tooltip field to the peer's inspection
  data, which moved `client_scene.peer` fields after 2960 by 16 and the server peer's
  authority actor from +0x1998 to +0x19d8 (seen in the `changed` diff of a legacy event
  body). `native_players.inc` and `legacy_server_use_resolution.inc` read those.
- **Hook slots.** Wherever code validates a dependency cell and then replaces a cell, both offsets
  must move together. In 0.35.0 the inventory title dependency moved to `+0x6430` but its hook stayed
  on `+0x6438` (now a one-argument helper), which crashed the game when a container inventory
  opened. `inventory_ui::prepare` now refuses to hook a cell that does not lead to the validated function.
- **Twin functions.** Identical editor-tool functions can diverge by a byte between builds. In
  0.1.24 AnyBalance's `HOVER_OVERLAY` bytes went on matching the microcontroller tool instead
  of the Properties tool, so the API hooked the wrong tool; the audit now reports that as `changed`.
  In 0.1.25 the two became byte-identical, so AnyBalance's unique-match check failed; the audit
  now also reports a pattern that gains an identical twin as `changed` with a `TWINS` note.
  In 0.1.27 the Properties overlay changed again and the old bytes matched only the microcontroller
  twin; the audit now reports a pattern that loses one of its former twins as `changed` too.
- **Enum values** the mods hard-code (`e_localization_string`, `e_audio_effect`): check
  them in the new `json/types.json`.
- **`game.exe` addresses** that are not byte patterns, such as the graphics context
  and command-list offset in `anyapi_scene_antialiasing.inc`. In 0.1.24 the context
  moved from RVA 0x5e3340 to 0x5e5340 and the command list from +0x1d48 to +0x2648; in
  0.1.27 to 0x5e23d0 and +0x2650. Find them again through their accessor and a caller, then update
  [scene antialiasing](scene-antialiasing.md). Natives called through a plain lower-case
  constant (`play_voice_rva = 0x...` in the engine-sound mods) are checked by the audit when it
  gets the new SDK's `--current-json` (the script passes it): `moved` rows, rewritten by `--apply`.
  Before 0.1.27 nothing checked them, and every one of them moved. The graphics context
  literal is still a manual check.
- **Hand-written signatures** outside the `*_bindings.h` headers (`map_actors.h`,
  `route_cell.h`, `rotary_mod.cpp`) are audited as `hexsig` rows since 0.1.27, when AnyMap's
  creature `code_sig` turned out to have matched nothing for two builds. A signature split
  over several string literals is reported but never rewritten by `--apply`.
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

Then install the candidate, launch Anymaker from Steam (the manager's Play gate refuses an
unreleased API) and test each mod in game. Always open a storage container's inventory:
the 0.35.0 port crashed there. The first launch log
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
  python native/tools/add_game_build.py --version 0.1.25 --steam-build 25904830 `
      --exe <game.exe sha256> --gcl <game.gcl sha256> --skip anymirror,enginesound,gpspart,thirdperson --apply
  ```

  `<work>/release-plan.json` prints this exact command and holds the `Packages`
  overrides for the releases above.
- The manager downloads whatever API the live catalog lists, so it needs no release for
  a game update. Its bundled API (the offline fallback) refreshes with the next manager
  release: copy the root `catalog.json` to `manager/publishing/catalog.json`, put the API
  ZIP in `manager/publishing/assets/` and run `python manager/prepare_guide.py`
  ([manager README](../../manager/README.md)).
