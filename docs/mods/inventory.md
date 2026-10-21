# AnyInventory

AnyInventory adds an item browser beside the native inventory. Search the complete
catalog, filter categories, mark favorites, view item images and read descriptions.
The current game profile has 970 definitions, including mechanical/building components.
Parts and items that other mods add (such as the GPS Sensor) join the list once a
world is loaded and the inventory has been opened, and their **Add** works the same way.

Press **F8** to show or hide the browser. The native inventory, portrait and tooltip
roots shift together while it is open and return to their normal positions when
hidden. Closing inventory removes the browser in the same UI lifecycle.

The browser fits the game's backbuffer every frame, so any resolution, aspect
ratio, fullscreen mode or window resize works. It scales by the tighter of
height/1080 and width/1920 (0.5x to 2x) and never pushes the native inventory
further than its free margin on the other side. On 16:10 it shrinks to fit
beside the inventory; on 4:3 and 5:4 there is no room beside it, so the browser
sits over the inventory's edge (F8 hides it). Clicks are mapped from window
pixels to backbuffer pixels. Each new size logs a `LAYOUT_FIT` line.

**Add** is available only in Sandbox and Creative. Survival and Career remain
lookup-only. Requests use the game's native event path; server rules still decide
the outcome. A full inventory may cause the native game to place the item in the world.

AnyHelpers provides optional settings and keybinds. Previews approximate materials,
textures and animated poses. See [Item catalog](../api/item-catalog.md),
[Item images](../api/item-images.md), [Inventory actions](../api/inventory-actions.md)
and [Screen layout](../api/screen-layout.md) for implementation contracts.
