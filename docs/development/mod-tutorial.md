# Tutorial: a complete mod

This walks through one small but complete mod, **ModeBadge**, from an empty folder
to a tested DLL in the game. Press **F7** in a world and a badge shows the current
game mode (Survival, Creative, Sandbox) in a corner of the screen. Press F7 again
to hide it.

It is deliberately small, but it uses the pieces almost every mod needs:

- the two exports the loader calls (`AnyAPI_ModInit` and `AnyAPI_ModReady`)
- querying and validating versioned services
- reading game state (`anyapi.session`, `anyapi.ui_state`)
- drawing on the HUD with `anyapi.gpu_draw`
- a rebindable key and editable settings through AnyHelpers, with working
  defaults when AnyHelpers is not installed
- packaging, installing and checking the log

Start with [Create a DLL mod](first-mod.md) if you have not built a DLL yet. The
finished project is in [`tutorial/ModeBadge`](tutorial/ModeBadge/), and you can
build it in place from this repository. The snippets below are taken from its
[`mod.cpp`](tutorial/ModeBadge/mod.cpp).

## 1. What you need

- Windows x64, Visual Studio with **Desktop development with C++**, CMake.
- A clone of this repository for the headers in [`sdk/include`](../../sdk/include/).
- Anymaker **0.1.23** (Steam build 25755694) with AnyAPI installed through the
  manager. AnyHelpers is optional; install it to see the settings and keybind.

## 2. Project layout

```text
ModeBadge/
  CMakeLists.txt
  mod.cpp
  ModeBadge.anymod.json
```

```cmake
cmake_minimum_required(VERSION 3.20)
project(ModeBadge LANGUAGES CXX)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "AnyAPI mods must be built for Windows x64")
endif()
set(ANYAPI_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../../sdk/include" CACHE PATH "AnyAPI sdk/include folder")
add_library(ModeBadge SHARED mod.cpp)
target_compile_features(ModeBadge PRIVATE cxx_std_20)
target_compile_options(ModeBadge PRIVATE /utf-8 /EHsc)
target_include_directories(ModeBadge PRIVATE "${ANYAPI_SDK}")
```

The default `ANYAPI_SDK` points at this repository's headers. If you copy the
folder elsewhere, pass `-DANYAPI_SDK=<clone>/sdk/include` when configuring.

## 3. The lifecycle

The loader in `dinput8.dll` scans `<game>/AnyAPI and Modding/mods` for `.dll` files
(top level only, sorted by filename, at most 32) and calls two exports:

1. **`AnyAPI_ModInit(host, callbacks)`** runs once per DLL, in filename order.
   Check the ABI and structure size, keep a copy of the host table, and fill in
   your callbacks. Return `false` to decline loading. Do not query other mods'
   services here, because they may not have loaded yet.
2. **`AnyAPI_ModReady()`** (optional) runs once after every DLL has initialized.
   This is where you query services, register settings and keybinds, and register
   a renderer. Because it runs after everything has loaded, filename order no
   longer matters.

```cpp
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* callbacks) {
    if (!h || !callbacks || h->abi != ANYAPI_MOD_ABI || h->struct_size != sizeof(AnyModHostV1)) return false;
    host = *h;
    *callbacks = {};
    callbacks->id = MOD_ID;
    callbacks->input = input;
    return true;
}
```

DLLs stay loaded until the game exits. There is no hot reload: close the game
before replacing your DLL. If any callback throws, the host disables that mod and
logs it.

## 4. Query services and check them

Every service is a table with `struct_size` and `version` at the front. Always
check both before calling anything, and check each function pointer you use. A
small helper keeps this tidy:

```cpp
template <class T> static const T* query(const AnyServicesV1* services, const char* id, uint32_t version) {
    auto table = static_cast<const T*>(services->query(id, version));
    return table && table->struct_size == sizeof(T) && table->version == version ? table : nullptr;
}
```

ModeBadge **requires** three framework services and treats AnyHelpers as
**optional**:

| Service | Header | Used for |
| --- | --- | --- |
| `anyapi.gpu_draw` v1 | `anyapi_gpu_draw_v1.h` | Drawing the badge ([GPU drawing](../api/gpu-drawing.md)) |
| `anyapi.ui_state` v1 | `anyapi_ui_state_v1.h` | Hiding it in menus and inventory ([UI state](../api/ui-state.md)) |
| `anyapi.session` v1 | `anyapi_session_v1.h` | Reading the game mode ([Session](../api/session.md)) |
| `anyhelpers.controls` v1 | `mod_controls_v1.h` | Rebindable key (optional, [AnyHelpers](../mods/helpers.md)) |
| `anyhelpers.settings` v1 | `anyhelpers_settings_v1.h` | Editable settings (optional, [Helper settings](../api/helper-settings.md)) |

If a required service is missing, log a clear message and stay idle rather than
crashing:

```cpp
if (!gpu || !gpu->emit || !gpu->register_renderer || !ui || !ui->copy || !session || !session->copy) {
    gpu = nullptr;
    if (host.log) host.log(2, MOD_ID, "Needs AnyAPI with gpu_draw, ui_state and session.");
    return;
}
```

Log levels are 0 info, 1 warning, 2 error and 3 debug.

## 5. Read game state

Services return **copies**, never game pointers. A `false` return means "unknown
right now", and the safe reaction is to hide your UI and do nothing:

```cpp
static bool in_gameplay() {
    AnyUiSnapshotV1 state;
    return ui && ui->copy(&state) && state.kind == ANY_UI_GAMEPLAY;
}
```

`anyapi.session` returns the normalized mode. It returns `false` when the sample is
older than 500 ms, for example at the main menu, so ModeBadge shows "Mode unknown"
instead of guessing.

## 6. Draw on the HUD

Register one renderer per DLL in `AnyAPI_ModReady`. The callback runs every
presented frame. Inside it, `emit` commands in screen pixels; text and points are
copied immediately, so stack buffers are fine.

```cpp
AnyGpuCommandV1 box;
box.kind = ANY_GPU_ROUND_RECT;
box.rect[0] = x; box.rect[1] = margin; box.rect[2] = width; box.rect[3] = height;
box.radius = 6;
box.color = 0xff191c20;          // straight ARGB
box.opacity = float(opacity / 100);
gpu->emit(&box);
```

Check `in_gameplay()` and `frame->focused` at the top of the renderer and return
early otherwise, so the badge disappears as soon as a menu or the inventory opens.

## 7. Handle input

The `input` callback receives keyboard and mouse events. Return nonzero to consume
an event so the game and later mods do not see it. Consume only what you act on,
and also consume the matching key-up so the game never sees half a key press.
Handle `ANY_FOCUS_LOST` by clearing any held-key state.

Input and rendering can run on **different threads**. ModeBadge guards its state
with one `std::mutex`, the same pattern the shipped mods use.

## 8. Settings and a rebindable key with AnyHelpers

AnyHelpers adds the **Mod Controls** and **Mod Settings** tabs. Nothing appears
there automatically: you register each option, then read the committed value
yourself.

```cpp
ModControlActionV1 action;
action.mod_id = MOD_ID; action.mod_name = "ModeBadge";
action.action_id = "toggle"; action.label = "Show game mode";
action.default_key = VK_F7;
toggle_action = controls->register_action(&action);   // 0 means rejected
```

At input time, `controls->key(toggle_action)` returns the committed Windows virtual
key, or 0 if the player unbound it. Without AnyHelpers, fall back to `VK_F7`.

Settings work the same way. Register each with a stable `setting_id` (it is the
saved identity, so do not rename it later) and keep the returned token.
`settings->revision()` increases only after the player applies a change, so re-read
values only when it moves:

```cpp
static void refresh_settings() {
    if (!settings) return;
    uint64_t revision = settings->revision();
    if (revision == settings_revision) return;
    settings_revision = revision;
    AnySettingValueV1 value;
    if (settings->get(enabled_token, &value)) enabled = value.number != 0;
    // ...
}
```

IDs use lowercase letters, digits, periods, underscores and hyphens. Bool, integer,
number and choice values all arrive in `value.number` (a choice is its index).
Clamp what you read even though AnyHelpers validates it.

## 9. Build

From a Visual Studio developer PowerShell:

```powershell
cmake -S docs/development/tutorial/ModeBadge -B build-modebadge -A x64
cmake --build build-modebadge --config Release
```

The DLL is `build-modebadge/Release/ModeBadge.dll`. `build-*` folders are ignored by Git.

## 10. Install and test

1. Close the game.
2. In AnyAPI Manager, open **Mods → Local**, choose **Import DLL** and pick
   `ModeBadge.dll`. Copy [`ModeBadge.anymod.json`](tutorial/ModeBadge/ModeBadge.anymod.json)
   next to the DLL first so the manager shows a name and version:

   ```json
   {
     "Name": "ModeBadge",
     "Version": "1.0.0",
     "Description": "Press F7 to show the current game mode.",
     "MinimumApi": 27
   }
   ```

   You can also copy the DLL straight into `<game>/AnyAPI and Modding/mods` and
   choose **Refresh**.
3. Choose **Play with mods**.
4. Open `<game>/anymaker_modding.log` (it sits beside `game.exe`). Look for your
   ready line:

   ```text
   [0] [example.modebadge] [pid=...] Ready. Press F7 in a world to show the game mode.
   ```

   If it is missing, look for `Plugin DLL load failed` (usually a missing Visual C++
   runtime or a 32-bit build) or `Unsupported plugin export` (the DLL does not
   export `AnyAPI_ModInit`).

Then check, in a world:

- F7 shows and hides the badge, and holding F7 does not make it flicker.
- Opening the inventory, pause menu or settings hides it; closing them brings it back.
- With AnyHelpers: **Mod Controls → ModeBadge** rebinds the key, and
  **Mod Settings → ModeBadge** changes the corner and opacity after **Apply**.
- Without AnyHelpers: F7 still works with the defaults.

## 11. Share it

To publish through the official catalog, follow [Publishing](publishing.md): a ZIP
containing exactly `AnyAPI and Modding/mods/ModeBadge.dll`, uploaded to a versioned
release, then a catalog entry with its hashes and verified game builds. Packages
must never include saved settings.

## Where to go next

- [API service reference](../api/README.md) lists every service, and
  [`native/examples`](../../native/examples/) has a short example for most of them.
- The shipped mods are full references: `native/anyclock_mod.cpp` is the closest to
  this tutorial (key, settings and GPU drawing in under 100 lines).
- [Experimental SDK](../../sdk/experimental/README.md) for game internals beyond the
  supported services. It is unvalidated, so read its known gaps first.

## About the source

[`mod.cpp`](tutorial/ModeBadge/mod.cpp) is syntax-checked against the current
`sdk/include` headers on every push (see `.github/workflows/checks.yml`). That
catches header drift, not runtime behavior: build and run it on Windows to try it
in game.
