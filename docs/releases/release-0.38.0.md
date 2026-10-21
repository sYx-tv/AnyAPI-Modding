# AnyAPI 0.38.0 and AnyInventory 0.28.0

For Anymaker 0.1.25 (Steam build 25904830). Update both in the manager, then restart the game.

## AnyInventory 0.28.0

- **Fits any screen.** The item browser now sizes itself to the game's resolution on
  every frame, by the tighter of height/1080 and width/1920. It no longer pushes the
  inventory, or the storage buttons inside it, off the edge of 16:10, 4:3 or 5:4 screens.
  On 16:10 it shrinks to fit beside the inventory. On 4:3 and 5:4 it sits over the
  inventory's edge (F8 hides it). 16:9 screens look the same as before.
- Clicks line up when fullscreen or Windows display scaling stretches the game.
- **Modded items.** Parts and items that other mods add, such as the GPS Sensor, appear
  in the browser once a world is loaded and the inventory is open. **Add** works on them
  in Creative and Sandbox. This needs AnyAPI 0.38.0. With an older AnyAPI, the browser
  works as before without them.

## AnyAPI 0.38.0

- `anyapi.item_catalog` now also lists definitions that mods add to the loaded world.
  While the inventory is open, AnyAPI reads the world's inventory and vehicle component
  definition lists and appends any ID the game's asset files did not have. Entries only
  append, so indices stay stable; consumers poll `count()` to see new ones. See
  [Item catalog](../api/item-catalog.md#mod-added-definitions).
- No other service changes.

EngineSound 0.3.0 ships in the same round; see [its notes](enginesound-0.3.0.md).
