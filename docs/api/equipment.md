# Equipment and hotbar selection

Query `anyapi.equipment`, version **1**, during `AnyAPI_ModReady`. Header: [anyapi_equipment_v1.h](../../sdk/include/anyapi_equipment_v1.h). Added in the AnyAPI 0.31.0 local candidate.

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

Actual native body and class/table checks run before calling through these contracts. Invalid actors or unavailable contracts do not expose usable equipment. The public service exposes no borrowed pointers. Live-world acceptance remains pending for this candidate.
