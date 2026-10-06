# Building AnyAPI and mods

## Requirements

- Windows x64 and a compatible Anymaker installation.
- Visual Studio with **Desktop development with C++**, the Windows SDK and CMake.
- Python 3 for local road-cache generation.

The 0.27.0 candidate native profile targets Anymaker 0.1.23 / Steam build 25755694.
Run these commands from the repository root in a developer PowerShell. Adjust
the game folder to match your installation.

```powershell
$gameDirectory = "C:/Program Files (x86)/Steam/steamapps/common/Anymaker"
New-Item -ItemType Directory -Force generated
python native/generate_road_graph.py generated/roads.bin $gameDirectory
cmake -S native -B build -A x64 -DANYAPI_GAME_DIRECTORY="$gameDirectory"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The road cache is generated from installed assets and embedded into AnyMap.dll.
Keep `generated/` local; it is ignored by Git. Optional native-contract inspection
tools require Capstone and explicitly supplied game/disassembly inputs. They are
not required for the normal C++ build.

## Output

`build/Release` contains `dinput8.dll`, `AnyHelpers.dll`, `AnyInventory.dll`,
`AnyStorage.dll`, `AnyMap.dll` and `AnyGraphics.dll`.
Close the game before replacing DLLs. The API loader belongs beside `game.exe`;
mod DLLs belong in `AnyAPI and Modding/mods`. Prefer manager import for local mods
so metadata, backups and receipts are maintained.

`AnyMap.exe` is an excluded legacy preview target. It is not needed to use AnyMap
in game. Mods and native extensions run inside Anymaker; the manager is the separate app.

## Manager build

```powershell
powershell -File manager/build.ps1
```

The manager uses the Windows .NET Framework C# compiler and checked-in release
resources. This builds the published manager profile with bundled API 0.25.0;
it does not automatically publish the newer native source or replace its payload.
See [Manager source](../../manager/README.md) and [Publishing](publishing.md).
