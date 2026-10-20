# AnyGraphics

AnyGraphics is a native graphics mod for Anymaker 0.1.25. The current release is
**0.31.0**, which needs **AnyAPI 0.36.0** for TAA, sharpening, beam shaping,
tone mapping and colour looks, and the GPU cost readout (older APIs keep the 0.29 feature set; TAA choices
fall back to SMAA). Its controls live inside
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
spot after loading another world with photo mode still on. See the
[release records](../releases/README.md).

## Tone mapping and colour (0.30.0)

The **Finish** rows grade the HDR scene before the game's tone mapping, using
[scene effects](../api/scene-effects.md):

- **Tone mapping**: Off (game look), Game curve, Filmic (AgX-style, keeps
  colour in bright light) or Clean (bright and true to the game's colours).
  Anything but Off also replaces the game's bloom, so the Bloom row there
  takes over and the native Bloom row is hidden.
- **Colour look**: None, Warm, Cool, Teal & orange, Moody or Vivid (the
  shader-pack look), with **Look strength** Subtle, Normal or Strong.
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
Terrain and trees follow the game's own render distance. High and Ultra use
Far and Cinematic uses Very far.

**Sun** (Off, Soft, Normal or Bright) draws a round sun with a soft halo, and
**Cloud layer** (Off, Few, Some or Many) adds high drifting clouds above the
game's own. Both follow the time of day: the sun and clouds turn orange at
sunrise and sunset, and the clouds are moonlit at night. Presets use the sun
from Low up and clouds from Balanced up.

**Night sky** (Off, Subtle, Normal or Bright) adds stars, a milky way and a
moon at night, and **Moonlight** (Off, Low, Medium or High) adds soft blue
light from the moon. Both follow the game's own day and night: the stars turn
with the sun and the moon rises as the sun sets. The moon casts no shadows.
Presets use them from Low up.

## Controls

The original game controls remain above the mod section. Choosing Game setting
uses those values; choosing an override tells the renderer to use AnyGraphics'
value during gameplay. Override choices are explicitly labelled. Disable
AnyGraphics to return to the game's own rendering settings.

Controls include native FXAA, SSAO, shadows, fog blur, bloom intensity and
threshold, sunlight, sky light, ambient light, base fog and scene-lighting stops.
They alter native renderer switches or frame lighting inputs before the game
builds scene constants and records rendering commands.

Fog, bloom and optional lighting controls use **Off / Low / Medium / High /
Ultra** strength levels instead of numeric sliders. Bloom also offers Game
setting. These levels change native intensity inputs; they are not separate
shadow-map or SSAO sampling-quality modes. Shadows, ambient occlusion and fog
blur retain the game's actual on/off capability.

Presets are **Off / Performance / Low / Balanced / High / Ultra / Cinematic /
Natural / Vibrant / Moody / Clear Day / Custom**. Choosing a preset stages a group of
settings. Off bypasses overrides and restores the game's own values. Balanced is
the initial preset.

Every preset except Custom shows only a few common settings: Antialiasing,
Fog, Brightness, Colour look, Depth of field (with Blur strength), Motion blur,
Object draw distance and Show GPU cost. Changing one of them keeps the preset.
**Custom** shows every setting and starts from the values of the preset you
were on, so a look can be fine-tuned from the closest preset.

**Reset preset** puts every setting back to the selected preset's values.
**Auto-pick preset** measures the frame rate in gameplay for a few seconds per
preset and steps between Performance and Ultra until the game holds about
60 fps, then says which preset it chose. Close the menu and play while it runs.

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
| 5 | Colour look: preset, None, Warm, Cool, Teal & orange, Moody, Vivid |
| 6 | Vignette: preset, Off, Light, Medium, Strong |
| 7 | Bloom: preset, Off, Subtle, Normal, Strong |
| R | Reset the photo controls and speed |
| H | Keep the help panel open, or hide it |

The camera starts exactly where your view was and stays within 200 m of that
point, because the world is only loaded around you. While it flies, your
character stays where it is and mouse clicks do nothing, so you cannot shoot
or build from the camera. Photo controls last until the game closes and are
never saved into your preset. The free camera hooks the game directly and
works on Anymaker 0.1.25 only; on another game build photo mode still hides
the HUD and the photo controls still work.

**Local light beams** (headlights, torches, lamps) light the air in clear,
thin beams that reach a little past each light's own range. Looking straight
into a lamp is capped so it never whites out the screen, the glow rolls off
softly, and beams fade in daylight the way real headlights do.

No preset goes past Low volumetric fog or Medium base fog, because thick fog
washes out the whole scene. More fog is available in Custom.

| Preset | Native settings | Antialiasing |
| --- | --- | --- |
| Off | Game settings; no native overrides | Game setting |
| Performance | SSAO, shadows, fog blur, bloom, base fog, clouds and grass off | FXAA |
| Low | Shadows on, low bloom/base fog; SSAO, fog blur and grass off | SMAA Medium |
| Balanced | Native effects on; medium bloom/base fog, contact shadows Low | TAA High |
| High | Native effects on; high bloom, medium base fog; subtle Vivid look, Medium detailed shading, bounce light and contact shadows, local light beams | TAA High, sharpening Low |
| Ultra | As High with a Vivid look, strong bloom and glare, High detailed shading, bounce light and contact shadows, bright sun, many clouds, very far objects and clear sun beams | TAA + jitter Ultra, sharpening Low |
| Cinematic | As Ultra, with a strong teal & orange film grade, a medium vignette, stronger beams, extreme object distance, stronger sun beams, Medium depth of field and Low motion blur | TAA + jitter Ultra, sharpening Medium |
| Natural | High, with Clean tone mapping, no colour look or vignette, softer bloom and glare, no volumetric fog | TAA High, sharpening Low |
| Vibrant | Ultra, with a strong Vivid look, many clouds, a bright sun and stars, strong clear sun beams and low base fog | TAA + jitter Ultra, sharpening Low |
| Moody | High, with a strong warm golden-hour grade, a medium vignette, strong sun beams, light haze and many clouds | TAA High, sharpening Low |
| Clear Day | High, with Clean tone mapping, no colour look or vignette, low base fog, no volumetric fog, short clear beams | TAA High, sharpening Medium |
| Custom | Every setting shown, starting from the last preset | As set |

High and Ultra do not claim greater texture resolution or sample counts.

Performance is a combination of settings, not a measured FPS guarantee.
Native bloom is part of the game's own render pipeline. AnyGraphics registers
no finished-screen filter passes. SMAA copies scene colour before HUD drawing
for its AA passes.
Colour filters, tone curves, grain, vignette, chromatic separation and split
comparison have been removed. Antialiasing choices are native FXAA, SMAA 1x,
Enhanced SMAA, TAA and TAA + jitter. MSAA and DLSS are not implemented.

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
- **TAA + jitter (scene, smoothest)** also moves the camera by a fraction of a
  pixel each frame, so edges settle into a supersampled look. It can trail
  slightly behind fast motion.

**AA quality** sets SMAA's edge search and, with TAA, how much of the previous
frames is kept. **Sharpening** (Off / Low / Medium / High) restores detail
softened by AA. It is contrast-adaptive, so flat areas and hard edges are left
alone, and it runs before the HUD. **Show GPU cost** adds a line with the GPU
time of volumetric lighting and scene AA, plus the frame rate.

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
