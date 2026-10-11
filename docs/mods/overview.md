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
| [AnyBalance](balance.md) | 1.2.0 | API 0.33.0 (fluid mass: 0.34.0); on Anymaker 0.1.24, API 0.35.1 | Centre-of-mass overlay, mass and size with the Properties Tool | AnyHelpers settings |
| [EngineSound](engine-sound.md) | 0.2.1 | API 0.34.0 | Engine sound presets and a Custom tuner in the Properties Tool, synced in co-op | None |
| [AnyMirror](mirror.md) | 1.0.0 | API 0.34.0 | Mirrored edge building with either edge tool, a toggleable mirror wall and keybinds | AnyHelpers settings and controls |
| [GpsPart](gps.md) | 1.0.0 | API 0.35.0; single player | A new GPS Sensor part: position, heading, pitch, roll, speed, acceleration and waypoint outputs for the data port and microcontroller | None |

All mods are in the published catalog ([`catalog.json`](../../catalog.json)).
All versions target Anymaker 0.1.24 / Steam build 25826614. On that build every mod needs AnyAPI 0.35.0 or newer (AnyBalance needs 0.35.1, and 0.35.2 fixes a container inventory crash);
Anymaker 0.1.23 keeps AnyAPI 0.34.1.

## Prototypes

Built from source only, not in the catalog yet. It uses the experimental SDK.

| Mod | Features | Optional integration |
| --- | --- | --- |
| [AnyLights](lights.md) | Light colours and flash patterns in the Properties Tool, with data port and microcontroller channels | AnyHelpers settings |

See [validation status](../development/validation.md) for tested behavior and limits,
including what has and has not been checked in multiplayer.
