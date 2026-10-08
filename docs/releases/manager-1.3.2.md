# AnyAPI Manager 1.3.2

Bundles AnyAPI 0.33.0 for Anymaker 0.1.23 with the matching 35-header, 124-article developer guide and starter SDK. The built-in catalog lists all eight optional mods, including AnyBalance 1.0.0; no optional mod DLLs are bundled in the EXE.

Fixes three garbled separators in the Overview text (for example "Anymaker 0.1.23 · Verified build").

The self-test no longer hardcodes the mod count, header count or API revision. It now checks that the bundled API ZIP, catalog and guide match each other, and `build.ps1` embeds the API ZIP named by the bundled catalog. All 94 manager fixture checks pass.

Existing 1.3.0 and 1.3.1 users can choose Check for updates in Settings, then Update & restart.
