# Creation balance snapshots

Query `anyapi.creation_balance`, version **1**, after `AnyAPI_ModReady`. Header: [anyapi_creation_balance_v1.h](../../sdk/include/anyapi_creation_balance_v1.h). Added in the local AnyAPI **0.32.0** test build; live acceptance is pending.

`copy` returns a bounded, copied snapshot of the creation targeted by the locally equipped Properties Tool. It exposes no game pointers and no physics mutation functions. A copy is unavailable outside focused gameplay, when another tool is equipped, without a valid target, or after **150 ms** without a native overlay sample.

The snapshot contains the vehicle ID, selected tool item ID, context generation, sampling time, local shape centre, local bounds, world centre, local height/offsets and projected centre/axes/bounds points. Screen positions use NDC X/Y from the final renderer view-projection matrix and positive homogeneous depth; consumers convert X/Y to viewport pixels and reject points behind the camera. Body mass is optional and indicated by `ANY_BALANCE_BODY_MASS`. `body_count` reports the combined bodies; `ANY_BALANCE_CONNECTED_CREATION` indicates a multi-body aggregate from verified local connection data. The vehicle ID identifies the heaviest body, with ID tie-breaks, rather than the current hovered member.

## Native contracts

All hooks require the loader's exact executable and GCL identity checks. Reviewed bodies are in `native/balance_contract.h`.

- `client_scene.item_world.vehicle_editor_properties.update_ui_overlay` is reached through the equipped item's virtual slot **12**. Its 424-byte body is shared by thirteen editor-tool implementations, so byte scanning alone cannot identify Properties. The runtime uses the selected authored definition ID and its actual virtual call cell; it does not pick the first matching body.
- The Properties overlay reads its state object from the second word of the `ref` at item **+0x38**, i.e. item **+0x40**. The state overlay virtual offset is read from dependency **2** of that actual Properties function. Only the exact hovered or selected Properties-state overlay bodies are accepted.
- Hovered/selected vehicle ID is at state **+8**. The scene's vehicle container is at **+0xa0**. The unique hovered-state overlay provides the vehicle lookup virtual offset at dependency **0** and render-transform virtual offset at dependency **9**.
- Vehicle lookup checks the returned vehicle ID at **+8**. The render transform is copied through the reviewed native getter. Native bounds come from virtual slot **15**, checked against `get_bounds_outer` before use.
- Vehicle **+0x408** is `m_grid_to_physics_center`. The reviewed `build_physics_shape` body assigns it from accumulated weighted positions divided by accumulated weight (division and assignment at body offsets **0x2706â€“0x27d3**). This is the client shape centre, not a server cargo estimate.
- Client vehicle physics is at **+0x4d0**. The optional native `physics.object.get_mass` import is obtained through dependency **7** of the unique `client_scene.vehicle_element.rotor.get_is_downwash_surface` body.
- The pre-HUD renderer boundary copies the main camera's final **view-projection matrix at graphics scene +0x240** and graphics origin at **+8**. It removes the world origin once and applies the full matrix. The HUD consumer reprojects all stored world points using that frame's copied render camera; it does not use the tool-overlay camera for the displayed marker. Camera copies expire after 150 ms. Native pointers are not retained across threads.

The overlay observer forwards the original call exactly once, then copies only the authorised locally selected tool's target. Sampling runs on the native overlay thread. The selected tool is tracked on the local actor tick; context/tool changes invalidate earlier data. Snapshot access uses a lock and never invokes borrowed game objects from the drawing thread.

## Connected creations

The exact `server_scene.vehicle` constructor resolves its actual type metadata and virtual slot **9**, checked against the unique `server_scene.vehicle.tick` body before observing its shared call cell. The observer forwards the original tick exactly once. It copies connected body IDs from the inspected creation's grid/component containers and the reviewed `get_connected_components` pointer reads on the owning server thread. The client receives only a locked ID graph, capped at 128 bodies with a 1000 ms freshness limit. It is used only when the native client identifies itself as a local peer. Unrelated vehicle ticks return before visiting grids/components. Connection refreshes are limited to once per 200 ms per requested member, and connection-function contracts are cached after validation.

Graph traversal treats connections as undirected, including slider connections whose records exist on only one endpoint. The client refreshes native mass/bounds data every 200 ms while updating member transforms and projection each frame. Validated member lookups are reused within that interval. It combines mass-weighted native centres in world space, transforms all member bounds into the heaviest body frame, and projects the aggregate using the final renderer camera at the HUD boundary. Incomplete local topology hides the result rather than switching to a partial-body centre. Remote-server topology is not exposed yet.

Bare vehicle hover targets fall back to the first native hovered interactible when the Properties state has no component target. This uses the reviewed 272-byte interactible and bounded native vector descriptor.

Fluids, cargo and contact forces still need additional server/physics evidence before they can be exposed as tipping predictions or axle loads.
