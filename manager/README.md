# AnyAPI Manager

A small Windows app with three pages: API, Mods, and Settings. The EXE contains AnyAPI and catalog metadata. It contains **no mod DLLs**. Mods are independent, optional GitHub downloads.

Open **AnyAPI Manager.exe**. The Steam game folder is normally detected automatically. Install/update the API, then choose mods in Mods. Settings connects a public GitHub repository and selects another game folder if needed. Launch game opens Anymaker through Steam. Close the game before changing DLLs.

The manager verifies the installed `game.exe` and `bin/game.gcl` against each release. If Steam changes the build, the app continues to work and checks the repository for a matching API release. It cannot automatically repair native hooks: the developer must review the new build, test the API and publish a verified update.

Downloads use HTTPS and SHA-256 checks for both the archive and each DLL. Only catalog-declared x64 DLL paths are installed. Changed files are backed up; failed transactions restore the prior DLLs and manager receipts. Saved mod settings, favorites, markers and game saves are not part of packages and are retained. Unmanaged/changed DLL replacement requires an explicit choice in the app. Windows administrator approval is requested only if the installation folder requires it.

Disable/enable takes effect on the next game launch. Remove deletes the mod DLL and keeps its saved settings. The manager stays closed during gameplay if you prefer; the game loads DLL mods itself.

The last connected catalog is saved locally so the browser remains available while offline. New downloads and online update checks still require a connection.

When a game update leaves active mods unverified, updating the API asks whether to temporarily disable those DLLs. Their data is preserved. Install matching mod releases to enable them again. The Launch game button also checks enabled mods before opening Steam.

Build: Windows with .NET Framework 4.7.2 or newer, `powershell -File manager/build.ps1`. No downloaded NuGet packages or standalone .NET installation is required on modern Windows. The source targets x64 and uses C#, Windows Forms and the Windows .NET Framework compiler.

Validation: run the EXE with `--self-test <absolute output.json>`; run `--capture <absolute preview.png>` to render its own interface for review. These tests use disposable fixture folders, never the installed game. See CATALOG_FORMAT.md and GITHUB_SETUP.md for publishing.
