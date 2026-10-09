# AnyAPI 0.34.1

Fixes the quick wheel refusing to equip tools in a world after it was reloaded.

The wheel equips tools into one hotbar slot it treats as its own, but it only remembered that slot until the world unloaded. The game saves the hotbar, so after a reload (a crash or a normal quit) the wheel's tool looked like any other item. With all hotbar slots full, every equip failed with `EQUIPMENT_TOOL_EQUIP needs_one_free_hotbar_slot=1`, and the wheel only worked again in a new world or after clearing a slot.

When no hotbar slot is empty, the wheel now reuses a slot that already holds a construction tool, the selected one first, and logs `EQUIPMENT_TOOL_RECLAIM`. The replaced tool stays in its container. Weapons, devices and other non-tool items are still never replaced. See [Equipment](../api/equipment.md) and [Quick wheel](../mods/quick-wheel.md).

Update AnyAPI in the manager, then restart the game. AnyQuickWheel 1.1.0 is unchanged. The API revision is now 35; existing mods keep working.

All 52 native checks passed on Windows, including a new unit check for the slot choice. The author confirmed in game that the wheel equips after reloading a world with a full hotbar.
