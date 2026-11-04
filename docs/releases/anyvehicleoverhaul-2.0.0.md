# AnyVehicleOverhaul 2.0.0

AnyEngineParts, AnyMoto and AnyCVT are now one mod, **AnyVehicleOverhaul**. It carries everything in AnyEngineParts
1.0.6 (turbos, superchargers, upkeep parts, rotary, Dyno, EngineSound 0.4.3, GpsPart 1.0.4), AnyMoto 0.0.4 and
AnyCVT 0.0.1, plus:

- **No boost cap.** Turbos hold their 12.8 psi spring until you turn them up (a Boost Controller or a turbo's
  `boost_target_psi` input). The 3x3 tops out near 34 psi and the 6x6 near 47 psi, if the exhaust can spin them.
- **Engine ratings.** Every engine has a boost limit by size, layout and length. Past it the engine knocks, loses
  power, takes damage, and at its last damage step it blows until you repair it. Hot charge air lowers the limit.
- **New parts:** Boost Controller (psi target and scramble for every turbo on the vehicle), Boost Gauge, Oil Gauge.
- **Bug fixes:** twin turbos dumped a shared blow-off valve twice per tick; a blown turbo lost its soot after a
  reload; engine and rotary sounds stuttered on long frames and kept sound handles after a world change; Lean
  Assist could lose the bike's wheels after a world load; a locked wear file could reset every turbo to 100%.

- **Upgrading:** the manager shows AnyVehicleOverhaul as an update for AnyEngineParts, AnyMoto and AnyCVT and removes
  their DLLs when it installs. By hand: delete `AnyEngineParts.dll`, `AnyMoto.dll` and `AnyCVT.dll`, add
  `AnyVehicleOverhaul.dll`.
- **Nothing to migrate:** mod ids, part ids, data folders and saved files are unchanged, so vehicles, worlds, Mod
  Settings, keybinds and on/off switches carry over. In Mod Settings the pieces keep their names (AnyEngineParts,
  EngineSound, GpsPart, AnyMoto, AnyCVT).
- If an old DLL is left in the mods folder, AnyAPI runs it instead of the matching bundled piece, so nothing is
  hooked twice.
- Details: [AnyVehicleOverhaul](../mods/vehicle-overhaul.md).

Publishing: upload `release.json` from [anyvehicleoverhaul-2.0.0/](anyvehicleoverhaul-2.0.0/release.json) with the
ZIP. AnyEngineParts, AnyMoto and AnyCVT must not be in the same release (the workflow removes their catalog entries).
