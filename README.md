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
| AnyGraphics | Native graphics, scene AA and volumetric lighting | [Guide](docs/mods/graphics.md) |
| AnyQuickWheel | Automatic inventory construction-tool wheel | [Guide](docs/mods/quick-wheel.md) |
| AnyClock | Configurable temporary game-time HUD | [Guide](docs/mods/clock.md) |

## Source and downloads

This repository contains the API and all project sources listed above,
public SDK headers, examples, tests and documentation. Game executables, models,
textures and the generated road cache are excluded. A compatible Anymaker
installation is needed to build and test game-dependent features.

The current release includes **AnyAPI 0.32.0** and **AnyQuickWheel 1.1.0**.
AnyGraphics is **0.29.1**. AnyHelpers,
AnyInventory, AnyStorage and AnyMap remain at **0.27.0**. **Manager 1.3.1**
bundles API 0.27.0 and downloads the newer API through its update check;
optional mods download individually. The native suite passes 47 checks.
See [release notes](docs/releases/release-0.32.0.md).

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

## Expanded modding SDK

Use the [experimental SDK](sdk/experimental/README.md) for discovered game types,
functions, globals and hook pathways beyond the supported API services. Download
the [complete SDK and reference](https://github.com/sYx-tv/AnyAPI-Modding/releases/tag/mods-2026.10.08.1)
for the generated bindings and machine-readable database.

### Complete research reference downloads

Both supplied reference bundles, including analysis tools and validation reports, are available from [Complete supplied SDK references](docs/sdk-reference/README.md).

### Continuing development in another workspace

See [Workspace handoff](docs/development/workspace-handoff.md) for requirements, SDK downloads, build commands and the current AnyBalance test status. AnyBalance and its API 0.33.0 support are included in source; the stable download catalog remains separate.
