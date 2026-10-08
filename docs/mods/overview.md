# Mod library

Every mod is an independent DLL. AnyAPI supplies shared integration services;
each mod owns its interface, behavior and settings policy. No mod requires another
mod: AnyHelpers adds settings and keybind tabs when present, and every other mod
keeps working defaults when it is absent.

| Mod | Stable version | Requires | Features | Optional integration |
| --- | --- | --- | --- | --- |
| [AnyHelpers](helpers.md) | 0.27.0 | API 0.27.0 | Native Mod Controls and Mod Settings tabs | Hosts other mods' registered options |
| [AnyInventory](inventory.md) | 0.27.0 | API 0.27.0 | Item search, images, categories, favorites, descriptions; Add in Sandbox/Creative | AnyHelpers settings and controls |
| [AnyStorage](storage.md) | 0.27.0 | API 0.27.0 | Deposit, withdraw, matching transfer and sorting | AnyInventory layout; AnyHelpers settings |
| [AnyMap](map.md) | 0.27.2 | API 0.27.0 | Full map, movable minimap, markers and route guidance | AnyHelpers settings and controls |
| [AnyGraphics](graphics.md) | 0.29.3 | API 0.32.0 | Native graphics presets, scene SMAA, volumetric fog and light beams | None; lives in Settings → Graphics |
| [AnyClock](clock.md) | 1.0.0 | API 0.30.0 | Temporary game-time popup | AnyHelpers settings and controls |
| [AnyQuickWheel](quick-wheel.md) | 1.1.0 | API 0.31.0 | Hold-to-open construction-tool wheel | AnyHelpers settings and controls |
| [AnyBalance](balance.md) | In development | API 0.33.0 (source only) | Centre-of-mass overlay with the Properties Tool | AnyHelpers settings |

All stable mods are in the published catalog ([`catalog.json`](../../catalog.json)).
AnyBalance and API 0.33.0 are in source only and stay out of the catalog until
their live-world check passes. All versions target Anymaker 0.1.23 / Steam build
25755694.

See [validation status](../development/validation.md) for tested behavior and limits,
including what has and has not been checked in multiplayer.
