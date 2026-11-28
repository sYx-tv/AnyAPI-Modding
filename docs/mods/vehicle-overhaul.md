# AnyVehicleOverhaul

> AnyVehicleOverhaul hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> It is a [bundle](../api/bundles.md): it needs AnyAPI 0.39.0 or newer (0.42.1 or newer on 0.2.0). Install it from the manager.

AnyVehicleOverhaul is every vehicle part mod in one download (`AnyVehicleOverhaul.dll`). It replaces two earlier
downloads: the **AnyEngineParts** bundle (AnyEngineParts, EngineSound and GpsPart) and the standalone **AnyMoto** and
**AnyCVT**.
Nothing about the parts changed in the merge: same parts, same settings, same saves.

| Piece (mod id) | What it adds | Guide |
| --- | --- | --- |
| AnyEngineParts (`anyengineparts`) | Turbochargers (3x3x2 and 6x6x2) with inline blow-off valves, a wastegate with antilag and boost channels; belt-driven superchargers (Small 2x2x2, Large 4x4x4, and a stretchable twin screw); turbo upkeep (oil ports, cone air filters, heat and wear, intercoolers); a 1-4 rotor rotary engine; built (forged) versions of the nine stock engines; the Dyno sensor, Boost Controller and boost and oil gauges | [Turbo](turbo.md), [Rotary](rotary.md), [Dyno](dyno.md) |
| EngineSound (`enginesound`) | Generated engine sounds with presets and a Custom tuner in the Properties tool | [EngineSound](engine-sound.md) |
| GpsPart (`gpspart`) | The GPS Sensor part | [GpsPart](gps.md) |
| AnyMoto (`anymoto`) | Motorcycle fork, swingarm, seat, handlebars and Lean Assist for the stock Motorcycle Wheel | [AnyMoto](moto.md) |
| AnyCVT (`anycvt`) | Belt CVTs for the small, medium and large engine (R/N/D on a mechanical shifter; none of them needs air) | [AnyCVT](cvt.md) |
| AnyWheels (`avowheels`, 2.1.0) | Race wheels, drag slicks and drag front runners that fit the stock wheel and tyre mount parts | below |

Each piece has an **Enabled** row at the top of its group in Mod Settings (AnyHelpers); switching one off applies the
next time you start the game. Each piece also starts on its own: if one cannot find the game functions it needs, the
others still work. Their lines in `anymaker_modding.log` keep their own tags (`turbo`, `rotary`, `dyno`,
`enginesound`, `gpspart`, `anymoto`, `anycvt`, `avowheels`).

## In the 2.1 dev build (not released)

Everything below is in the dev build only, for testing. Its lines in the log start with `AVO`.

- **ECU** (`engine_ecu`): one per vehicle tunes every engine on it. Open its panel with **F9** near it (Up/Down picks
  a row, Left/Right changes it, Shift moves 4 steps). The rows are rev limit, launch RPM, launch (two-step), boost
  target, ignition timing, fuel trim, pops and antilag. Every row is also a data input, and a wired input that is not 0
  wins over the panel. Its outputs are rpm, boost psi, knock, power scale and knock limit (see `native/ecu_logic.h`).
- **Built Rotary** (`built_rotary_engine`): the rotary's knock limit now drops a little with each rotor.
- **Rotary sounds**: the rotary's Sound effect row opens the Rotary sound panel (EngineSound's): the three ports, five sounds made for its rotor count (1-4 rotors) and Custom (port, exhaust note, bark, rasp, muffler, idle chop, pops, idle blat, shaft whirr, volume). Saves keep a port so a world still loads without the mod. The parts on its pipes (mufflers, resonators, headers, tips) change it too, and it plays from the exhaust tips.
- **Exhaust tips**: Street, Race and Classic looks, each as a single, dual, quad or side pipe
  (`exhaust_tip_<look>_<layout>`). A tip is a vent. The engine's exhaust sound plays from it.
- **Inline exhaust parts**: chambered, straight-through, glasspack and turbo mufflers, inline and Helmholtz
  resonators and a catalytic converter. Exhaust goes in the back and out the front. Each part has its own backpressure.
- **Race headers** (`header_1_1` to `header_6_1`): equal-length 1-1 to 6-1 headers that are exhaust manifolds. The
  flange snaps straight onto the engine's exhaust face like a stock manifold (the first runner is the one that snaps
  on; the inputs sit one per cylinder, every 2 cells). The primaries run down and back to the collector, whose outlet
  is the manifold's gas node: put a large pipe or a gas port there and route it anywhere. Fixed sizes, no stretch.
  On the engine they add about 4 % power and a smooth note.
  Each size also comes **mirrored** (`header_1_1_m` to `header_6_1_m`) for the other bank of a V engine, so both
  collectors point the same way.
- **Intake parts** for naturally aspirated engines: a velocity stack, ITBs and a muscle carb. They go on the air
  manifold.
- **Exhaust sound**: EngineSound follows the pipes from the engine to its outlets. It plays the exhaust from each tip
  or stock vent, and the parts on the way set its tone. The engine itself keeps a quieter voice. See
  `native/exhaust_logic.h`.

- **Race and drag wheels** (`avo_wheel_<kind>_<style>`, part `avowheels`): new tyre items that fit the stock wheel
  and tyre mount parts like the 20 inch car wheel. **Race Wheel** grips 1.5x a stock tyre (radius 0.33 m), **Drag
  Slick** 2.5x (radius 0.36 m, wide), **Drag Front Runner** stock grip (skinny). Each comes in a 5-spoke, 6-spoke and
  mesh rim. The game has no grip per tyre, so on the host a wheel wearing one ticks with a copy of its tuning whose
  sliding grip and friction-circle limit are scaled (`native/avo_wheels_logic.h`). The grip circle is shared, so a
  drag slick corners harder too. Loot: race and front runner tier 3 (0.1), drag slick tier 4 (0.075). The items are
  made at start-up from your own copy of the game's 20 inch wheel entry; no game data ships with the mod.

The looks of the ECU, the intake parts, the turbo muffler, the resonators, the cat and the headers are placeholders.

## Trailing Link Gear (test build, not in the bundle yet)

Steerable landing gear with its own wheel built in, in three sizes (Nate 2026-11-25, look 3 of three). It ships as a
separate test DLL, `AvoGear.dll` (`native/avo_gear_mod.cpp`), and joins this bundle once it passes in game.

| Part | Wheel | Travel | Steering |
|---|---|---|---|
| Trailing Link Gear (Small) `avo_gear_trailing_s` | 34 cm | 10 cm | 40 degrees each way |
| Trailing Link Gear (Medium) `avo_gear_trailing_m` | 50 cm | 15 cm | 40 degrees each way |
| Trailing Link Gear (Large) `avo_gear_trailing_l` | 72 cm | 22 cm | 35 degrees each way |

- **Fitting (top mount, 0.1.1):** bolt the top of the mount plate to the underside of a hull or wing, the way a seat
  stands on a floor by its bottom face. The leg drops from the plate and the wheel trails out. 0.1.0 mounted on a
  vertical face instead. That the game keeps the part upright on a top face is likely (the seat does it), not tested.
- **Links:** steering in on each side of the mount plate (one turns it left, the other right), brake on the back, drive
  in through the torque face on the front. Spring and damping are the stock wheel sliders in the Properties tool.
- **Built-in wheel:** no tyre item fits. The game's own integral-tread flag gives it full tyre grip.
- **How:** each size is a wheel-class part with its own copy of the stock wheel's code-side tuning, set on the server
  and the client the way AnyMoto does it for the fork and swingarm. The spring is the stock wheel's, straight up and
  down along the part. The leg, arm, twin coilovers and wheel are drawn by the mod: the assembly turns about the leg
  when it steers, the arm follows the axle and the shocks compress.
- **Steering pivot (likely):** the tuning's steering position is set on the leg, so the game should swing the wheel
  round the leg as the drawing does. The "drawing" log lines give the game's axle next to the drawn one, also at
  full lock, to confirm it. If the game turns the tyre about its own centre instead, they part by up to 23 cm at
  full lock (medium). There's no test bench save yet.
- **Status:** the route is proven by AnyMoto. The built-in wheel, the drawing and the sizes are untested in game.

## Upgrading from AnyEngineParts, AnyMoto or AnyCVT

- **In the manager (1.5.0 or newer):** AnyVehicleOverhaul shows as an update while `AnyEngineParts.dll`,
  `AnyMoto.dll`, `AnyCVT.dll`, `EngineSound.dll` or `GpsPart.dll` is installed. Installing it removes those DLLs (with a backup).
- **By hand:** delete `AnyEngineParts.dll`, `AnyMoto.dll` and `AnyCVT.dll` from `AnyAPI and Modding/mods` and copy in
  `AnyVehicleOverhaul.dll`.
- **If an old DLL is left behind:** nothing loads twice. AnyAPI runs the old DLL's copy of each piece and skips the
  bundled one (the log says `BUNDLE_PART_SKIPPED bundle=AnyVehicleOverhaul reason=standalone_dll_installed`), so you
  keep the old version until you delete the old DLL.

### What carries over

The pieces keep every identifier the game or the player's files store. Only the download, the DLL file name and the
catalog entry changed.

| Kept | Value | Why it matters |
| --- | --- | --- |
| Mod ids | `anyengineparts`, `enginesound`, `gpspart`, `anymoto`, `anycvt` | Mod Settings values, keybinds (Dyno F6 is `anyengineparts`/`dyno_toggle`) and the on/off switch in `AnyAPI and Modding/bundle-parts.tsv` are keyed by these |
| Game part ids | `turbocharger_*` (`_precision`, `_efr` looks, each with a `_twin` twin-scroll version), `supercharger_small`, `supercharger_large`, `supercharger_twin_screw`, `turbo_blow_off_valve*`, `turbo_wastegate*`, `turbo_air_filter*` (cone, `_sock`, `_drum`), `turbo_intercooler*` (bar and plate, `_round`, `_race`), `rotary_engine`, `dyno_sensor`, `gps_sensor`, `moto_fork`, `moto_swingarm`, `moto_seat`, `moto_handlebars`, `moto_lean_assist`, `anycvt_small`, `anycvt_medium`, `anycvt_large`, `boost_controller`, `boost_gauge`, `oil_gauge`, `built_engine`, `built_engine_block_b`, `built_engine_block_c`, `built_v_engine`, `built_v_engine_b`, `built_v_engine_c`, `built_opposed_engine`, `built_opposed_engine_b`, `built_opposed_engine_c` | Saved vehicles, creations and worlds name parts by these ids |
| Data folders | `AnyAPI and Modding/Turbo`, `Rotary`, `Dyno`, `GpsPart`, `AnyMoto`, `AnyCVT` | The part definition files each piece writes at start (`Rotary/built_engines.json` holds the built engines) |
| Saved state | `anyengineparts_wear.json` (turbo wear), `enginesound_sounds.json` (engine sound choices), in the save's `working` folder | Travels with each world save |
| Generated meshes | In `rom/meshes/components/`: the turbo, supercharger and valve meshes, `rotary_engine*`, `anyengineparts_dyno_sensor_a`, `gpspart_gps_sensor_a`, `anymoto_*`, `anycvt_*` | Rewritten at every start under the same names |

In co-op, everyone should run the same download. A player still on AnyEngineParts + AnyMoto + AnyCVT has the same pieces and
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
| [`part_anycvt.cpp`](../../native/bundles/part_anycvt.cpp) | [`anycvt_mod.cpp`](../../native/anycvt_mod.cpp) |

The standalone CMake targets (`AnyEngineParts`, `EngineSound`, `GpsPart`, `AnyMoto`, `AnyCVT`) still build, for testing one
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
