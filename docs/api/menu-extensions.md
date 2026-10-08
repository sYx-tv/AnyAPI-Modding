# Native menu extensions, version 1

For independent top-level Settings tabs and compact native key rows, use version 2:
[Settings tabs](settings-tabs.md). AnyHelpers.dll now uses that service.
This document describes the retained version 1 Controls-section interface.

AnyAPI provides native integration; each DLL mod supplies its own section title, widgets, behavior, state and persistence. No Mod Controls category or keybinding service is built into AnyAPI. Without AnyHelpers.dll, that category does not exist. This extends the process-lifetime DLL ABI 1 without changing either existing host or callback layout.

## Available capabilities

Include `anyapi_services_v1.h` and use `AnyAPI_Services()`. It resolves `AnyAPI_GetServices(1)` from the already loaded dinput8.dll; it never loads another API or launches a process. An absent export means the older host lacks optional services.

| Function | Contract |
| --- | --- |
| `query(id, version)` | Return a process-lifetime service table for the requested exact version, or null. `anyapi.menu`, version 1, is supplied by the framework. |
| `publish(id, version, table)` | Publish an optional mod-owned table from an owned callback. Duplicate name/version pairs and the reserved `anyapi.` namespace are rejected. Up to 64 published tables. |
| `input_filter(callback, user, priority)` | Register an owned input filter before ordinary mod callbacks. Highest priority first; equal priorities keep registration order. Up to 32 filters. A consumed event stops further filters and ordinary callbacks; focus loss reaches all. |
| Optional `AnyAPI_ModReady()` export | Called once after all accepted DLLs initialize. Resolve optional mod dependencies here so filename order is irrelevant. |

Register services, filters and sections during ModInit/ModReady. Tables and callback pointers must remain valid until process exit. Publishing does not create a trust or memory-isolation boundary. A rejected or faulted provider is not returned by future queries; already acquired tables remain allocated because DLLs are retained. Consumer mods must handle missing dependencies. Exceptions disable the owning mod and release its input capture.

IDs use 1–95 lowercase ASCII letters, digits, periods, underscores and hyphens. All APIs target x64 Windows. Check structure sizes and versions. Menu and input callbacks can run on different threads: synchronize plugin-owned state and avoid blocking work. Capture ownership is scoped to the active DLL callback, including section lifecycle/draw and input-filter callbacks.

## Native menu service

`anyapi_menu_v1.h` defines `AnyMenuV1`. The currently implemented location is **Settings > Controls, after the Flight Stick section and inside the existing scroll content**. This is a section/category slot, not a new top-level Settings tab. General, Graphics, Audio, main-menu entries, new tabs, sliders, checkboxes and gamepad rebinding are not exposed by this version. Add and verify a separate native contract when a mod actually requires one; do not advertise unimplemented locations.

| Function / callback | Behavior |
| --- | --- |
| `add_section(&section)` | Register a title, stable ID, order, user pointer, draw callback and optional state/lifecycle callbacks. Max 32 sections. Duplicate IDs within one owner are rejected; another owner may reuse the same local ID. |
| `available()` | True only after the current-build native bridge has been verified and installed. Registration can succeed before it becomes available. |
| `settings_open()` | Read the current native Options UI state. Allows gameplay mods to hide overlays and suppress their binds while settings are open. |
| `heading(id, text)` | Native heading using the game's font and styling. |
| `begin_table(id, columns, width_cells)` | Start a native table, 1–8 columns. Width 0 inherits the native option column width; height inherits the native icon/item row height. Returns false outside draw or for invalid dimensions. |
| `end_table()` | Close only this callback's table. Unclosed tables are recovered before the next section. Nesting is capped at 8. |
| `button(id, text, disabled)` | Native button, native option-column width and row height, no custom icon. Returns whether it was clicked. |
| `draw(frame, user)` | Build immediate-mode native widgets once per Controls frame. Widgets exist only during this callback; never retain or expose native UI pointers. |
| `event(OPEN, user)` | Options session opens, before the first page is built. Initialize the mod's working copy. |
| `dirty(user)` | Whether the section has pending changes. Combined with vanilla state to enable native Apply. |
| `defaults(user)` | Whether the working copy is at defaults. Combined with vanilla state to enable native Reset. Missing callback means true. |
| `event(APPLY, user)` | Native Apply was requested with updated settings and valid vanilla controls. Commit/save the mod's working copy. Keep dirty true if saving fails. |
| `event(RESET, user)` | Native Reset was requested. Stage defaults; commit only on Apply. The game's own controls also follow their ordinary Reset behavior. |
| `event(CANCEL, user)` | Options closes. Discard remaining pending changes and release capture. This also arrives after a successful Apply; committed values must remain saved. |

Sections sort by order, then owner DLL filename, then section ID. The API draws the heading supplied by the mod. Widget IDs are automatically owner/section-namespaced. One mod cannot pop another's tables. The draw callback may be skipped if a mod faults. Keep labels concise enough for the native column width; API 1 does not implement custom text wrapping.

Switching Settings tabs retains the working copy until Apply/Reset/close. Native lifecycle callbacks preserve original game functions exactly once. Vanilla keybind conflict rules remain owned by the game; extension state does not rewrite native control enums or the game's settings data.

## Current build validation

`MENU_NATIVE_CONTRACTS.json` records exact game.gcl function identities, body sizes, body SHA256s, dependency offsets, Options state 11 and the Controls scroll close call site. `anyapi_menu_patterns.h` contains the exact generated bytes. The existing executable/game.gcl SHA256 guard remains mandatory.

The worker validates the complete Controls builder and Options update bodies, resolves helper identities through those owners' dependency cells, and validates complete helper bodies. The current bridge redirects thirteen owner-local dependency references through private cells; shared native callable cells are not overwritten. The validated 19-byte Settings-update prologue uses the existing transactional detour and registered unwind metadata. The Controls builder is forwarded by its owner-local dispatch slot. Version 2 tab contracts and routing are described separately in settings-tabs.md. Failed preparation restores redirected references. Widgets and lifecycle stay unavailable on unmatched builds.

The quarantined legacy v16 Mod Controls prototype remains disabled. Its old offsets and public action functions are historical and are not the current service.

Fixtures verify two section owners, namespaced IDs, table recovery, exact equality return-site behavior, forwarding, lifecycle, input priority and service versions. A separate integration fixture loads the real AnyHelpers.dll and AnyMap.dll against the public contracts. These fixtures do not prove visual appearance or every menu interaction in a Steam session.

See `examples/native_settings_section.cpp`, `MOD_CONTROLS.md`, `CURRENT_CAPABILITIES.json` and `MENU_LOGGING.md`.


## Button activation / capture repair (2026-10-03)

The first live menu build had a rebind softlock: the native button result was
hover/focus, but the host exposed it directly as a click. Hovering restarted capture
on each frame, including immediately after a key or Escape. The API now requires
both active/hovered state and the exact native `client_ui.is_input_enter` event;
disabled buttons never activate. The mod separately refuses to restart an active
capture. Successful capture does not re-arm from a stationary pointer.

The full native activation-helper body is validated through the Controls owner's
0x86f0 dependency. A regression executes that real current-build body with controlled
hovered-element virtual tables. It checks 300 hover-only frames, actual activation,
disabled/not-hovered/no-target cases. A native-return-address fixture verifies 256
rows are inserted before scroll close at 0x7726; they share the game's existing
Controls scroll and are not clipped into a fixed-height mod panel. The real DLL
fixture checks a 256-action list, repeated capture frames, Escape, focus loss and
timeout without re-entry. Native tab integration and rebinding were confirmed in host gameplay. Long-list
fixtures establish row insertion; they do not replace visual checks at every screen size.

## Graphics sections (AnyAPI 0.28.0)

`AnyMenuV1::add_section` also accepts `ANY_MENU_SETTINGS_GRAPHICS` (3).
Its ABI is unchanged; this location requires the reviewed 0.28.0 bridge.
Sections append inside the native Graphics scroll, before the third closing
call at builder return offset `0x3c59`. V3 native widgets work in these draw
callbacks. Graphics equality comparisons at `0x92` and `0x123` include the
section's dirty/default flags; private dependency slots `0x4678`, `0x4730`,
`0x4810` and `0x4818` route equality, container close, Reset and Apply respectively.
All target bodies are validated against the pinned build before attachment.

Graphics Apply/Reset events are scoped to Graphics sections; Controls events
remain scoped to Controls. Settings Open/Cancel reach both. Mod IDs remain
namespaced, exceptions disable the offending owner and unbalanced tables are
recovered before returning to the native container. Existing graphics rows,
scrolling, resolution controls and Back button remain native game code.
