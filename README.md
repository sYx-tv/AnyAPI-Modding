# AnyAPI

An in-game mod framework for **Anymaker**, with a simple Windows mod manager.

**Public mod library.** Source and downloads are available without a GitHub account. Manager 1.2.0 includes the library address automatically; players do not enter repository URLs or install GitHub CLI.

[**Download AnyAPI Manager**](https://github.com/sYx-tv/AnyAPI-Modding/releases/latest/download/AnyAPI.Manager.exe) · [All release downloads](https://github.com/sYx-tv/AnyAPI-Modding/releases/latest)

## Play with mods

1. Download and open **AnyAPI Manager.exe**.
2. Close Anymaker and select **Install API**. The Steam folder is normally detected automatically.
3. Open **Mods**, select the ones you want and click **Install**.
4. Choose **Play with mods** on Overview to launch through Steam.

The manager bundles AnyAPI, offline documentation and a source-only starter SDK. Optional mods download separately. The manager does not need to stay open while playing. Updates, toggles and removals take effect on the next launch. Saved settings and markers are kept.

**Play without mods** pauses AnyAPI without changing your mod selections. Normal Steam launches stay in this mode until you choose **Play with mods** again.

**Local mods** appear beside library mods. Use Import DLL or put an AnyAPI DLL in the mods folder and select Refresh. Enable, disable and remove work for handmade mods too. Optional companion metadata declares a name, version and minimum API revision. Local compatibility remains unverified; updates are manual. The manager does not provide custom load-order or dependency resolution.

**Develop** contains searchable services, headers, integration hooks, contracts and examples. Export a starter SDK to build your own C++ DLL. Disabled historical hooks are clearly separated from current services.

## Available mods

| Mod | What it adds |
| --- | --- |
| **AnyHelpers** | Native Mod Controls and Mod Settings tabs for registered mod options and keybinds. |
| **AnyInventory** | Native inventory item browser with images, search, categories and favorites. Add is limited to Sandbox and Creative. |
| **AnyStorage** | Deposit, withdraw, transfer matching items and sort eligible storage inventories. |
| **AnyMap** | World map, smooth minimap, named markers and road guidance. |

## Compatibility

Current release: **API revision 25**, verified for **Anymaker 0.1.21 / Steam build 25725299** on Windows x64. The installed executable and game-data hashes must match a verified release.

After a game update, use **Check for updates**. New native hooks must be reviewed and tested before a compatible API release can be published. The manager does not claim compatibility merely because a version number changed.

Download checksums, file checksums, automatic backups and installation rollback are built into the manager. It asks before replacing an unmanaged DLL or temporarily disabling unverified mods during an API update.

## Develop or publish

- [Build the current API and mods](SOURCE_BUILD.md)
- [API and mod documentation](Phase34/MODS_AND_API.md)
- [Manager source and build instructions](manager/README.md)
- [Catalog format](manager/CATALOG_FORMAT.md)
- [Publishing instructions](manager/GITHUB_SETUP.md)

The native API and mods use C++ and Visual Studio's C++ tools. The manager uses C#, Windows Forms and .NET Framework. Source, headers, examples and tests are included; game textures, models and executable files are not included in the source tree.

Manager 1.2.0 checks pass **73/73**, plus **7/7** anonymous HTTPS catalog/package download checks. The exported starter builds as an x64 DLL with the expected entry point. Launch switching was tested against disposable game fixtures; Steam gameplay was not launched during this manager refresh. The native revision 25 checks pass **27/27**; the current mods were also accepted in the author's host gameplay testing. Joining-client coverage is separate and should not be inferred from those results.

