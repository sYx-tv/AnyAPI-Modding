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
| [AnyBalance](balance.md) | 1.2.0 | API 0.33.0 (fluid mass: 0.34.0); on Anymaker 0.1.25, API 0.35.1 | Centre-of-mass overlay, mass and size with the Properties Tool | AnyHelpers settings |
| [EngineSound](engine-sound.md) | 0.2.2 | API 0.34.0 | Engine sound presets and a Custom tuner in the Properties Tool, synced in co-op | None |
| [AnyMirror](mirror.md) | 1.1.1 | API 0.34.0 | Mirrored edge building with either edge tool, a toggleable mirror wall and keybinds | AnyHelpers settings and controls |
| [GpsPart](gps.md) | 1.0.1 | API 0.35.0; single player | A new GPS Sensor part: position, heading, pitch, roll, speed, acceleration and waypoint outputs for the data port and microcontroller | None |
| [ThirdPerson](third-person.md) | 0.1.3 | API 0.35.0 | Third-person camera on F5 with an adjustable follow distance (Alt + mouse wheel) | AnyHelpers settings and controls |

All mods are in the published catalog ([`catalog.json`](../../catalog.json)).
All versions target Anymaker 0.1.25 / Steam build 25904830. On that build every mod needs AnyAPI 0.37.0 or newer (AnyInventory lists modded items with 0.38.0), and the experimental-SDK mods need their 0.1.25 builds
(AnyMirror 1.1.1, EngineSound 0.2.2, GpsPart 1.0.1, ThirdPerson 0.1.1). Anymaker 0.1.24 keeps AnyAPI 0.36.0 and 0.1.23 keeps 0.34.1.

## Prototypes

Built from source only, not in the catalog yet. They use the experimental SDK.

| Mod | Features | Optional integration |
| --- | --- | --- |
| [AnyLights](lights.md) | Light colours and flash patterns in the Properties Tool, with data port and microcontroller channels | AnyHelpers settings |
| [AnyEngineParts](engine-parts.md) | Exhaust-driven turbochargers (3x3x2 and 6x6x2) and superchargers with blow-off valves, wastegate antilag and boost channels, and a stretchable 1-4 rotor rotary engine, each with synthesized sounds; single player | Rotary works alongside EngineSound |

See [validation status](../development/validation.md) for tested behavior and limits,
including what has and has not been checked in multiplayer.
