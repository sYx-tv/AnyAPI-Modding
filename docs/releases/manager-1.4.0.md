# AnyAPI Manager 1.4.0

A redesigned manager that updates itself. Bundles AnyAPI 0.34.1 for Anymaker 0.1.23 with the matching developer guide and starter SDK. The built-in catalog lists all nine optional mods, including EngineSound 0.2.0; no optional mod DLLs are bundled in the EXE.

## New look

- A frameless dark window with a grouped sidebar (Game, Library, Build, Manager), titled panels, a status bar and a recent-activity log. It resizes, snaps and maximizes like a normal window and scales with Windows display settings.
- Every mod has an illustrated tile and a details panel with its category, latest and installed versions, required API and a link to its release.
- Settings has an accent picker (Aqua, Violet, Rose, Amber, Mint).

## Updates in one place

- Mods: Install all, Update all, Enable all and Disable all.
- Overview: Update everything installs the newer API, every mod update and the manager update in one go.
- The manager checks for its own update at launch. When one exists, a chip in the title bar says "Manager X is ready"; Restart to update downloads it, verifies it and restarts into it. The launch check can be switched off in Settings, and Check for updates still works by hand.

## Windows 10 and custom game folders

- Settings has a Game folder box: Browse to `Anymaker.exe`, Find automatically, or type a folder. Steam libraries on other drives are found automatically.
- The manager forces TLS 1.2 for downloads on older Windows 10 builds and writes a log to `%LOCALAPPDATA%\AnyAPI Manager\manager.log`. Settings has an Open log button.

Users on 1.3.x can choose Check for updates in Settings, then Update & restart. From 1.4.0 on, updates are offered at launch.
