# AnyQuickWheel

Hold **Q** during gameplay, point at a construction tool, then release **Q** to equip. Left-click also confirms. Release in the centre, right-click or press Escape to cancel.

## Automatic inventory wheel

AnyQuickWheel 1.1 finds construction tools in your carried inventory, including backpack contents. Each available tool definition gets one slice; duplicate copies share a slice. Two tools give two halves, three give three slices, and picking up or dropping tools updates the open wheel. Edge Tool and Edge Tool 3×3 are separate choices. More than twelve choices use mouse-wheel pages.

Weapons, torches, clothing, empty hands and physical parts such as engines are excluded. No manual wheel assignments are required. External crates and floor items are excluded until their tools enter your carried inventory.

## Equipping and the shared slot

The game activates stored tools through hotbar references. A tool already on your hotbar uses its existing slot. An unassigned tool uses one empty slot; the wheel then reuses that same slot for subsequent tools. Keep one hotbar slot free when first equipping an unassigned tool. No existing weapon or manually assigned item is overwritten. Tools remain in their containers rather than being moved, spawned or dropped.

Assignments use the game's normal replicated hotbar event. Selection waits for the matching slot/item to appear locally. If the server rejects the assignment, the item disappears or confirmation times out, the request fails without selecting the previous slot item.

## Configuration

With AnyHelpers installed:

- **Mod Controls → AnyQuickWheel → Hold equipment wheel** changes the key.
- **Mod Settings → AnyQuickWheel** changes size, opacity, centre cancel radius and icons.
- Choose **Apply** to save changes.

Menus, inventory and focus loss close the wheel and release input. The wheel uses native GPU HUD drawing with resident preview textures.

## Compatibility and validation

Requires the updated AnyAPI **0.31.0**, including `anyapi.equipment` v2, for reviewed Anymaker 0.1.23. The inventory-driven wheel and equipping were confirmed in a live world. The optional native control hint did not appear in the author’s live test; it is not required to use the wheel. All 47 native checks passed.
