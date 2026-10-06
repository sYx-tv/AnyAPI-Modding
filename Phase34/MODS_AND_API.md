# AnyAPI and current mods

Target: Anymaker 0.1.21, Steam build 25725299, Windows x64. Revision 25 (2026-10-06).

Storage toolbars and cursor tooltips were confirmed working by the user. AnyMap now uses resident GPU terrain, cached text and every-presentation minimap drawing with short position/heading smoothing. Revision 25 gameplay performance confirmation remains pending; see MAP_GPU25.md and GPU_DRAW_API.md.

| Component | Available now |
| --- | --- |
| AnyAPI (dinput8.dll) | Loads DLL mods inside the game; copied player/session/UI data, input/text events, input ownership, canvas/GPU rendering, named services and labeled logs. Exact executable/GCL compatibility guard. |
| AnyHelpers | Native Mod Controls and Mod Settings tabs. Rebind/unbind, conflict handling, Apply/Cancel/Reset and saved settings. Other mods can register their own controls and typed settings. |
| AnyMap | M opens Anymap: smooth zoom/pan, Steam player names/positions/facing, named saved markers and list, waypoint routes/guidance, square rotating minimap with movable position and appearance settings. Native menus hide map UI immediately. |
| AnyStorage | Native external-storage toolbar: deposit/withdraw all, matching stacks both ways, alphabetical/quantity sorting, cancel/progress, six optional settings. Works with or without AnyInventory. |
| AnyInventory | Native inventory remains. Item browser opens alongside it; F8 hides/shows it. Search/filter/favorites, descriptions and mesh previews for 970 definitions: 372 inventory items and 598 vehicle/building components. Add is offered only in Sandbox/Creative. Native inventory, portrait and tooltips shift together and restore when the browser is hidden. |

Public API services currently include native menu tabs/sections/widgets (menu v1/v2/v3),
current UI state (ui_state v1), native session mode (session v1), complete item metadata
(item_catalog v1), copied previews (item_images v1), queued native Give requests
(inventory_actions v1/v2), and owned native root translations (screen_layout v1).
Native storage instances/widgets/events are exposed by inventory_ui v1.
AnyHelpers publishes controls and settings services for cooperating mods.
The SDK includes all current public headers, examples and detailed contracts.

The map and item browser are custom drawing inside the game process and backbuffer;
AnyHelpers uses native widgets. No separate overlay window or mod executable is required.

Known limits: software item previews approximate native materials/textures. Road routes
use authored roads rather than live traffic/obstacles. An Add request marked SENT confirms
native event submission, not server placement acknowledgement. Host play has been tested;
joining-client behavior still needs a dedicated check. AnyStorage adds native storage transfers, matching stacks and alphabetical/quantity sorting; its first live crate check is pending. Historical ModControls is now included in AnyHelpers, not an extra mod.

All 26 automated checks pass. The map, Helpers and item-browser revision 16 are user-confirmed working. AnyStorage is new and awaits its first live crate test.

AnyStorage revision 21 uses inventory UI v3 compact native symbol buttons with
copied hover tooltips. Transfers/sorting remain native replicated actions.
Long status text is available through `i`, while active work exposes `X` to cancel.
