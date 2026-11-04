# AnyVehicleOverhaul 2.0.0

AnyEngineParts and AnyMoto are now one mod, **AnyVehicleOverhaul**. It carries everything in AnyEngineParts 1.0.6
(turbos, superchargers, upkeep parts, rotary, Dyno, EngineSound 0.4.3, GpsPart 1.0.4) and AnyMoto 0.0.4, unchanged.

- **Upgrading:** the manager shows AnyVehicleOverhaul as an update for AnyEngineParts and AnyMoto and removes their
  DLLs when it installs. By hand: delete `AnyEngineParts.dll` and `AnyMoto.dll`, add `AnyVehicleOverhaul.dll`.
- **Nothing to migrate:** mod ids, part ids, data folders and saved files are unchanged, so vehicles, worlds, Mod
  Settings, keybinds and on/off switches carry over. In Mod Settings the pieces keep their names (AnyEngineParts,
  EngineSound, GpsPart, AnyMoto).
- If an old DLL is left in the mods folder, AnyAPI runs it instead of the matching bundled piece, so nothing is
  hooked twice.
- Details: [AnyVehicleOverhaul](../mods/vehicle-overhaul.md).

Publishing: upload `release.json` from [anyvehicleoverhaul-2.0.0/](anyvehicleoverhaul-2.0.0/release.json) with the
ZIP. AnyEngineParts and AnyMoto must not be in the same release (the workflow removes their catalog entries).
