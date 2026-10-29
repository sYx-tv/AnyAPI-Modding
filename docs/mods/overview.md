# Mod library

**Bundles (AnyAPI 0.39.0, Manager 1.5.0).** Most mods now ship inside four bundles,
so the library lists fewer entries: **AnyEngineParts** (with EngineSound and
GpsPart), **AnyHelpers** (with AnyInventory, AnyMirror, AnyBalance and MCCode),
**AnyGraphics** (with ThirdPerson) and **AnyMap**. Every bundled mod works exactly as
before and keeps its settings, keybinds, saved vehicles and worlds. Each one has an
**Enabled** row at the top of its group in Mod Settings (applies the next time you
start the game). Updating a bundle in the manager removes the old separate DLLs. See
[Bundles](../api/bundles.md).

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
| [AnyGraphics](graphics.md) | 0.31.0 | API 0.36.0 | Native graphics presets, scene AA, volumetric fog and light beams, photo mode with a free camera | None; lives in Settings → Graphics |
| [AnyClock](clock.md) | 1.0.0 | API 0.30.0 | Temporary game-time popup | AnyHelpers settings and controls |
| [AnyQuickWheel](quick-wheel.md) | 1.1.0 | API 0.31.0 | Hold-to-open construction-tool wheel | AnyHelpers settings and controls |
| [AnyBalance](balance.md) | 1.2.0 | API 0.33.0 (fluid mass: 0.34.0); on Anymaker 0.1.25, API 0.35.1 | Centre-of-mass overlay, mass and size with the Properties Tool | AnyHelpers settings |
| [EngineSound](engine-sound.md) | 0.2.2 | API 0.34.0 | Engine sound presets and a Custom tuner in the Properties Tool, synced in co-op | None |
| [AnyMirror](mirror.md) | 1.1.1 | API 0.34.0 | Mirrored edge building with either edge tool, a toggleable mirror wall and keybinds | AnyHelpers settings and controls |
| [GpsPart](gps.md) | 1.0.1 | API 0.35.0; single player | A new GPS Sensor part: position, heading, pitch, roll, speed, acceleration and waypoint outputs for the data port and microcontroller | None |
| [ThirdPerson](third-person.md) | 0.1.3 | API 0.35.0 | Third-person camera on F5 with an adjustable follow distance (Alt + mouse wheel) | AnyHelpers settings and controls |
| [MCCode](mccode.md) | 0.2.0 | API 0.38.0 | A Code view in the microcontroller editor: type the program or build it from nodes and wires, with a command list and the game's debugger | None; `</> Code` in the microcontroller editor |
| [CustomGame](custom-game.md) | 0.1.0 | API 0.40.0 | A Custom Game page on the New Game screen: day cycle, starting tier, up to 20 players, survival, enemy and world settings saved per world | Map packs from CustomMap |
| [AnyMoto](moto.md) | 0.0.1 (test) | API 0.40.1 | Motorcycle fork, swingarm, seat, handlebars and Lean Assist for the Motorcycle Wheel | None |

All mods are in the published catalog ([`catalog.json`](../../catalog.json)).
All versions target Anymaker 0.1.28 / Steam build 26277967. On that build every mod needs AnyAPI 0.41.0 or newer, and the experimental-SDK mods ship
in the AnyHelpers 1.0.2, AnyEngineParts 1.0.2, AnyGraphics 1.0.3 and AnyMap 1.0.1 bundles and AnyCombat 0.1.1. Anymaker 0.1.25 keeps AnyAPI 0.39.2,
0.1.24 keeps 0.36.0 and 0.1.23 keeps 0.34.1.

## Prototypes

Built from source only, not in the catalog yet. They use the experimental SDK.

| Mod | Features | Optional integration |
| --- | --- | --- |
| [AnyLights](lights.md) | One paint tool for every colour (palette panel, modes, Ctrl+Z undo); lights glow their paint colour | AnyHelpers settings |
| [AnyEngineParts](engine-parts.md) | Exhaust-driven turbochargers (3x3x2 and 6x6x2) and superchargers with blow-off valves, wastegate antilag and boost channels, and a stretchable 1-4 rotor rotary engine, each with synthesized sounds; single player | Rotary works alongside EngineSound |
| [AnyCombat](combat.md) | Hit markers and floating damage numbers for the hits you deal, red on a kill | AnyHelpers settings |
| [AnyBattery](battery.md) | The Battery Pack: a battery you place and stretch to size like a liquid tank, placed empty and charged from the electric network; saved charge, charge_percent output | AnyHelpers settings |
| [AnyCVT](cvt.md) | Small, Medium and Large CVTs: the stock gearbox of each size with R/N/D on the shifter link, a ratio that slides to hold a target RPM and a centrifugal clutch; ratio, engine_rpm, target_rpm and current_gear (-1/0/1) outputs | |
| [AnyPerformance](performance.md) | Smooths the world's slow-motion and catch-up when the simulation stalls, logs what stalled it and every frame hitch, and keeps the server's 60 ticks a second evenly spaced | AnyHelpers settings |

See [validation status](../development/validation.md) for tested behavior and limits,
including what has and has not been checked in multiplayer.
