# Mod library

Every mod is an independent DLL. AnyAPI supplies shared integration services;
each mod owns its interface, behavior and settings policy.

| Mod | Features | Optional integration |
| --- | --- | --- |
| [AnyHelpers](helpers.md) | Native settings and rebindable controls | Hosts other mods' registered options |
| [AnyInventory](inventory.md) | Search, images, categories, favorites and item descriptions | AnyHelpers settings and controls |
| [AnyStorage](storage.md) | Deposit, withdraw, matching transfer and sorting | AnyInventory layout; AnyHelpers settings |
| [AnyMap](map.md) | Full map, movable minimap, markers and route guidance | AnyHelpers settings and controls |
| [AnyGraphics](graphics.md) | Presets and custom graphics effects | AnyHelpers editable settings |

The published catalog contains the first four mods. AnyGraphics and the compact
settings update are included in current source, ahead of the stable catalog.
See [validation status](../development/validation.md) for tested behavior and limits.
