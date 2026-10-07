# AnyQuickWheel

Hold **Q** during gameplay to open the equipment wheel. Move the mouse toward a tool and release **Q** to equip it directly. You can also left-click to confirm. Release in the centre, right-click or press Escape to cancel.

The wheel shows occupied native hotbar slots, plus the primary-hand / empty-hands slot. This first version gives direct access to your existing hotbar equipment; it does not create equipment or assign arbitrary backpack items to the hotbar. With more than twelve choices, scroll the mouse wheel to change pages.

## Configuration

With AnyHelpers installed:

- **Mod Controls → AnyQuickWheel → Hold equipment wheel** changes the key.
- **Mod Settings → AnyQuickWheel** changes size, opacity, centre cancel radius, item icons and whether the primary-hand slot is shown.
- Choose **Apply** to save changes.

Menus, inventory and focus loss close the wheel and release input. While open, the wheel captures camera/mouse and action input. It uses native GPU HUD drawing with resident item-preview textures; no separate window or executable is used.

## Native selection and compatibility

Requires the AnyAPI **0.31.0 local candidate**, for the reviewed Anymaker 0.1.23 build. Selection is queued to the player's native tick and calls the game's own hotbar authority selector. The current slot/item pair is revalidated before execution. Requests expire after 500 milliseconds, on a changed context or if the item disappears. The game retains its normal networking and equipment rules.

**Status:** local development candidate. Live-world equipping, input release and appearance acceptance are pending.
