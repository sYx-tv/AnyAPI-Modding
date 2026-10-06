# AnyAPI for Anymaker

AnyAPI loads C++ DLL mods inside Anymaker. AnyAPI Manager installs the framework,
browses the mod library, manages local mods and launches the game with or without mods.

[Download the manager](https://github.com/sYx-tv/AnyAPI-Modding/releases/latest/download/AnyAPI.Manager.exe) · [Documentation](docs/README.md) · [Developer SDK](sdk/README.md) · [Build from source](docs/development/building.md)

## For players

1. Download and open **AnyAPI Manager.exe**.
2. Close Anymaker, select its Steam folder, then choose **Install API**.
3. Open **Mods**, install the mods you want, then choose **Play with mods**.

The manager includes the API; optional mods download separately. It does not need
to remain open during gameplay. **Play without mods** pauses the API loader and
retains installed mods and saved settings. Local AnyAPI DLLs can be imported or
discovered from the game's mods folder.

## Included projects

| Project | Purpose | Source |
| --- | --- | --- |
| AnyAPI | DLL loading, versioned services and native integration | [native](native/) |
| AnyAPI Manager | Installation, updates, mod discovery and launch modes | [manager](manager/) |
| AnyHelpers | Native Mod Controls and Mod Settings | [Guide](docs/mods/helpers.md) |
| AnyInventory | Item search, previews, favorites and mode-limited Add | [Guide](docs/mods/inventory.md) |
| AnyStorage | Storage transfers, matching stacks and sorting | [Guide](docs/mods/storage.md) |
| AnyMap | World map, rotating minimap, markers and road guidance | [Guide](docs/mods/map.md) |
| AnyGraphics | Graphics presets, bloom, sharpening and color controls | [Guide](docs/mods/graphics.md) |

## Source and downloads

This repository contains the API and all project sources listed above,
public SDK headers, examples, tests and documentation. Game executables, models,
textures and the generated road cache are excluded. A compatible Anymaker
installation is needed to build and test game-dependent features.

The published manager and mod catalog currently distribute **AnyAPI 0.25.0** and
four optional mods. The source includes **AnyAPI 0.27.0 candidate** and **AnyGraphics / AnyHelpers
0.26.1**. These graphics changes are available in source but have not yet replaced
the stable download catalog. The initial graphics effects were confirmed working;
the new compact preset menu and 0.1.23 compatibility still await in-game acceptance.

## Compatibility

The current native profile targets **Anymaker 0.1.23**, Steam build **25755694**,
on **Windows x64**. Exact executable and game-data fingerprints are recorded in
[the build manifest](native/BUILD_MANIFEST.json). API ABI/service version numbers
are compatibility contracts and remain part of SDK names.

After a game update, use **Check for updates**. Native integration must be reviewed
and validated before a matching API build is published. Changing a version or hash
alone does not establish compatibility.

## For developers

- [Build the API and mods](docs/development/building.md)
- [Start a DLL mod](docs/development/first-mod.md)
- [API service reference](docs/api/README.md)
- [Register settings and controls](docs/mods/helpers.md)
- [Publish a mod](docs/development/publishing.md)
- [Validation and release status](docs/development/validation.md)

Mods implement behavior; AnyAPI exposes reusable mechanisms. AnyHelpers is an
optional provider of settings and keybind registries. Mods must explicitly register
their editable options and use the returned values in their implementation.
