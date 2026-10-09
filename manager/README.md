# AnyAPI Manager

A Windows app with four pages: Overview, Mods, Develop, and Settings. Version 1.4.0 includes AnyAPI revision 33, catalog metadata, an offline developer guide, and a source-only starter SDK. It contains **no optional mod DLLs**. Mods are independent GitHub downloads.

The manager recognizes newer compatible installed API/mod receipts even when
its cached catalog is older. The bundled API and offline guide match 0.33.0.
Settings includes an independent manager EXE update check.

Open **AnyAPI Manager.exe**. The Steam game folder is normally detected automatically. Install/update the API, then choose mods in Mods. The official mod library is built in. If the game isn't found, or it lives in a custom location, open Settings and choose **Browse** to pick `game.exe`, paste the folder path and choose **Save folder**, or choose **Find automatically**. A Steam library folder also works; the manager finds `steamapps\common\Anymaker` inside it. Close the game before changing DLLs or switching launch modes.

**Play with mods** restores the verified API loader and launches through Steam. **Play without mods** moves only that loader into the manager's backup area, leaving mod DLLs, individual enable/disable choices and saved data alone. The mode persists: normal Steam launches also run without AnyAPI until you choose Play with mods. The manager can be closed while playing. If Steam cannot open, the manager attempts to restore the preceding mode. Other third-party loaders are outside this manager's scope.

**Develop** has searchable services, exact headers, native integration callbacks, contract notes, and examples. Export starter SDK gives a CMake C++ DLL project, the public/reference headers and examples. Historical legacy hooks are clearly marked disabled; they are not advertised as working services. Settings and keybinds require explicit registration with optional AnyHelpers services. The bundled offline documentation describes API 0.33.0; newer installations display a version notice. Current source documentation is under [docs](../docs/README.md).

This project is public at **sYx-tv/AnyAPI-Modding**. Browsing and downloads use ordinary HTTPS. No GitHub account, GitHub CLI or repository address entry is required. Saved repository preferences from older versions are overridden by the bundled catalog address; the saved game folder is retained. Downloads never invoke the private GitHub CLI fallback. No credentials are bundled.

The manager verifies the installed `game.exe` and `bin/game.gcl` against each release. If Steam changes the build, the app continues to work and checks the repository for a matching API release. It cannot automatically repair native hooks: the developer must review the new build, test the API and publish a verified update.

Downloads use HTTPS and SHA-256 checks for both the archive and each DLL. Only catalog-declared x64 DLL paths are installed. Changed files are backed up; failed transactions restore the prior DLLs and manager receipts. Saved mod settings, favorites, markers and game saves are not part of packages and are retained. Unmanaged/changed DLL replacement requires an explicit choice in the app. Windows administrator approval is requested only if the installation folder requires it.

Disable/enable takes effect on the next game launch. Remove deletes the mod DLL and keeps its saved settings. The manager stays closed during gameplay if you prefer; the game loads DLL mods itself.

The last connected catalog is saved locally so the browser remains available while offline. New downloads and online update checks still require a connection.

When a game update leaves active mods unverified, updating the API asks whether to temporarily disable those DLLs. Their data is preserved. Install matching mod releases to enable them again. Play with mods checks game compatibility, minimum API revisions and enabled mod DLLs before opening Steam. Play without mods remains available after a game update.

## Windows 10 and 11

One manager EXE runs on both Windows 10 and Windows 11 (64-bit). There is no separate
Windows 10 build. It needs .NET Framework 4.7.2 or newer, which every supported
Windows 10 release (1803 and later) and every Windows 11 release already include.

If installing the API or a mod fails, the manager writes the reason to
`%LOCALAPPDATA%\AnyAPI Manager\manager.log`: the Windows build, .NET Framework
release, detected game folder, `game.exe`/`game.gcl` hashes and the full error. Choose
**Open log** in Settings > Manager to open it, and attach the file to a bug report.

Build: Windows with .NET Framework 4.7.2 or newer, `powershell -File manager/build.ps1`. No downloaded NuGet packages or standalone .NET installation is required on modern Windows. The source targets x64 and uses C#, Windows Forms and the Windows .NET Framework compiler.

To refresh the bundle for a new API release, copy the root `catalog.json` to `publishing/catalog.json`, put that release's API ZIP in `publishing/assets/`, and run `python manager/prepare_guide.py`. `build.ps1` embeds the API ZIP named by the bundled catalog, and the self-test checks the ZIP, catalog and guide agree.

Validation: run the EXE with `--self-test <absolute output.json>`; run `--capture <absolute preview.png>` to render its own interface for review. These tests use disposable fixture folders, never the installed game. See CATALOG_FORMAT.md and GITHUB_SETUP.md for publishing.

## Look and layout

Version 1.4.0 has a frameless dark window: a slim title bar (drag to move, double-click
to maximize, drag any edge to resize), a sidebar grouped into Game, Library, Build and
Manager, and pages built from titled panels. Mods switch on and off from the list.
Each mod has an illustrated tile, its category, latest and installed versions and a
link to its release. **Enable all** and **Disable all** switch every installed mod on or off.
**Install all** adds every compatible mod you don't have yet (and
AnyAPI first if needed), **Update all** updates installed mods with newer releases,
and **Update everything** on Overview updates AnyAPI, every installed mod and
then the manager itself. A failure is logged and skipped so the rest still install.
Settings > Appearance picks the accent colour (Aqua, Violet, Rose, Amber or Mint).
Everything is drawn with Windows Forms and GDI+, uses Segoe UI and Consolas, and scales
with the Windows display setting, so Windows 10 and Windows 11 look the same. Shared
styling lives in `Theme.cs`, `ModernButton.cs` and `Controls.cs`.

## Manager updates

The manager checks for a newer version of itself each time it opens. The check is
silent when offline or already up to date. When an update exists, **Manager X.Y.Z is
ready** appears in the title bar with a **Restart to update** button; one click
downloads it (with progress), then the manager closes and reopens as the new version.
Settings > Manager has a switch to turn the launch check off. Settings
also has **Check for updates** under Manager, which changes to **Update & restart**
when one is available. No website download is needed.

Every update downloads the official EXE and checks its size, SHA-256, x64
executable format and assembly version before anything is replaced. A separate
helper waits for the current process to exit; it never terminates it forcibly.

The old EXE is kept beside the manager as a `.bak` file until the new version has
started; the new version then deletes that backup and its download folder.
Replacement or restart failure restores the old EXE. Mods, API DLLs, preferences
and saved game data are untouched.
Windows asks for administrator approval only when the manager's own folder needs
it. An offline check leaves the current EXE in place. Managers 1.3.0 to 1.3.2 update
through Settings once; older managers need a one-time download of a newer one.

For front ends: `ManagerUpdater` (`ManagerUpdater.cs`) holds the whole flow with no
UI code. Bind to its `Changed` event and read `State`, `Release`, `Progress`
(0 to 1, or -1 when the size is unknown) and `Message`. Call `Check(true)` on
launch, `Install()` when the user accepts, and close the app once `State` is
`Restarting`. `ManagerUpdateBadge` is the title-bar control for it.

Manager releases use root `manager-update.json`, separately from the mod/API
catalog. See [Publishing](../docs/development/publishing.md) for release steps.

## Local mods

Mods shows Library and Local sources. Import DLL accepts an x64 AnyAPI mod, and Refresh discovers DLLs placed directly in the mods folder. Local mods support enable, disable and removal, with backups and saved settings preserved. Optional `ExampleMod.anymod.json` declares Name, Version, Description and MinimumApi; see the offline guide and starter SDK. Compatibility is shown as unverified, and local mods are not automatically downloaded or updated. The manager inspects DLL exports without executing them. This is a format check, not proof the mod is safe or game-compatible.

The loader sorts DLL filenames; this manager does not promise dependency resolution or user-defined load order. The launch controls now also handle requests without a package, as the real UI sends them.
