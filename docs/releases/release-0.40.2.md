# AnyAPI 0.40.2 and AnyGraphics 0.33.0 for Anymaker 0.1.27

AnyAPI 0.40.2 is the graphics audit release: faster native reads, fixes across every scene
pass, the game's clock and weather for the sky and lights, and new service versions for mod
authors. AnyGraphics 0.33.0 adds ocean reflections, three new rows and a measuring Auto-pick.

Update AnyAPI and your mods in the manager, then restart the game.

- **AnyAPI 0.40.2**
- **AnyGraphics bundle 1.1.0** with AnyGraphics 0.33.0 and ThirdPerson (needs AnyAPI 0.40.2)
- **AnyHelpers bundle 1.1.0**: Heal fix in the admin tab, bent windows split cleanly (AnyMirror 1.2.0),
  hidden struts and frameless windows, and MCCode 0.3.1 lists modded parts in the Parts tab
- **AnyMap bundle 1.0.2**: markers and the waypoint are saved per world
- **AnyPerformance 0.1.2**: wreck loot spread over several frames, no autosave freeze while it fills,
  faster water checks and fewer updates for far-away loose items
- **AnyCareer 0.0.8**: co-op joiners trade and take jobs through the host, loot Cash no longer
  fills to 9999 after a few world loads, and the direction line at the top of the screen is gone
- **AnyMoto 0.0.1** (new): motorcycle fork, swingarm, seat, handlebars and Lean Assist, in loot
  after the first bunker
- **AnyLights 0.2.0** (new): one paint tool for every colour (palette, paint modes, Ctrl+Z).
  Painted lights don't glow their colour yet

Everything new that changes the look is either a clear bug fix or a row that starts off. Each
default change has a hidden revert in `AnyGraphics/settings.tsv` (see
[AnyGraphics](../mods/graphics.md#0330-anyapi-0402)).

## Frame core

- AnyAPI reads the game's scene data in-process instead of through `ReadProcessMemory`, so
  each frame makes far fewer system calls (most noticeable under Wine and Proton). A bad
  address is caught and the pass skips that frame instead of crashing.
- Scene passes check the view flags themselves, so in bunkers and under water they keep
  running where they apply instead of all switching off.
- One game clock (sun, moon, phase) and one weather reading per frame feed every pass.
- The log keeps one file handle open instead of reopening the file for every line (about
  0.1 ms saved per line).
- For mod authors: scene controls 4, scene lighting 4, scene antialiasing 3, scene effects 3,
  scene timing 2 and GPU info 2 (`sdk/include`). See the [API index](../api/README.md).

## Overlay and plugin faults

- Fixed a startup crash with the Steam overlay (or another overlay that hooks the game's swap
  chain after AnyAPI): the two hooks could call each other until the game ran out of stack.
  AnyAPI now notices the loop, logs `PLUGIN_PLATFORM hook_reentry` once and calls DXGI directly.
- A mod that faults no longer takes the mouse from another mod or silently stops drawing: the
  fault is logged under that mod's id, its capture is released and a short notice appears.
- If the overlay cannot start (11on12 or Direct2D), the scene passes keep running; the overlay
  retries with growing waits instead of every second.
- Resizing the window no longer rebuilds every overlay bitmap and texture.
- Fewer per-frame window-system calls (focus and swap chain checks are cached).
- The Graphics menu bridge backs off when the game build does not match instead of rescanning
  every 5 s forever.
- For mod authors: scene timing 2 (frame time median and slowest 5%, GPU ms per pass, AnyAPI
  CPU time, native reads) and GPU info 2 (memory, scene size, lamp shadow support).

## Anti-aliasing

- TAA and SMAA record before the editor, placement and inspection overlays, so building no
  longer smears or turns AA off (`aa_before_overlays` reverts).
- Ocean pixels reproject on the sea surface, so the water no longer ghosts under TAA
  (`taa_ocean` reverts).
- Sharper TAA in motion (Catmull-Rom history) and a better rule for still and moving pixels.
  Both revert together with `taa_still_v2`; there is no separate switch for the history filter.
- Looking through a scope resets the history instead of smearing.
- A one-step dither removes banding in dark gradients (`output_dither` turns it off, also in
  the finish pass).
- Cheaper TAA input pass, and AA reads the game without system calls.
- For mod authors: scene antialiasing 3 with `history_bias` (TAA extra smooth) and a status
  with the last fallback reason.

## Effects (tone mapping, shading, lens)

- The game's own ambient occlusion is skipped when Detailed shading replaces it, which saves
  its GPU time (`keep_native_ssao` turns it back on).
- Detailed shading no longer draws a crease that changes with the resolution, and bounce light
  no longer leaks through pillars or comes off the seabed (`shading_revision` reverts both).
- Exposure metering only runs while eye adaptation or sun glare needs it. Glare no longer
  counts the ocean or the game's clouds as open sky.
- Soft film no longer turns bright orange toward yellow; contact shadows take the floor light
  from the right side.
- Depth of field no longer blurs held items, focuses on the seabed or smears your own vehicle;
  lens history resets on fast turns and zooms.
- The game's bloom comes back within a few frames if the finish pass stops drawing.
- With Weather response, eye adaptation ignores lightning flashes.
- For mod authors: scene effects 3 (cloud model, weather, shading and tone revisions, bloom
  falloff, dither, flags) and a status with a per-pass error code.

## Scene controls, clock and weather

- Light colour tints the sun and sky lights it was meant for; before, the warm tint also
  landed on the sky straight above.
- New for AnyGraphics: time-of-day light colours, the moon as the game's night light and storm
  fog, all through scene controls 4.
- Game bloom is forced off only while the AnyAPI finish pass is really drawing.
- Cloud lighting refreshes after a time skip.
- For mod authors: scene controls 4 and a status 2 with the clock, the weather and the moon.

## Volumetric lighting and lamps

- AMD graphics: lamp beams no longer go negative. Faster builds sized to the lamps in view are
  ready for testing (`amd_lamp_path` 1); until they are confirmed on AMD hardware the default
  stays on the single shader that fixed the Radeon RX 6800.
- Lamps are tracked between frames, so beams stop popping as lamps compete for the budget and
  fade in and out instead.
- In bunkers the lamps light the air (the sun does not); lamp light stops at the sea surface.
- The volumetric pass blends straight into the scene (no HDR copy) and no longer waits for the
  GPU when its tables are reused.
- With Weather response, beams thicken in rain and fog and lightning lights the fog.
- For mod authors: scene lighting 4 (lamp budget, tracking, sea plane, weather, lightning
  glow, AMD path) and status 2 (lamps drawn, budget, why the pass skipped).

## Sky and water

- The night sky follows the game's clock: stars all night (before, about a tenth of it), moon
  phases, a dawn fade and a sparser, more natural star field (`sky_model` reverts). The Sky
  glow moonlight on the ground keeps its old timing: it has no shadows, so all night it would
  light interiors too.
- Clouds v2: cloud cover no longer changes with zoom or resolution, thick cloud hides the sun,
  the moon lights the clouds and the clouds drift with the game's wind (`cloud_model` reverts).
- The moonlight glow no longer lands on the ocean surface.
- With Weather response, stars, moon and sun dim, clouds thicken and the sea gets rougher in
  rain and storms.
- Water reflections and glint fixes (fog, wave phase, the camera under water).

## AnyGraphics 0.33.0

- **Water reflections and Sun glint** (Sky and weather) on the sea.
- **Moonlight source**: Sky glow (as before) or Game light, where the moon casts the game's
  shadows and lights the fog. Off by default.
- **Light colour follows time**: golden at sunrise and sunset, neutral at noon, blue at night.
  Off by default.
- **Weather response**: rain, snow and storms change fog, clouds, sky, sea and lamp beams. Off
  by default.
- **Auto-pick** measures typical and worst frame times and how much AnyAPI's passes cost,
  stops when the CPU or game is the limit (never below Balanced that way; a game waiting for
  the graphics card still steps down), notices a frame-rate
  cap (a 30 or 40 fps limit stops it instead of dropping presets), keeps a look's style, and
  stops if the settings cannot be saved.
- **Show GPU cost** lists every pass, and a second line shows frame times, AnyAPI's CPU time
  and the lamps drawn. In menus it shows the last gameplay numbers.
- Presets: High is no longer as costly as Ultra (32 samples and 6 lamps), Balanced sharpens TAA,
  Natural keeps the game's ambient occlusion. The looks use 32 volumetric samples, Natural and
  Clear Day 24 (fewer than before, so their sun shafts are a little softer). A preset saved with
  an earlier version moves to these values once; Custom is not touched. A saved preset keeps the
  new Water reflections and Sun glint off: choose the preset again or select Reset preset to
  turn them on. Picking Performance or Low again now keeps the game's weather fog (base fog
  Reduced); a saved preset keeps the fog it has (Fog is a top row).
- If you never selected Apply in AnyGraphics, you ran each row's own default; 0.33.0 runs the
  real Balanced preset instead (TAA, Filmic, volumetric fog and sun shafts, the sky and contact
  shadows), which costs more. On integrated graphics and Steam Deck it starts on Low instead,
  once, and saves that. Auto-pick or a lower preset brings the cost down.
- Fixes: a fresh install and Reset give the real Balanced preset; quick photo-key taps work at
  low frame rates; photo mode returns the camera if AnyGraphics stops updating it; photo
  controls no longer add sun glare or swap the bloom; the Lighting group shows without scene
  lighting; notes for sun shafts with the game's shadows off and lamp beams on AMD graphics.
- Fog and light levels are named after the game (Reduced and Game instead of Medium and High).
  Saved values do not change.

With AnyAPI 0.40.1 AnyGraphics 0.33.0 hides the new rows; the 0.33.0 preset tables, the
one-time preset update and Reset to Balanced still apply.

## How it was checked

- A multi-agent audit of every scene pass against the game code, with a written fix and test
  for each finding.
- CPU contract tests for every changed pass, the AnyGraphics menu (with the AnyAPI 0.40.2,
  0.40.1 and older service sets, a fresh install and settings saved by 0.32.0) and photo mode.
- GPU tests under WARP for the AA, effects, lighting, sky and water shaders.
- The in-game owner round for the looks that cannot be tested off the game.
