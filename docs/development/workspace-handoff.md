# Workspace handoff

Updated 8 October 2026. The repository contains the API, optional mods, manager source, SDK headers, tests and maintained documentation. A current clone is enough for the source; the installed game and large research downloads are separate.

## Current state

- Stable downloads: API 0.34.0, AnyBalance 1.1.0, AnyMap 0.27.2, AnyGraphics 0.29.3, AnyClock 1.0.0, AnyQuickWheel 1.1.0, and Helpers/Inventory/Storage 0.27.0. Manager 1.3.2 has its own update channel.
- **API 0.34.0 and AnyBalance 1.1.0** add tank fluid mass ([v0.34.0](https://github.com/sYx-tv/AnyAPI-Modding/releases/tag/v0.34.0), user-confirmed in game). **API 0.33.0 and AnyBalance 1.0.0** were released earlier on 8 October 2026 ([v0.33.0](https://github.com/sYx-tv/AnyAPI-Modding/releases/tag/v0.33.0)). The bare-body targeting correction (zero unset Properties target ID) was confirmed in game before release.
- The release build passed **51 native checks** on Windows. Eight experimental SDK examples also compile. Tests using game assets require the matching installed game; GPU tests require a working graphics environment.
- AnyMap's orientation correction is user-confirmed. AnyGraphics includes native scene controls, scene-only AA before the HUD, volumetric fog, and configurable sun/local-light beams. The latest submission-retry correction passed fixtures, not a new visual acceptance session.

## Get started

From your existing clone, fetch and fast-forward your clean main checkout. Preserve any local work before switching branches.

```powershell
git fetch origin
git switch main
git pull --ff-only origin main
```

Install Windows x64, Git, Visual Studio's **Desktop development with C++** workload, Windows SDK, CMake and Python 3. This workspace used Visual Studio 2026 (18) and Python 3.12. Use a Visual Studio developer PowerShell so the compiler and CMake are available. Capstone is optional for native disassembly research, not a normal build requirement. GitHub CLI/login is only needed for publishing.

Install Anymaker through Steam. Native hooks target **0.1.24 / Steam build 25826614**. Verify both files against `native/BUILD_MANIFEST.json`; a different game build needs reviewed bindings, not disabled identity checks.

```powershell
$gameDirectory = 'C:/Program Files (x86)/Steam/steamapps/common/Anymaker'
Get-FileHash "$gameDirectory/game.exe" -Algorithm SHA256
Get-FileHash "$gameDirectory/bin/game.gcl" -Algorithm SHA256
New-Item -ItemType Directory -Force generated
python native/generate_road_graph.py generated/roads.bin $gameDirectory
cmake -S native -B build -A x64 -DANYAPI_GAME_DIRECTORY="$gameDirectory"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --output-junit native-tests.xml
```

Generate the road cache **before configuring** so the embedded map and asset checks are included. Create a fresh build directory in the new workspace; CMake caches contain absolute paths and should not be transferred. The game assets and generated cache are not checked into Git.

DLLs are in `build/Release`. With the game closed, the API loader `dinput8.dll` belongs next to `game.exe`; mod DLLs belong under `AnyAPI and Modding/mods`. Back up existing DLLs and the manager receipt before installing a candidate. Keep receipt hashes consistent with installed files. Do not replace the stable catalog with development binaries just to transfer a workspace.

Manager build: `powershell -File manager/build.ps1`. Manager 1.3.2 embeds API 0.33.0 and the matching guide and starter SDK; newer API and mod downloads come from the catalog. Building the EXE does not refresh its embedded resources; see the [manager documentation](../../manager/README.md) for the refresh steps. See [manager documentation](../../manager/README.md).

## Research downloads

- [Expanded experimental SDK](https://github.com/sYx-tv/AnyAPI-Modding/releases/tag/mods-2026.10.08.1): `AnyAPI-Experimental-SDK.zip` includes the guarded binding layer and complete generated binding catalog.
- [Complete supplied references](../sdk-reference/README.md): original and v2 archives include metadata, analysis tools, documentation and supplied runtime/build-comparison evidence. Use v2 for build 25755694. Download these assets separately; cloning Git does not download release attachments.
- These references contain no decompiled function bodies or game binaries. Unresolved or untested pathways remain experimental. Metadata presence is not proof a method is safe to call.

Extract the SDK ZIP into a separate research directory. Start with its `sdk/experimental/README.md`; the reference root for `tools/bind.py --reference` is the extracted `sdk/experimental/reference` directory. Do not overwrite maintained repository headers with older generated headers from the original bundle.

## AnyBalance regression test

Equip Properties and aim at a bare chassis, an edge/plate, a door handle and a tyre. The same connected creation should retain its centre while you move the camera. Looking away or changing tools should hide the display. Check that ordinary Properties actions still work and simulation speed remains normal.

The latest fix changes target resolution only. It uses the first native hovered interactible, rejecting non-vehicle obstructions. It does not scan through objects, change physics, or modify the working final-camera projection.

Key code: `native/anyapi_creation_balance.inc`, `native/balance_assembly.inc`, `native/balance_math.h`, `native/anybalance_mod.cpp`, and `native/tests/balance_native_test.cpp`. See [API contracts](../api/creation-balance.md) and [mod behavior](../mods/balance.md). Connected-body aggregation currently relies on locally hosted server connection evidence; remote-server topology and accurate cargo/fluid contributions remain limitations.

The earlier local candidate packager assumes a sibling `graphics_clear_air_release` checkout and cached release ZIPs. It is a developer convenience, not a portable build requirement. Adapt its inputs before using it from a fresh clone. Follow [publishing instructions](publishing.md) for release packaging, archive/DLL hashes and catalog updates.

## What does not need transferring

Old staging directories, local compiler dependencies, generated outputs and build caches can be recreated. Personal manager preferences, game saves and mod settings are separate from source; back them up only if moving to a different computer and you want the same play/test state. Never copy credentials into the repository.

Earlier prose that calls the native build “0.27” describes the original build profile. Use this handoff, the current CMake version resources, build manifest and release catalog to distinguish development source from stable downloads.
