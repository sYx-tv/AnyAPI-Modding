# Build current AnyAPI and mods

Target: Windows x64, Anymaker 0.1.21 / Steam 25725299. All current C++ source,
internal/public headers, tests, included source fragments, examples and Python tools
are in Phase34. Game-derived textures, models and road cache are intentionally absent.
No friend's compiler is needed for the separate runtime ZIP.

Install Visual Studio Community with Desktop development with C++, CMake and Python 3.
Extract this ZIP, then run in a developer PowerShell:

```powershell
New-Item -ItemType Directory -Force build-phase34
python Phase34/generate_road_graph.py build-phase34/roads_revision2.bin "C:/Program Files (x86)/Steam/steamapps/common/Anymaker"
cmake -S Phase34 -B build -A x64 -DANYAPI_GAME_DIRECTORY="C:/Program Files (x86)/Steam/steamapps/common/Anymaker"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Adjust the game folder to your own Steam installation. The map embeds the generated
road cache in AnyMap.dll and reads game textures from that installation at startup.
Release output: dinput8.dll (host), AnyHelpers.dll, AnyMap.dll, AnyInventory.dll, AnyStorage.dll.
AnyMap.exe is an excluded historical preview target; it is not part of the runtime pack.
See MODS_AND_API.md and UI_STATE_API.md for current supported behavior.
