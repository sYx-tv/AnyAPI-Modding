# AnyInventory

AnyInventory adds an item browser beside the native inventory. Search the complete
catalog, filter categories, mark favorites, view item images and read descriptions.
The current game profile has 970 definitions, including mechanical/building components.

Press **F8** to show or hide the browser. The native inventory, portrait and tooltip
roots shift together while it is open and return to their normal positions when
hidden. Closing inventory removes the browser in the same UI lifecycle.

**Add** is available only in Sandbox and Creative. Survival and Career remain
lookup-only. Requests use the game's native event path; server rules still decide
the outcome. A full inventory may cause the native game to place the item in the world.

AnyHelpers provides optional settings and keybinds. Previews approximate materials,
textures and animated poses. See [Item catalog](../api/item-catalog.md),
[Item images](../api/item-images.md), [Inventory actions](../api/inventory-actions.md)
and [Screen layout](../api/screen-layout.md) for implementation contracts.
