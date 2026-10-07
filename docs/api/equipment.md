# Equipment and hotbar selection

Query `anyapi.equipment`, version **1**, during `AnyAPI_ModReady`. Header: [anyapi_equipment_v1.h](../../sdk/include/anyapi_equipment_v1.h). Added in AnyAPI 0.31.0.

The snapshot contains copied local-player hotbar entries, their native slot/item IDs, authored definition IDs and names. Slot **-1** is the native primary-hand / empty-hands selection; slots 0 and above retain their native indices. Empty ordinary slots are omitted. The capacity is 33, including the primary hand. The snapshot has a context token, selected native slot and monotonic sampling timestamp. A copy older than 500 milliseconds is unavailable.

`select(context, slot_index, expected_item_id)` returns a request ticket, or zero if the snapshot/context/slot no longer matches. The latest accepted queued request supersedes an earlier queued selection. The request is executed once on the native local-player thread, only during focused gameplay, and verifies the slot still contains the expected item. `state(ticket)` reports queued, applied, expired or failed. **Applied** confirms local native selection, not an independently observed server acknowledgement.

## Reviewed native contract

The profile matches the exact Anymaker 0.1.23 executable/game-data hashes already checked by the loader. Anchors are in `native/equipment_contract.h`.

- Local actor: scene actor-container pointers at scene + 0x188 / 0x190, compared with the actor receiving the native tick.
- Inventory and hotbar: actor virtual getters recovered from `client_scene.actor.update_ui_overlay` dependency slots, requiring reviewed character getter bodies and corroborated actor offsets 0x5c8 / 0x658.
- Hotbar count/item virtual slots: `frontend_ui._build_ui_hotbar` dependencies 2 and 9.
- Authority selector virtual slot: `client_scene.inventory_hotbar.select_slot_left_authority` dependency 0.
- Item lookup: reviewed `client_scene.inventory.get_item_world_by_item_id` body. Definition pointer at item + 0x28; bounded UTF-8 strings at definition + 0 and + 0x10.
- Selection calls `set_selected_slot_authority` with the game's native pointer/reference ABI. The provider does not patch replicated properties, forge events or simulate keypress cycling.

Actual native body and class/table checks run before calling through these contracts. Invalid actors or unavailable contracts do not expose usable equipment. The public service exposes no borrowed pointers. The author confirmed native equipment selection in a loaded world.

## Inventory tools, version 2

Query `anyapi.equipment`, version **2**. Header: [anyapi_equipment_v2.h](../../sdk/include/anyapi_equipment_v2.h). Version 1 remains available with its original layout and hotbar behavior.

`copy` returns up to 64 distinct carried construction-tool definitions. The native recursive inventory enumerator includes nested bag contents; a copied class filter excludes weapons, devices and physical add-component parts. Distinct definition IDs preserve tool variants. `equip(context, item_id)` queues an operation on the local player's tick. It revalidates the item, definition and carried source, then uses an existing hotbar assignment or a single shared empty slot. An assignment waits up to three seconds for native replicated slot confirmation before selecting. Queue requests expire after 500 milliseconds before execution. Superseded, stale and missing-item requests cannot execute twice.

The shared slot is reused only while it contains the mod's previously assigned item or is empty. A user replacement is protected. No free slot means failure, rather than replacing another item. No server inventory objects or ownership flags are modified directly.

Reviewed anchors are in `native/equipment_tools_contract.h`:

- `client_scene.inventory.get_items(inventory, filter_pointer, pointer_vector)` recursively enumerates carried items.
- `client_scene.item_world.gun._is_internal_magazine_ammo_available` provides pointer-vector constructor/destructor call cells at body end +8/+80 and the s32 property virtual slot at +72.
- The unique `frontend_ui_inventory.update_ui` caller resolves the active-slot handler at body end +149×8. Active and passive slot handlers have identical machine code, so prefix scanning alone cannot distinguish them.
- `frontend_ui_inventory._release_item_into_hotbar_slot` provides the normal `client_peer.data.push_event_hotbar_set_slot_item` call cell at body end +16.
- `client_scene.inventory.get_item_source_by_item_id` distinguishes worn-container and primary-hand sources.
- `inventory_definition.m_class` is a native string at +0x30; item IDs use the checked s32 property getter on item +8.
- Native vector layout: pointer +0, flags +8, count +12, capacity +16, stride +20, constructor/destructor +24/+32. The pointer vector has stride 8 and native lifetime cleanup, including exception paths.
- Native `inventory_item_util.item_id` is six s32 fields: type, floor, vehicle, actor, component, item. The local client peer event object is client +0x90.

Reference arguments are passed by address. Hidden return pointers are the first parameter for returned IDs/pointers. Execution and snapshots are local-player-only. The author confirmed inventory-driven tool selection and equipping in a loaded world.
