# AnyGraphics

AnyGraphics is a native graphics mod for Anymaker 0.1.27. The current release is
**0.33.0**. It is built for **AnyAPI 0.40.2**, which adds water reflections, the
0.33.0 rows below and the per-pass GPU cost readout. With an older AnyAPI the
rows it cannot drive are hidden: AnyAPI 0.40.1 keeps the 0.32.0 feature set,
0.36.0 the 0.30 set (TAA, sharpening, beam shaping, tone mapping and colour
looks, the GPU cost readout), and older APIs the 0.29 set, where TAA choices
fall back to SMAA. Its controls live inside
Settings → Graphics, in the scrolling **AnyGraphics · Modded** section. It does
not require AnyHelpers.

The menu and native controls arrived in 0.28.0, volumetric lighting in 0.29.0,
fog-independent light beams in 0.29.1 (with API 0.32.0), and bounded retries for
rejected settings in 0.29.3. 0.30.0 fixes foggy sun shafts and adds TAA,
sharpening, tone mapping and colour looks, a Cinematic preset and a GPU cost
readout. 0.31.0 adds a free camera and photo controls (depth of field,
exposure, look, vignette, bloom) to photo mode. 0.31.1 fixes the free camera spinning
when photo mode starts in ThirdPerson's third-person view. 0.31.2 hides the lamp beam
settings on AMD graphics cards, where AnyAPI 0.39.1 switches lamp beams off; with AnyAPI 0.39.2
they are back. 0.31.3 fixes your own head showing in first person after leaving photo mode (when F5 switched
ThirdPerson to first person while the camera was flying), and the free camera staying at the last world's
spot after loading another world with photo mode still on. 0.32.0 (with AnyAPI 0.40.1) splits the
section into sub-menus and fixes the AA, sun shaft, contact shadow and cloud bugs below. 0.33.0
(with AnyAPI 0.40.2) adds reflections and a sun glint on the sea (see Water below), the
Moonlight source, Light colour follows time and Weather response rows, a measuring
Auto-pick and a per-pass cost readout (see 0.33.0 below). See the
[release records](../releases/README.md).

## Sub-menus (0.32.0)

The top of the section holds the main choices: Enabled, the preset, Reset preset,
Auto-pick and a few headline rows. Everything else sits in groups you open with
**Show** and close with **Hide**: Lighting and sun shafts, Shadows and shading,
Sky and weather, Anti-aliasing and sharpness, Camera and photo, Colour and
finish, and Performance. Groups start closed with a preset and open with Custom.
Changing anything inside a group switches the preset to Custom, starting from
the preset you had. Without AnyAPI menu v4 the Show/Hide buttons are toggles.

Fixes in AnyAPI 0.40.1:

- **Sun shafts**: a partly shadowed ray is no longer up to about 4.7x brighter
  than a lit one, so beams stop pumping as the camera moves; air past the last
  shadow cascade no longer glows as a lit wall; distant ground no longer looks
  blocky; sun shadows in the beams are filtered (2x2), so foliage no longer
  speckles.
- **Contact shadows**: no more self-shadowing at grazing angles, no double
  darkening on top of the game's own shadows, nothing on the ocean, softer
  edges and no offset shadows from thin objects.
- **Clouds**: fixed in the world and crisper (see Sky below).

New looks in 0.32.0 (AnyAPI 0.40.1):

- **Soft & warm** preset: a Complementary-style look built from the High tier.
  It uses the **Soft film** tone curve (a long straight middle and a slow
  roll-off, so bright skies stay soft), the **Soft & warm** colour look (golden
  highlights, cool blue shade, rich but not neon colour) and Light colour
  Normal. Detailed shading (GTAO) stays off.
- **Light colour** (Lighting group: Game, Subtle, Normal, Strong) tints the
  game's own lights: the sun side goes golden and the shade goes cool blue.
  Each light keeps its brightness, and shadows pick the tint up naturally
  because it is in the lighting, not a filter over the screen. Moody uses Subtle.
- **Wind in trees** (Sky group: Calm, Game, Breezy, Stormy) sets how hard trees
  and grass sway. Only the look changes; vehicles, physics and other players
  are not affected.

With an older AnyAPI, Soft film falls back to Clean, Soft & warm to Vivid, and
the Light colour and Wind rows are hidden.

## Water (0.33.0)

Two rows in the Sky and weather group, with AnyAPI 0.40.2 (hidden with an older
AnyAPI):

- **Water reflections** (Off, Low, Medium or High) mirrors boats, coasts, hills
  and anything else on screen above the sea in the water, on top of the game's
  own sky reflection. Like real water it reflects most at grazing angles and
  little when you look straight down. Only what is on screen can reflect: an
  object above the top of the screen or behind the camera leaves the game's sky
  reflection as it was, and reflections fade out toward the screen edges
  instead of cutting off. Low, Medium and High trace each ray more carefully
  (16, 24 or 32 steps at half resolution), so higher levels miss fewer thin
  objects and cost more GPU time.
- **Sun glint** (Off or On) adds a bright, rippling path of sunlight on the sea
  while the sun is up, in the game's sun colour, so it turns orange at sunrise
  and sunset and is gone at night.

Small ripples fixed in the world shape both. They drift with the game's wind and
grow as it picks up; far away they blend into a calm mirror with a wider glint
instead of sparkling. Distant water fades into the game's fog as before. Only
ocean pixels change, and nothing is drawn while the camera is underwater.

High, Natural, Moody, Clear Day and Soft & warm use Medium reflections; Ultra,
Cinematic and Vibrant use High. All of them add the sun glint. Off,
Performance, Low and Balanced leave the water as the game draws it. **Show GPU
cost** includes the water pass.

## 0.33.0 (AnyAPI 0.40.2)

Three new rows. All start off, and no preset changes them, so turning one on
keeps your preset:

- **Moonlight source** (Sky and weather, shown with Moonlight on): **Sky glow**
  is the soft blue glow on screen from earlier releases. **Game light** puts
  the moonlight into the game's own night light instead, so the moon casts the
  game's shadows, lights the fog and glints on the sea, and the moon disc sits
  where that light comes from. The Moonlight level sets its strength.
- **Light colour follows time** (Lighting, shown with a Light colour): golden
  light at sunrise and sunset, neutral at noon, cool blue at dusk and blue
  moonlight at night, following the game's clock. The Light colour level sets
  how strong the colours are. Off keeps one colour all day.
- **Weather response** (Sky and weather): rain, snow and storms change
  AnyGraphics too. Fog and cloud thicken, stars, moon and sun dim, the sea gets
  rougher, lamp beams thicken in the wet air and lightning lights up the fog
  without the exposure pumping. With Game light, storms dim the moonlight
  only while Weather response is on, like the moon disc. It costs almost no GPU time.

Light colour now tints the sun and the sky lights it was meant for (before,
the warm sun tint also landed on the sky straight above).

**Auto-pick** measures typical and worst frame times, not an average, and how
much of each frame AnyAPI's passes take. When they take little of it, the game
or the CPU sets the frame rate, so it stops there instead of dropping presets
that would not help, and it never ends below Balanced that way. With AnyAPI
0.40.2 it also checks that the game is not waiting for the graphics card (time
spent in Present), so a slow integrated GPU still steps down. It notices a
frame-rate cap (vsync or a limiter) and only climbs while the passes leave
room. A limit below 55 fps (a Steam Deck 30 or 40 fps limit, Battery Boost or
half-rate vsync) holds every frame at the same time, so it counts like the game
setting the frame rate: Auto-pick stops instead of dropping presets that could
not raise it. A slow result is measured twice before it steps down. It runs only while
AnyGraphics is on and connected to the game's renderer, and stops if the
settings cannot be saved or you choose a preset yourself. Started from
Natural, Vibrant, Moody, Clear Day or Soft & warm it keeps that look and only
changes what it costs (the result is shown as Custom). Started from Custom it
asks first, because it replaces your settings.

**Show GPU cost** lists the GPU time of each AnyAPI pass (shading, contact
shadows, bounce light, water, lighting, sky, lens, effects and scene AA) and
their total. A second line shows the frame time (typical and slowest 5%),
AnyAPI's own main-thread time, the game reads it made, the overlay's CPU time
and how many lamps were drawn. A pass AnyGraphics turned off reads "off".
While **Gameplay only** bypasses AnyGraphics in a menu, the lines and the group
status lines show the last numbers from gameplay instead of going blank.

Preset changes, applied once to a saved preset (Custom and the top rows such
as Antialiasing and Fog are left alone):

- High no longer costs as much as Ultra: volumetric quality and the lamp
  budget have separate steps (High 32 samples and 6 lamps, Ultra 32 and 8,
  Cinematic 48 and 8). The looks use 32 samples, Natural and Clear Day 24.
- Balanced sharpens TAA (Low).
- Natural keeps the game's own ambient occlusion (no detailed shading), like
  Soft & warm.
- A preset saved before 0.33.0 keeps **Water reflections** and **Sun glint**
  off, so the update does not switch the new pass on by itself. Choose the
  preset again or select **Reset preset** to get its water rows.

Performance and Low now set the base fog to Reduced when picked, like every
other tier: removing it saved no GPU time and also removed the weather fog.
Fog is a top row, so a saved Performance or Low keeps the fog it has until the
preset is picked again.

Fixes:

- A fresh install and **Reset** now give the real Balanced preset (TAA, tone
  mapping, sky and sun shafts), not each row's own default. This includes an
  install that never selected **Apply** in AnyGraphics, which used to run each
  row's own default. On integrated graphics (Intel, an AMD processor's
  graphics or a Steam Deck) such an install starts on **Low** instead, once, as
  soon as AnyAPI knows the graphics card, and saves it. Elsewhere, Auto-pick
  or a lower preset brings the cost down.
- The photo key works on quick taps at low frame rates, and photo mode no
  longer freezes the camera if AnyGraphics stops updating it.
- Photo controls that switch the colour pass on no longer swap the game's bloom
  for a stronger one or add sun glare. Key 5 now also offers Soft & warm.
- With the game's shadows off, the Lighting group says that sun shafts have no
  shadows to show. On AMD graphics it says that lamp beams have no shadows yet.
- The Lighting group stays available when AnyAPI's scene lighting is missing.
- The fog and light levels are named after the game: base fog **Off, Light,
  Reduced, Game, Thick**; Sunlight, Sky light and Ambient light **Off, Dim,
  Game, Bright, Brighter**. Saved values do not change.

Testing rows: `AnyGraphics/settings.tsv` also holds a few rows the menu never
shows, so a tester can compare a change with the 0.32.0 behaviour. Reset puts
them back.

| Row (`anygraphics.hidden`) | Default | Other values |
| --- | --- | --- |
| `cloud_model` | 1, clouds v2 | 0: the AnyAPI 0.40.1 clouds |
| `shading_revision` | 1, the 0.40.2 detailed shading and bounce light | 0: the 0.40.1 versions |
| `tone_revision` | 0, the 0.40.1 Filmic | 1: Filmic with more midtone contrast |
| `bloom_falloff` | 1, every bloom level weighted the same | 0.5 to 0.95: a weaker widest glow |
| `taa_still_v2` | 1, the 0.40.2 TAA history (Catmull-Rom resampling and the rule for still and moving pixels) | 0: the whole 0.40.1 history (both) |
| `taa_thin_rescue` | 0 | 1: keep thin wires and grass in the TAA history (experiment) |
| `keep_native_ssao` | 0, the game's ambient occlusion is skipped when Detailed shading replaces it | 1: run it anyway |
| `amd_lamp_path` | 0, automatic (constant-buffer lamps with shaders sized to the lamps in view) | 1: the same; 2: the uniform-branch experiment; 3: the 0.40.1 single 8-lamp shader (AMD only) |
| `preset_revision` | 1 | internal: the preset tables the settings were saved with |
| `sky_model` | 1, the night sky on the game clock | 0: the AnyAPI 0.40.1 night sky |
| `aa_before_overlays` | 1, scene AA before the editor, placement and inspection overlays | 0: the 0.40.1 placement after them (TAA pauses while an overlay shows) |
| `taa_ocean` | 1, TAA moves ocean pixels on the sea surface | 0: the 0.40.1 depth buffer rule |
| `output_dither` | 1, about one step of dither in the finish and scene AA output | 0: no dither |

## Tone mapping and colour (0.30.0)

The **Finish** rows grade the HDR scene before the game's tone mapping, using
[scene effects](../api/scene-effects.md):

- **Tone mapping**: Off (game look), Game curve, Filmic (AgX-style, keeps
  colour in bright light), Clean (bright and true to the game's colours) or
  Soft film (since 0.32.0).
  Anything but Off also replaces the game's bloom, so the Bloom row there
  takes over and the native Bloom row is hidden.
- **Colour look**: None, Warm, Cool, Teal & orange, Moody, Vivid (the
  shader-pack look) or Soft & warm, with **Look strength** Subtle, Normal or Strong.
- **Eye adaptation**: brightens caves and tunnels and dims bright views for a
  moment, then returns to the game's own day and night brightness.
- **Brightness (stops)**, **Bloom**, **Sun glare** and **Vignette**.

Presets use Filmic from Low up, a subtle Vivid on High and Vivid on Ultra; Cinematic uses a
strong teal & orange film grade with strong bloom and a medium vignette.

**Detailed shading** (Off, Low, Medium or High, next to Ambient occlusion)
replaces the game's ambient occlusion with softer, deeper contact shading in
corners, under parts and between blocks. It only darkens ambient light, never
sunlight, and is hidden while Ambient occlusion is forced off. Presets use it
from High up.

**Contact shadows** (Off, Low, Medium or High) adds small, sharp sun shadows
under parts, between blocks and where things touch the ground. Presets use it
from Balanced up.

**Bounce light** (Off, Low, Medium or High) lets lit surfaces tint nearby ones
with their colour. Only what is on screen can bounce. Presets use it from High
up.

**Depth of field** and **Motion blur** (Off, Low, Medium or High) are camera
effects. Depth of field focuses on whatever is in the middle of the screen and
blurs what is nearer or farther. Motion blur blurs the world as the camera
turns and moves; hands, held items and nearby cockpit parts stay sharp. Only
the Cinematic preset turns them on (depth of field Medium, motion blur Low). With depth of field on, **Blur
strength** (0.25 to 3, default 1) sets how strongly it blurs.

**Object draw distance** (Game setting, Far 1.5x, Very far 2x or Extreme 3x)
keeps characters, props, dropped items and vehicle parts drawn farther away.
Terrain and trees follow the game's own render distance. Farther costs CPU and
GPU time in busy areas, and a host may see a lower server tick. High uses Far,
Ultra Very far and Cinematic Extreme.

**Sun** (Off, Soft, Normal or Bright) draws a round sun with a soft halo, and
**Cloud layer** (Off, Few, Some or Many) adds a cloud deck about 1.5 km up,
above the game's own clouds. Since 0.32.0 it is fixed in the world: it drifts
slowly with the wind and passes overhead as you drive, instead of following the
camera. Both follow the time of day: the sun and clouds turn orange at
sunrise and sunset, and the clouds are moonlit at night. Presets use the sun
from Low up and clouds from Balanced up.

**Night sky** (Off, Subtle, Normal or Bright) adds stars, a milky way and a
moon at night, and **Moonlight** (Off, Low, Medium or High) adds soft blue
light from the moon. Both follow the game's own day and night: the stars turn
with the sun and the moon rises as the sun sets. With the Sky glow moonlight
source the moon casts no shadows; Game light (0.33.0) makes it the game's
night light. Presets use them from Low up.

## Controls

The original game controls remain above the mod section. Choosing Game setting
uses those values; choosing an override tells the renderer to use AnyGraphics'
value during gameplay. Override choices are explicitly labelled. Disable
AnyGraphics to return to the game's own rendering settings.

Controls include native FXAA, SSAO, shadows, fog blur, bloom intensity and
threshold, sunlight, sky light, ambient light, base fog and scene-lighting stops.
They alter native renderer switches or frame lighting inputs before the game
builds scene constants and records rendering commands.

Base fog uses **Off / Light / Reduced / Game / Thick** and Sunlight, Sky light
and Ambient light use **Off / Dim / Game / Bright / Brighter** instead of
numeric sliders; Game keeps the game's own value. Bloom offers Game setting,
Off and Low to Ultra. These levels change native intensity inputs; they are not separate
shadow-map or SSAO sampling-quality modes. Shadows, ambient occlusion and fog
blur retain the game's actual on/off capability.

Presets are **Off / Performance / Low / Balanced / High / Ultra / Cinematic /
Natural / Vibrant / Moody / Clear Day / Custom / Soft & warm**. Choosing a preset stages a group of
settings. Off bypasses overrides and restores the game's own values. Balanced is
the initial preset, and a fresh install and Reset give its values. On
integrated graphics and Steam Deck a fresh install starts on Low instead.

Every preset except Custom shows only a few common settings: Antialiasing,
Fog, Brightness, Colour look, Depth of field (with Blur strength), Motion blur,
Object draw distance and Show GPU cost. Changing one of them keeps the preset.
**Custom** shows every setting and starts from the values of the preset you
were on, so a look can be fine-tuned from the closest preset.

**Reset preset** puts every setting back to the selected preset's values.
**Auto-pick preset** measures the frame rate in gameplay for a few seconds per
preset and steps between Performance and Ultra until the game holds about
60 fps, then says which preset it chose. Close the menu and play while it runs.
On a laptop or integrated graphics, start here: Balanced can be too heavy
there. See 0.33.0 above for how it decides.

**Photo mode** hides the game's HUD, crosshair and menus and frees the camera.
Press **F1** in gameplay to turn it on or off (rebindable in AnyHelpers
controls), or use the Photo mode switch under Preset. Opening any menu shows
the UI again. A help panel in the bottom-left corner lists the keys; it hides
after a few seconds so it stays out of screenshots, and comes back whenever a
photo key is pressed.

| Key | In photo mode |
| --- | --- |
| W A S D | Fly the camera where you look; the mouse still turns the view |
| Space / C | Up / down |
| Shift | Fly four times faster |
| Mouse wheel | Flying speed (0.25 to 40 m/s, starts at 4) |
| V | Free camera on or off (off: the camera follows you, movement keys work) |
| 1 / 2 | Less / more depth of field; it focuses on the middle of the screen |
| 3 / 4 | Exposure down / up in quarter stops |
| 5 | Colour look: preset, None, Warm, Cool, Teal & orange, Moody, Vivid, Soft & warm (AnyAPI 0.40.1 or newer) |
| 6 | Vignette: preset, Off, Light, Medium, Strong |
| 7 | Bloom: preset, Off, Subtle, Normal, Strong |
| R | Reset the photo controls and speed |
| H | Keep the help panel open, or hide it |

The camera starts exactly where your view was and stays within 200 m of that
point, because the world is only loaded around you. While it flies, your
character stays where it is and mouse clicks do nothing, so you cannot shoot
or build from the camera. Photo controls last until the game closes and are
never saved into your preset. The free camera hooks the game directly and
works on Anymaker 0.1.27 only; on another game build photo mode still hides
the HUD and the photo controls still work.

**Local light beams** (headlights, torches, lamps) light the air in clear,
thin beams that reach a little past each light's own range. Looking straight
into a lamp is capped so it never whites out the screen, the glow rolls off
softly, and beams fade in daylight the way real headlights do.

No preset goes past Low volumetric fog or Reduced base fog (the game's fog at
65%), because thick fog washes out the whole scene. More fog is available in
Custom.

| Preset | Native settings | Antialiasing |
| --- | --- | --- |
| Off | Game settings; no native overrides | Game setting |
| Performance | SSAO, shadows, fog blur, bloom, clouds and grass off; Reduced base fog | FXAA |
| Low | Shadows on, low bloom, Reduced base fog; SSAO, fog blur and grass off | SMAA Medium |
| Balanced | Native effects on; medium bloom, Reduced base fog, contact shadows Low | TAA High, sharpening Low |
| High | Native effects on; high bloom, medium base fog; subtle Vivid look, Medium detailed shading, bounce light and contact shadows, local light beams, Medium water reflections and sun glint | TAA High, sharpening Low |
| Ultra | As High with a Vivid look, strong bloom and glare, High detailed shading, bounce light and contact shadows, bright sun, many clouds, very far objects, clear sun beams and High water reflections | TAA extra smooth Ultra, sharpening Low |
| Cinematic | As Ultra, with a strong teal & orange film grade, a medium vignette, stronger beams, extreme object distance, stronger sun beams, Medium depth of field and Low motion blur | TAA extra smooth Ultra, sharpening Medium |
| Natural | High, with Clean tone mapping, no colour look or vignette, softer bloom and glare, no volumetric fog, the game's ambient occlusion | TAA High, sharpening Low |
| Vibrant | Ultra, with a strong Vivid look, many clouds, a bright sun and stars, strong clear sun beams and low base fog | TAA extra smooth Ultra, sharpening Low |
| Moody | High, with a strong warm golden-hour grade, a medium vignette, strong sun beams, light haze and many clouds | TAA High, sharpening Low |
| Clear Day | High, with Clean tone mapping, no colour look or vignette, low base fog, no volumetric fog, short clear beams | TAA High, sharpening Medium |
| Soft & warm | High, with the Soft film curve, the Soft & warm look, Light colour Normal, the game's ambient occlusion | TAA High, sharpening Low |
| Custom | Every setting shown, starting from the last preset | As set |

Every value, generated from the presets by `graphics_plugin_test` (it fails
when this table and the code disagree):

<!-- preset-table:begin -->
| Setting | Performance | Low | Balanced | High | Ultra | Cinematic | Natural | Vibrant | Moody | Clear Day | Soft & warm |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Antialiasing | FXAA | SMAA | TAA | TAA | TAA extra smooth | TAA extra smooth | TAA | TAA extra smooth | TAA | TAA | TAA |
| AA quality | Medium | Medium | High | High | Ultra | Ultra | High | Ultra | High | High | High |
| Sharpening | Off | Off | Low | Low | Low | Medium | Low | Low | Low | Medium | Low |
| Game SSAO | Off | Off | On | On | On | On | On | On | On | On | On |
| Detailed shading | Off | Off | Off | Medium | High | High | Off | High | Medium | Medium | Off |
| Contact shadows | Off | Off | Low | Medium | High | High | Medium | Medium | Medium | Medium | Medium |
| Bounce light | Off | Off | Off | Medium | High | High | Medium | Medium | Medium | Medium | Medium |
| Volumetric fog | Off | Off | Low | Low | Low | Low | Off | Low | Low | Off | Low |
| Sun shafts | Off | Low | Medium | High | Ultra | Ultra | Low | High | High | Low | Medium |
| Shaft clarity | Light | Light | Light | Light | Clear | Clear | Light | Clear | Light | Clear | Clear |
| Volumetric quality | Low | Low | Medium | High | High | Ultra | Medium | High | High | Medium | High |
| Lamp beams | Off | Low | Medium | Medium | High | High | Medium | High | Medium | Medium | Medium |
| Lamp budget | 2 | 2 | 4 | 6 | 8 | 8 | 6 | 8 | 6 | 6 | 6 |
| Base fog | Reduced | Reduced | Reduced | Reduced | Reduced | Reduced | Reduced | Light | Reduced | Light | Reduced |
| Tone mapping | Off | Filmic | Filmic | Filmic | Filmic | Filmic | Clean | Filmic | Filmic | Clean | Soft film |
| Colour look | None | None | None | Vivid | Vivid | Teal & orange | None | Vivid | Warm | None | Soft & warm |
| Look strength | Normal | Normal | Normal | Subtle | Normal | Strong | Subtle | Strong | Strong | Subtle | Normal |
| Light colour | Game | Game | Game | Game | Game | Game | Game | Game | Subtle | Game | Normal |
| Bloom | Off | Subtle | Subtle | Normal | Strong | Strong | Subtle | Normal | Normal | Normal | Normal |
| Sun glare | Off | Subtle | Subtle | Normal | Strong | Strong | Subtle | Normal | Normal | Normal | Normal |
| Vignette | Off | Off | Off | Off | Light | Medium | Off | Light | Medium | Off | Light |
| Night sky | Off | Subtle | Normal | Normal | Bright | Bright | Subtle | Bright | Normal | Normal | Normal |
| Moonlight | Off | Low | Medium | Medium | Medium | Medium | Low | Medium | Low | Medium | Medium |
| Sun disc | Off | Soft | Normal | Normal | Bright | Bright | Soft | Bright | Soft | Normal | Normal |
| Cloud layer | Off | Off | Few | Some | Many | Many | Few | Many | Many | Few | Some |
| Water reflections | Off | Off | Off | Medium | High | High | Medium | High | Medium | Medium | Medium |
| Sun glint | Off | Off | Off | On | On | On | On | On | On | On | On |
| Object draw distance | Game | Game | Game | 1.5x | 2x | 3x | 1.5x | 2x | 1.5x | 1.5x | 1.5x |
| Depth of field | Off | Off | Off | Off | Off | Medium | Off | Off | Off | Off | Off |
| Motion blur | Off | Off | Off | Off | Off | Low | Off | Off | Off | Off | Off |
<!-- preset-table:end -->

High and Ultra do not claim greater texture resolution or sample counts.

Performance is a combination of settings, not a measured FPS guarantee.
Native bloom is part of the game's own render pipeline. AnyGraphics registers
no finished-screen filter passes. SMAA copies scene colour before HUD drawing
for its AA passes.
Colour filters, tone curves, grain, vignette, chromatic separation and split
comparison have been removed. Antialiasing choices are native FXAA, SMAA 1x,
Enhanced SMAA, TAA and TAA extra smooth. MSAA and DLSS are not implemented.

## Apply and persistence

Use the Graphics tab's existing Apply Changes, Reset and Back buttons. Draft
changes stay pending until Apply; Cancel discards them. Reset stages defaults.
The mod participates in the native tab's dirty/default comparisons so the same
buttons work for both the original controls and AnyGraphics.

On first launch the mod imports retained native values from the old AnyHelpers
settings file. Removed effect values are ignored. Subsequent changes save to
`AnyAPI and Modding/mods/AnyGraphics/settings.tsv`. Other mods' settings are
untouched. The old shared file remains available for rollback.

Native hooks are confirmed active in the prior corrected build, with renderer
and scene calls recorded and no rejected frames. This native-only menu revision
has live menu acceptance from the user. The simplified quality levels still need a live check. A connection warning appears in the
section if native hooks are unavailable. Readiness and rejection counts are
also recorded in the game-root `anymaker_modding.log`.

See [Native scene controls](../api/scene-controls.md) and
[Menu extensions](../api/menu-extensions.md) for integration contracts.

## Scene antialiasing development

Since 0.28.0 the antialiasing choices are Game setting, Off, FXAA, **SMAA 1x (scene)** and **Enhanced SMAA (scene)**.
Enhanced SMAA adds stronger contrast-aware blending at diagonal edges; it may
soften fine scene detail. Both methods keep the HUD untouched.
Choosing SMAA reveals **SMAA quality: Low / Medium / High / Ultra**. Use Apply
Changes. SMAA disables native FXAA once ready and processes scene colour before
the HUD, preserving UI sharpness. The game-bound visual/FPS check is pending;
automated GPU smoothing and HUD restoration tests pass. Unsupported bindings
bypass rather than falling back to a finished-screen filter.

Since 0.30.0 (AnyAPI 0.36.0) two more choices appear:

- **TAA (scene)** blends each SMAA frame with recent frames, reprojected
  through scene depth and the camera. It stops the shimmer and crawling on
  foliage, wires and thin edges. Pixels that moved relative to the camera fall
  back to the current frame, so fast objects do not smear. Your own vehicle,
  which moves with the camera, stays stable.
- **TAA extra smooth (scene)** keeps more of the previous frames, so edges
  settle further. It can trail slightly behind fast motion. With AnyAPI 0.40.2
  it adds one level of history on top of the AA quality, so it also smooths
  more at Ultra; older APIs raise the AA quality one level instead.

Since 0.32.0 both TAA choices move the camera by a fraction of a pixel each
frame (before, plain TAA blended identical frames and smoothed almost nothing),
and edges keep their history instead of falling back to raw SMAA, so jagged and
crawling edges actually settle. Native FXAA no longer flickers on for a frame
when scene AA misses one.

**AA quality** sets SMAA's edge search and, with TAA, how much of the previous
frames is kept. **Sharpening** (Off / Low / Medium / High) restores detail
softened by AA. It is contrast-adaptive, so flat areas and hard edges are left
alone, and it runs before the HUD. **Show GPU cost** adds a line with the GPU
time of AnyAPI's passes and the frame rate; with AnyAPI 0.40.2 it lists every
pass and adds the frame-time line described under 0.33.0.

See [Scene AA development](../development/scene-antialiasing.md) and
[Scene antialiasing](../api/scene-antialiasing.md).

## Volumetric lighting

Version 0.29.0 adds native-depth fog, shadowed sun shafts and local-light beams
in the HDR scene before bloom, tone mapping, antialiasing and the HUD. Headlights,
torches and other native point/spot lights participate. Fog, beam strengths,
beam focus, sampling quality and local-light budgets are editable in Graphics.
Performance disables the added lighting; Low through Ultra progressively add
beams, fog and higher sampling budgets. Off restores game settings.

Version 0.30.0 fixes sun shafts looking like thick fog. Light now gathers
where it breaks through gaps (trees, windows, terrain) instead of lighting all
air out to 500 m. **Haze between beams** chooses Clear, Light (default),
Natural or Classic (the original look). **Beam reach** stops distant air from
piling up into haze. **Follow sun height** makes beams strongest and warm at
sunrise and sunset, subtle at noon, and off at night. **Smooth beams and fog**
blends recent frames to remove banding and noise, so lower sample counts look
clean.

See [Scene lighting](../scene-lighting.md) for preset values, the API and limits.
