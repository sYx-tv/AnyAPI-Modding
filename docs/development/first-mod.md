# Create a DLL mod

Create a project folder containing `mod.cpp` and `CMakeLists.txt`.

```cpp
#include "anyapi_mod_v1.h"

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(
    const AnyModHostV1* host, AnyModCallbacksV1* callbacks) {
    if (!host || !callbacks || host->abi != ANYAPI_MOD_ABI ||
        host->struct_size != sizeof(AnyModHostV1)) return false;
    *callbacks = {};
    callbacks->id = "example.hello";
    if (host->log) host->log(0, callbacks->id, "Loaded");
    return true;
}
```

```cmake
cmake_minimum_required(VERSION 3.20)
project(ExampleMod LANGUAGES CXX)
add_library(ExampleMod SHARED mod.cpp)
target_compile_features(ExampleMod PRIVATE cxx_std_20)
target_include_directories(ExampleMod PRIVATE "PATH_TO_ANYAPI/sdk/include")
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "AnyAPI requires Windows x64")
endif()
```

Replace `PATH_TO_ANYAPI`, configure with `cmake -S . -B build -A x64`, then build
with `cmake --build build --config Release`. Import the DLL using the manager.
Check `anymaker_modding.log` in the game folder (beside `game.exe`) after launching
with mods; your `Loaded` line appears with the mod's ID.

For integrations, implement `AnyAPI_ModReady`, query the required service and
validate its version and structure size. Follow its callback/thread restrictions
and check return values. See [examples](../../native/examples/) and
[service contracts](../api/README.md). Hot unloading is unsupported; restart the
game after rebuilding. Settings and controls require explicit registration.

Next, follow the [mod tutorial](mod-tutorial.md) for a complete mod with services,
HUD drawing, a keybind, settings, packaging and in-game testing.
