# AnyVehicleOverhaul

> AnyVehicleOverhaul hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> It is a [bundle](../api/bundles.md): it needs AnyAPI 0.39.0 or newer (0.42.1 or newer on 0.2.0). Install it from the manager.

AnyVehicleOverhaul is every vehicle part mod in one download (`AnyVehicleOverhaul.dll`). It replaces two earlier
downloads: the **AnyEngineParts** bundle (AnyEngineParts, EngineSound and GpsPart) and the standalone **AnyMoto**.
Nothing about the parts changed in the merge: same parts, same settings, same saves.

| Piece (mod id) | What it adds | Guide |
| --- | --- | --- |
| AnyEngineParts (`anyengineparts`) | Turbochargers (3x3x2 and 6x6x2) with inline blow-off valves, a wastegate with antilag and boost channels; belt-driven superchargers (Small 2x2x2, Large 4x4x4); turbo upkeep (oil ports, cone air filters, heat and wear, intercoolers); a 1-4 rotor rotary engine; the Dyno sensor | [Turbo](turbo.md), [Rotary](rotary.md), [Dyno](dyno.md) |
| EngineSound (`enginesound`) | Generated engine sounds with presets and a Custom tuner in the Properties tool | [EngineSound](engine-sound.md) |
| GpsPart (`gpspart`) | The GPS Sensor part | [GpsPart](gps.md) |
| AnyMoto (`anymoto`) | Motorcycle fork, swingarm, seat, handlebars and Lean Assist for the stock Motorcycle Wheel | [AnyMoto](moto.md) |

Each piece has an **Enabled** row at the top of its group in Mod Settings (AnyHelpers); switching one off applies the
next time you start the game. Each piece also starts on its own: if one cannot find the game functions it needs, the
others still work. Their lines in `anymaker_modding.log` keep their own tags (`turbo`, `rotary`, `dyno`,
`enginesound`, `gpspart`, `anymoto`).

## Upgrading from AnyEngineParts or AnyMoto

- **In the manager (1.5.0 or newer):** AnyVehicleOverhaul shows as an update while `AnyEngineParts.dll`,
  `AnyMoto.dll`, `EngineSound.dll` or `GpsPart.dll` is installed. Installing it removes those DLLs (with a backup).
- **By hand:** delete `AnyEngineParts.dll` and `AnyMoto.dll` from `AnyAPI and Modding/mods` and copy in
  `AnyVehicleOverhaul.dll`.
- **If an old DLL is left behind:** nothing loads twice. AnyAPI runs the old DLL's copy of each piece and skips the
  bundled one (the log says `BUNDLE_PART_SKIPPED bundle=AnyVehicleOverhaul reason=standalone_dll_installed`), so you
  keep the old version until you delete the old DLL.

### What carries over

The pieces keep every identifier the game or the player's files store. Only the download, the DLL file name and the
catalog entry changed.

| Kept | Value | Why it matters |
| --- | --- | --- |
| Mod ids | `anyengineparts`, `enginesound`, `gpspart`, `anymoto` | Mod Settings values, keybinds (Dyno F6 is `anyengineparts`/`dyno_toggle`) and the on/off switch in `AnyAPI and Modding/bundle-parts.tsv` are keyed by these |
| Game part ids | `turbocharger_*`, `supercharger_small`, `supercharger_large`, `turbo_blow_off_valve*`, `turbo_wastegate*`, `turbo_air_filter*`, `turbo_intercooler*`, `rotary_engine`, `dyno_sensor`, `gps_sensor`, `moto_fork`, `moto_swingarm`, `moto_seat`, `moto_handlebars`, `moto_lean_assist` | Saved vehicles, creations and worlds name parts by these ids |
| Data folders | `AnyAPI and Modding/Turbo`, `Rotary`, `Dyno`, `GpsPart`, `AnyMoto` | The part definition files each piece writes at start |
| Saved state | `anyengineparts_wear.json` (turbo wear), `enginesound_sounds.json` (engine sound choices), in the save's `working` folder | Travels with each world save |
| Generated meshes | In `rom/meshes/components/`: the turbo, supercharger and valve meshes, `rotary_engine*`, `anyengineparts_dyno_sensor_a`, `gpspart_gps_sensor_a`, `anymoto_*` | Rewritten at every start under the same names |

In co-op, everyone should run the same download. A player still on AnyEngineParts + AnyMoto has the same pieces and
part ids, so worlds load either way.

## For developers

The bundle is [`native/bundles/anyvehicleoverhaul_bundle.cpp`](../../native/bundles/anyvehicleoverhaul_bundle.cpp)
(CMake target `AnyVehicleOverhaulBundle`, output `build/bundles/<config>/AnyVehicleOverhaul.dll`). Each piece is
still its own standalone source, compiled unchanged through a `part_<id>.cpp` wrapper:

| Part wrapper | Source |
| --- | --- |
| [`part_anyengineparts.cpp`](../../native/bundles/part_anyengineparts.cpp) | [`engine_parts_mod.cpp`](../../native/engine_parts_mod.cpp), which starts [`turbo_mod.cpp`](../../native/turbo_mod.cpp), [`rotary_mod.cpp`](../../native/rotary_mod.cpp) and [`dyno_mod.cpp`](../../native/dyno_mod.cpp) |
| [`part_enginesound.cpp`](../../native/bundles/part_enginesound.cpp) | [`enginesound_mod.cpp`](../../native/enginesound_mod.cpp) |
| [`part_gpspart.cpp`](../../native/bundles/part_gpspart.cpp) | [`gpspart_mod.cpp`](../../native/gpspart_mod.cpp) |
| [`part_anymoto.cpp`](../../native/bundles/part_anymoto.cpp) | [`anymoto_mod.cpp`](../../native/anymoto_mod.cpp) |

The standalone CMake targets (`AnyEngineParts`, `EngineSound`, `GpsPart`, `AnyMoto`) still build, for testing one
piece alone; they are never released. `native/tools/port_release_plan.py` skips any standalone target whose main
source a bundle compiles.

**Adding a new vehicle piece** (the bundle's job from here on):

1. Write it as a standalone mod with its own mod id, in its own namespace (all parts share one DLL: no `DllMain`, no
   non-namespaced globals), with its own standalone CMake target and logic test.
2. Add `native/bundles/part_<id>.cpp` (copy `part_anymoto.cpp`), add it and any extra `.cpp` files to the
   `AnyVehicleOverhaulBundle` sources in `native/CMakeLists.txt`, then `ANYAPI_BUNDLE_DECLARE(<id>)` and an
   `ANYAPI_BUNDLE_PART` row in the bundle file. Give `legacy_file` the old DLL name if the piece ever shipped alone.
3. Bump the `AnyVehicleOverhaulBundle` version block in `native/CMakeLists.txt`, and add the piece to the tables
   above and to the catalog description in the release's `release.json`.
4. Never rename a shipped piece's mod id, game part ids or data folder: that loses players' settings and breaks their
   saves. Rename display text only.

A bundle holds at most 16 parts, and every part takes one of AnyAPI's 32 mod slots.
