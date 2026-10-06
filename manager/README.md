# AnyAPI Manager

A Windows app with four pages: Overview, Mods, Develop, and Settings. Version 1.1.1 includes AnyAPI revision 25, catalog metadata, an offline developer guide, and a source-only starter SDK. It contains **no optional mod DLLs**. Mods are independent GitHub downloads.

Open **AnyAPI Manager.exe**. The Steam game folder is normally detected automatically. Install/update the API, then choose mods in Mods. The official mod library is built in; Settings selects another game folder if needed. Close the game before changing DLLs or switching launch modes.

**Play with mods** restores the verified API loader and launches through Steam. **Play without mods** moves only that loader into the manager's backup area, leaving mod DLLs, individual enable/disable choices and saved data alone. The mode persists: normal Steam launches also run without AnyAPI until you choose Play with mods. The manager can be closed while playing. If Steam cannot open, the manager attempts to restore the preceding mode. Other third-party loaders are outside this manager's scope.

**Develop** has searchable services, exact headers, native integration callbacks, contract notes, and examples. Export starter SDK gives a CMake C++ DLL project, all 21 public/reference headers and examples. Historical legacy hooks are clearly marked disabled; they are not advertised as working services. Settings and keybinds require explicit registration with optional AnyHelpers services. The documentation belongs to revision 25; newer API installations display a version notice.

This project is public at **sYx-tv/AnyAPI-Modding**. Browsing and downloads use ordinary HTTPS. No GitHub account, GitHub CLI or repository address entry is required. Saved repository preferences from older versions are overridden by the bundled catalog address; the saved game folder is retained. Downloads never invoke the private GitHub CLI fallback. No credentials are bundled.

The manager verifies the installed `game.exe` and `bin/game.gcl` against each release. If Steam changes the build, the app continues to work and checks the repository for a matching API release. It cannot automatically repair native hooks: the developer must review the new build, test the API and publish a verified update.

Downloads use HTTPS and SHA-256 checks for both the archive and each DLL. Only catalog-declared x64 DLL paths are installed. Changed files are backed up; failed transactions restore the prior DLLs and manager receipts. Saved mod settings, favorites, markers and game saves are not part of packages and are retained. Unmanaged/changed DLL replacement requires an explicit choice in the app. Windows administrator approval is requested only if the installation folder requires it.

Disable/enable takes effect on the next game launch. Remove deletes the mod DLL and keeps its saved settings. The manager stays closed during gameplay if you prefer; the game loads DLL mods itself.

The last connected catalog is saved locally so the browser remains available while offline. New downloads and online update checks still require a connection.

When a game update leaves active mods unverified, updating the API asks whether to temporarily disable those DLLs. Their data is preserved. Install matching mod releases to enable them again. Play with mods checks game compatibility, minimum API revisions and enabled mod DLLs before opening Steam. Play without mods remains available after a game update.

Build: Windows with .NET Framework 4.7.2 or newer, `powershell -File manager/build.ps1`. No downloaded NuGet packages or standalone .NET installation is required on modern Windows. The source targets x64 and uses C#, Windows Forms and the Windows .NET Framework compiler.

Validation: run the EXE with `--self-test <absolute output.json>`; run `--capture <absolute preview.png>` to render its own interface for review. These tests use disposable fixture folders, never the installed game. See CATALOG_FORMAT.md and GITHUB_SETUP.md for publishing.
