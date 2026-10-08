# Current native UI state: anyapi.ui_state v1

Query `anyapi.ui_state`, version 1, through `AnyAPI_Services()`. Include anyapi_ui_state_v1.h.
The immutable service table exposes copy(AnyUiSnapshotV1*). Initialize the POD normally;
its size and version must match. No game-owned pointer leaves this API.

copy reads the reviewed frontend UI owner's int32 state at +0x828 on every call.
It does not use player snapshots, simulation ticks, a timer, or the previous render frame.
The frontend pointer is resolved by the existing exact-build-guarded player resolver.
No new native hook is installed. The current profile targets Anymaker 0.1.23 / Steam build 25755694.
The snapshot in [UI_STATE_NATIVE_CONTRACT.json](../../native/UI_STATE_NATIVE_CONTRACT.json) was recorded
on 0.1.21 (frontend_ui.update_client_ui, GCL record 26049); at runtime the exact 0.1.23 build guard must match;
inventory is state 2, Options 11, Pause 13. State 1 is normal gameplay.

Kinds: GAMEPLAY (1), INVENTORY (2), MENU (all other valid native states).
False returns UNKNOWN for unavailable frontend data or a state outside 0..127.
Do not interpret arbitrary native states as gameplay; copied raw native_state is diagnostic.
Call from render AND input callbacks. On false/unknown or an inappropriate kind,
return no canvas and release owned input. The inventory browser also restores all three
native inventory/portrait/tooltip offsets. This prevents stale event capture as menus change.

AnyInventory requires INVENTORY, independently of player snapshot freshness.
AnyMap requires GAMEPLAY once this service is discovered. Its minimap still requires a
valid local player position; stale position may hide the map, but cannot keep it visible
in a native menu. Player data and UI lifecycle now have separate responsibilities.

Validation: the native host fixture changes the frontend state without a player tick.
Actual mod DLL fixtures retain misleading cached player UI flags while switching native
inventory, Pause, Options and unknown states. First callback must emit no canvas,
consume no input, release capture and restore inventory offsets. Inventory remains usable
without a player snapshot when the current native UI is inventory.

These changes synchronize in-process custom canvas UI with native menu lifecycle.
They do not convert the map/item browser into native game widgets. AnyHelpers' settings
and controls tabs already use native widgets. No external overlay executable is shipped.
