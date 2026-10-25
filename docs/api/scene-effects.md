# Scene effects

AnyAPI 0.36.0. Query `anyapi.scene_effects`, version 1, using
`AnyServicesV1::query`. The service table is declared in
[`anyapi_scene_effects_v1.h`](../../sdk/include/anyapi_scene_effects_v1.h).
Version 2 (AnyAPI 0.40.2, [`anyapi_scene_effects_v2.h`](../../sdk/include/anyapi_scene_effects_v2.h))
adds reflections and a sun glint on the ocean; see [Water](#water-version-2).
Version 3 (AnyAPI 0.40.2, [`anyapi_scene_effects_v3.h`](../../sdk/include/anyapi_scene_effects_v3.h))
adds the night sky and cloud models, weather response, shading and tone
revisions, bloom falloff, dither and a per-pass status; see [Version 3](#version-3).

Scene effects finish the HDR scene: tone mapping, a colour look, eye
adaptation, bloom, sun glare and vignette. The pass records on the game's
command list right after [scene lighting](../scene-lighting.md), at
`renderer._record_commands_additive`, before the game's bloom, tone mapping,
scene antialiasing and the HUD. The HUD is never touched.

## How tone mapping is replaced

The game tone maps every frame with a fixed chain, decoded from
`pipeline_postprocess` on Anymaker 0.1.25:

```
display = saturation 1.15 ( contrast 1.12 around 0.45 ( gamma 1/2.2 ( ACES fit ( 0.85 * hdr ))))
```

The composite works out the display colour it wants (from the chosen curve
and look), then writes the HDR value that this chain turns into exactly that
colour. The game's vignette and screen fades still apply afterwards. A dither
of about one 8-bit step (triangular noise, never on pure black or white)
removes banding in skies and fog; it is on unless a version 3 owner sets
`dither` 0. With `tonemap` 0, no look, exposure, eye adaptation, bloom, glare,
vignette or dither, there is nothing to change: the pass records nothing and
the image is exactly the game's own, including its bloom.

## Parameters

`set` takes `AnySceneEffectsParametersV1` (matching `struct_size`, `version` 1):

| Field | Range | Meaning |
| --- | --- | --- |
| `enabled` | 0/1 | Turn the service on |
| `finish` | 0/1 | Run the finishing pass (the rows below up to `vignette`). While it renders, the game's own bloom is turned off because this pass replaces it; the game's bloom comes back 15 frames after the pass stops rendering (bypassed, night vision or thermal, or nothing to change) |
| `tonemap` | 0-3 | 0 Game (the game's ACES curve), 1 Filmic (AgX-style, keeps colour in bright light), 2 Clean (Khronos PBR Neutral), 3 Soft film (AnyAPI 0.40.1; Uchimura curve: a long straight middle and a slow highlight roll-off) |
| `look` | 0-6 | None, Warm, Cool, Teal & orange, Moody, Vivid, Soft & warm (AnyAPI 0.40.1; golden highlights, cool shade) |
| `look_strength` | 0-1 | How strongly the look is applied |
| `exposure` | -3..3 | Manual stops before the curve |
| `eye_adaptation` | 0/1 | Adds up to ±1.5 stops after big brightness changes, then settles back (see below) |
| `adaptation_seconds` | 0.2-10 | Time constant of the fast adaptation |
| `bloom` | 0-1 | Threshold-free 6-level bloom (13-tap downsample with a Karis average on the first level, tent upsample) |
| `glare` | 0-2 | Halo, core and six faint streaks around the sun while it is on screen |
| `vignette` | 0-1 | Darkens the corners |
| `ambient_occlusion` | 0/1 | Replace the game's SSAO output with detailed ambient occlusion (see below). The game's SSAO switch is forced on while this is on, and the game's SSAO computation is skipped in frames this pass renders |
| `ao_radius` | 0.25-4 | World radius of the occlusion search, in metres |
| `ao_strength` | 0-2 | How dark occlusion gets |
| `night_sky` | 0/1 | Run the night sky pass (see below) |
| `stars` | 0-2 | Brightness of the stars and milky way |
| `moon` | 0-2 | Brightness of the moon and its glow |
| `moonlight` | 0-2 | Strength of the unshadowed moonlight on surfaces |
| `contact_shadows` | 0/1 | Run the contact shadow pass (see below) |
| `contact_length` | 0.05-4 | Length of the shadow rays toward the sun, in metres |
| `bounce_light` | 0-2 | One bounce of screen-space indirect light; 0 is off |
| `depth_of_field` | 0-4 | Lens blur away from the focus distance; 0 (default) is off |
| `motion_blur` | 0-2 | Camera motion blur; 0 (default) is off |
| `sun_disc` | 0-2 | Sun disc in the game's sun colour; 0 (default) is off |
| `clouds` | 0-2 | Drifting cloud layer, amount of cover; 0 (default) is off |
| `draw_distance` | 1-4 | Multiplier on the game's object cull distances; 1 (default) is the game's own |
| `hide_ui` | 0/1 | Photo mode: turns on the game's own disable-UI switch, hiding the HUD, crosshair and menus, and hides the damage, flash and acid screen-edge overlays; 0 (default) leaves the UI alone |

Only one active mod owns the policy, as with the other scene services.

Turn `hide_ui` off whenever a menu needs to be seen; AnyGraphics does this whenever the player leaves gameplay. `photo_available` in the status says the switch was found. `hide_ui` and `draw_distance` need only an active owner, so they work even when the scene lighting hook is unavailable.

## Water (version 2)

Query `anyapi.scene_effects` version 2 for `AnySceneEffectsV2`. Its `set` takes
`AnySceneEffectsParametersV2` (`struct_size`, `version` 2): the whole v1
parameter block in `effects` (`struct_size`, `version` 1, the same ranges as
above) plus:

| Field | Range | Meaning |
| --- | --- | --- |
| `water_reflections` | 0-1 | Screen-space reflections on the ocean; 0 is off |
| `sun_glint` | 0-2 | Sun highlight on the rippled ocean; 0 is off |
| `reflection_quality` | 0-2 | 16, 24 or 32 ray steps (each followed by 4 refinement steps) |

Values out of range, a wrong size or version, or another mod owning the policy
reject the call and change nothing. The water draws only while `effects.enabled`
is 1. Version 1 stays available and unchanged; a v1 `set` turns the water off,
so a mod that does not know about it never leaves it on. `status` is the v1
status call, and `status_v2` fills `AnySceneEffectsStatusV2`: the v1 status in
`effects`, plus `water_ready`, `water_ms` (smoothed GPU time, negative until
measured) and `water_frames`.

The pass records after bounce light and before volumetric lighting, so fog,
sun shafts and the finishing pass treat the water like the rest of the scene:

- **Where.** Only pixels the game marks as ocean in its G-buffer are touched;
  the pass reads that mark as a mask and nothing else. The ocean writes no
  depth, so the water surface is the sea level plane (y = 0) under each pixel.
  The pass records nothing while the game's ocean is off, while the camera is
  more than a metre under the sea, and in special views (scope, vision modes).
- **Surface.** A flat sea with small value-noise ripples (8 m down to 1 m), fixed
  in the world and drifting with the game's wind, stronger as the wind rises.
  Ripples smaller than a pixel fade out and widen the glint instead, so distant
  water is a calm mirror rather than noise.
- **Reflections.** At half resolution the reflected ray is traced across the
  screen through the depth buffer (linear steps with jitter, then a binary
  search, a thickness test, and a fade toward the screen edges and for rays
  that turn back toward the camera). A full-resolution composite upsamples the
  result, using only ocean texels at a similar distance, and adds
  `Fresnel × (hit colour − sky estimate)` with Schlick Fresnel (0.02 head-on).
  The sky estimate is the game's own sky gradient, which its water already
  reflects, so a ray that finds nothing leaves the pixel exactly as the game
  drew it.
- **Glint.** A GGX highlight (roughness 0.1, plus the ripple detail lost to
  distance) in the game's sun colour, at the same brightness scale as the
  game's sunlight, capped so it blooms instead of turning into a flat band. It
  fades out as the sun reaches the horizon.
- Both fade with distance like the game's own fog.

Limits: only what is on screen can reflect (an object above the top of the
screen or behind the camera does not); the glint is not shadowed, so it can
show on water in the shade of a cliff or boat; waves and swell are not
modelled, and the surface is always taken at sea level.

## Eye adaptation

A 64x36 centre-weighted log-luminance grid is averaged into a fast value
(`adaptation_seconds`) and a slow baseline (about 45 seconds). The exposure
added is `0.75 × (slow − fast)` stops, clamped to ±1.5. Walking into a cave
brightens the view, and looking out of it dims it, and both settle back as the
baseline catches up. The game's own day and night brightness therefore stays
as designed. Adaptation restarts after a pause of more than half a second.
The grid is averaged with 32x18 filtered reads (each covers four cells, so the
average is the same). Metering runs only while eye adaptation or glare is on;
otherwise `scene_luminance` is −1 and `adapted_exposure` 0. With version 3
`weather` 1, a lightning flash and the quarter second after it do not move the
adaptation (at most 1.5 s of hold in any 5 s).

## Sun glare

Sixteen depth samples around the sun's screen position measure how much of its
disc is open sky (reversed depth: sky is 0, and neither the ocean nor the
game's own clouds count as sky). Glare scales with that fraction, smoothed
over about 40 ms, and fades out at the horizon. Trees, terrain, buildings, the
sea and the game's clouds in front of the sun dim it. Samples that fall off
screen are not counted, and while the sun is off screen the last value is
kept, so the halo does not jump at the screen edge. There is no glare
underground or underwater.

## Ambient occlusion

The pass runs at the game's SSAO call (8 samples, inside
`renderer._record_commands_composite`), before the lighting composite reads
the SSAO target. When it renders, the game's own SSAO is skipped (it used to
be computed and then overwritten); when it bypasses, the game's SSAO runs in
the same frame as before. A version 3 owner can keep the game's SSAO running
with `ANY_FX_KEEP_NATIVE_SSAO`. It writes that target with Alchemy ambient
occlusion: 12 spiral samples with a rotation from a 4x4 interleaved pattern
that the 4x4 blur cancels exactly (fixed per pixel, so it does not crawl on
foliage; it moves per frame only while TAA resolves it), a world-space radius
with falloff capped at 80 pixels,
samples that fall off screen counted as open (so the screen border never
darkens), a depth-aware 4x4 blur and a multi-bounce fit on the
albedo so bright surfaces do not go grey. The game multiplies only ambient and
back light by this value, so direct sunlight is never darkened. Sky and cloud
pixels are left unoccluded. It also runs underground and underwater.

With version 3 `shading_revision` 1 (the default for version 3 owners) the
radius grows with distance (0.03 × distance, at least `ao_radius`, at most
4 m) while the strength shrinks with it; the footprint is capped at 7.5% of
the screen height (at least 32 pixels), with the world radius shrunk to match,
so the result no longer changes with resolution; samples more than half a
radius in front of the pixel fade out by 1.5 radii, so objects standing in
front cast no halo; and occluders above the pixel count more than those below,
as they block more of the sky.

## Contact shadows

Before volumetric lighting, twelve screen-space steps toward the sun from each
pixel within 60 m look for nearby geometry the game's shadow cascades are too
coarse to catch (under parts, between blocks, where things meet the ground).
Where a step lands behind the depth buffer within a short thickness, the
pixel's sunlight is removed. That sunlight is rebuilt from the G-buffer and
the game's own sun colour (`albedo × sun × (N·L + rim) × 0.4`), and the result
never drops below the pixel's ambient and back light, so pixels the game
already shadows are left alone, and only surfaces turned toward the sun are
shadowed (surfaces facing away are in the game's own shadow already). The
back light term uses the side facing away from the sun, as the game's does
(it used the sun side before 0.40.2). The pass fades out as the sun sets and
is inactive underground and underwater.

## Bounce light

After contact shadows and before volumetric lighting, ten spiral samples
within 2.5 m (out to 120 m from the camera) gather the lit colour of nearby
surfaces that face the pixel, weighted by both surfaces' angles and distance,
and add it tinted by the pixel's albedo. A red wall warms the ground next to
it. Only what is on screen can bounce; sky and ocean pixels neither send nor
receive (the ocean writes no depth, so its pixels carry the seabed's). The
pass is inactive underwater. With version 3 `shading_revision` 1 the
footprint is capped at 18.5% of the screen height instead of 200 pixels, and a
sender is skipped when the depth buffer shows something in front of the
midpoint between it and the pixel, so light no longer bounces through thin
walls and pillars.

## Depth of field and motion blur

After the night sky and before finishing, the lens pass redraws the HDR scene
from a copy. Depth of field focuses on the nearest of five taps around the
screen centre (ignoring anything within a metre, such as held items) and eases
the focus over about a quarter second; the blur radius follows the difference
in inverse distance from the focus, up to 12 pixels at 1080p (scaled up with
`depth_of_field` above 1, at most 48). The ocean writes no depth, so on ocean
pixels focus and blur use the sea surface (sea level under the camera) rather
than the seabed. Nothing within 1.5 m gets a blur circle (hands, held items,
cockpits), ramping in to 2.5 m, and blur taps are weighted down by their
brightness so a single bright point does not swamp a soft background. Motion
blur reprojects each pixel with last frame's camera (position and axes), so
it blurs camera turns and movement only: the game has no per-object motion.
Within about 6 m of the camera the blur fades out, and vehicles, vehicle parts
and characters (the game's rim flag) get no camera blur within 25 m, fading
back to full by 40 m, so the vehicle you ride in stays sharp. Timing uses the
high-resolution clock. History resets after a pause, a resize, a jump of more
than 20 m, a turn of more than 60° or a zoom of more than 30% in one frame.
The sampling pattern moves per frame only while TAA resolves it.

## Draw distance

`draw_distance` scales the distance thresholds the game passes to its
distance-cull helpers: characters 256 m, props 1024 m, dropped items 128 m,
projectiles 256 m and vehicle parts 128 m at 1. AnyAPI hooks the helpers'
shared call cells and multiplies the threshold before calling the original,
so the game still makes every cull decision. Terrain, trees and grass follow
the game's own render distance. Farther costs CPU and GPU time in busy areas
(the game culls and draws more objects on its main thread), and a hosting
player may see a lower server tick. `distance_available` in the status says the
hooks are installed and `distance_scale` is the multiplier in use.

## Sun and cloud layer

The sky pass also runs by day when `sun_disc` or `clouds` is above 0. The sun
is a limb-darkened disc about 1.4° across with a tight halo, in the game's own
sun colour, so it reddens at dusk with the rest of the lighting. The cloud
layer is five octaves of noise on a plane about 1.5 km up, drifting slowly
with wall time; `clouds` sets how much of the sky it covers. It is lit by the
sun by day (with a silver lining toward the sun and darker bases where the
cloud thickens toward the light), by the moon at night, and always by the
game's sky colour as ambient, so it follows the day/night cycle. Clouds are
blended as premultiplied colour over the sky and hide the stars, moon and sun
behind them. Everything added here sits behind the game's own clouds, and
only sky pixels are touched. Version 3 `cloud_model` 1 is clouds v2: the far
octaves are filled with their mean instead of fading (so cover no longer
changes with zoom and resolution), dense cloud can hide the sun, the sun and
moon key the cloud lighting, and the layer drifts with the game's wind.

## Night sky

The night sky pass is blended additively onto the HDR scene at the additive
stage, before the finishing pass, so tone mapping and bloom treat it like the
rest of the scene. Everything follows the game's own sun
(`scene_renderer` +0xa78), so it rides the day/night cycle:

- A night factor fades in as the sun drops below the horizon. In daylight
  the pass records nothing.
- Stars and a faint milky way sit on sky pixels (depth 0, not ocean). The
  star field turns about the same axis as the sun, and stars twinkle and fade
  near the horizon. Clouds have real depth, so they cover the stars.
- The moon sits opposite the sun, lifted so it stands high at midnight. It
  has a phase-lit disc with maria and a soft glow.
- Moonlight lights surfaces facing the moon, using the G-buffer normal and
  albedo. The game has no shadow map for the moon, so moonlight casts no
  shadows.

Version 3 `sky_model` 1 follows the game's own clock instead (the time of day
the game keeps, `scene_renderer` +0xba8): stars and the moon show all night,
the moon goes through its phases, the sky fades out at dawn, and the star
field is sparser. `moon_disc_follows_light` 1 puts the moon disc where the
game's night light comes from (with `anyapi.scene_controls` v4 `moon_mode` 1).
With `weather` 1, rain, snow and storms dim the stars, moon and sun disc,
thicken the clouds and roughen the sea; it also lets storms dim the scene
controls v4 moonlight, so the moon and its light dim together or not at all. `sky_model_active`, `cloud_model_active`,
`weather_active` and `clock_valid` in the version 3 status say what is in use.

## Status

`status` fills `AnySceneEffectsStatusV1`: `available`, `ready`, frame and
rejected-frame counts, `gpu_ms` (smoothed GPU time of the pass),
`adapted_exposure` (stops currently added by eye adaptation),
`scene_luminance` (measured average) and `sun_visibility`. `ao_available`,
`ao_ready`, `ao_frames` and `ao_ms` describe the ambient occlusion pass, and
`sky_ready`, `sky_ms` and `night` (0 by day, 1 at night) the night sky pass,
`contact_ready` and `contact_ms` the contact shadow pass, `gi_ready` and
`gi_ms` the bounce light pass, and `lens_ready` and `lens_ms` the depth of
field and motion blur pass, and `distance_available` and `distance_scale`
the draw distance hooks. `status_v2` adds the water pass (see above), and
`status_v3` adds the per-pass error codes (see [Version 3](#version-3)). The
values are read back seven frames late without a fence.

## Limits

In night vision and thermal views the finishing pass leaves the game's own
look alone. Underground and underwater it keeps the curve, look, bloom and
exposure but draws no glare; contact shadows are inactive there, and bounce
light underwater. These are inactive states, not errors: no log line and a
`pass_error` of −1. Each pass is bypassed, with a logged reason and code, when
the native targets, camera or command bindings fail their checks, or when it
cannot create its resources (retried after 1, 2, 4 … 60 s, not every frame).
Ambient occlusion is bypassed the same way and the game's own SSAO then runs.
The game's bloom is off only while the finishing pass renders, and returns 15
frames after it stops. A pass that stays off for about two seconds frees its
GPU memory. Effects cover the scene only: water and glass
are finished like everything else, and no material data is used, apart from the
ocean mask the version 2 water pass reads. The water pass bypasses the same
way, with `SCENE_WATER bypass=` reasons in the log, and the game's water is
then left as it is.

## Version 3

Query `anyapi.scene_effects` version 3 for `AnySceneEffectsV3`. Its `set`
takes `AnySceneEffectsParametersV3` (`struct_size`, `version` 3): the whole
version 2 block in `water` (`struct_size`, `version` 2, the same rules as
above) plus:

| Field | Range | Default | Meaning |
| --- | --- | --- | --- |
| `sky_model` | 0/1 | 1 | 0 the version 2 night sky; 1 the game clock (see [Night sky](#night-sky)) |
| `cloud_model` | 0/1 | 1 | 0 the version 2 clouds; 1 clouds v2 (see [Sun and cloud layer](#sun-and-cloud-layer)) |
| `weather` | 0/1 | 0 | 1: rain, snow and storms dim stars, moon and sun disc, thicken clouds, roughen the sea, and hold eye adaptation on lightning |
| `moon_disc_follows_light` | 0/1 | 0 | 1: the moon disc sits at the game's night light direction |
| `shading_revision` | 0/1 | 1 | 0 the version 2 ambient occlusion and bounce light; 1 the revised ones (see [Ambient occlusion](#ambient-occlusion), [Bounce light](#bounce-light)) |
| `tone_revision` | 0/1 | 0 | 1: Filmic with more midtone contrast (mid grey about 0.51 on screen instead of 0.48, closer to the game's own curve) |
| `bloom_falloff` | 0.5-1 | 1 | Weight r^n for bloom level n; lower keeps bloom tighter around bright sources. 1 is version 2 |
| `dither` | 0/1 | 1 | About one 8-bit step of dither before output |
| `flags` | bits | 0 | `ANY_FX_KEEP_NATIVE_SSAO` (1): run the game's SSAO even when the ambient occlusion pass renders |
| `reserved[4]` | 0 | 0 | Must be 0 |

A wrong size or version, a value out of range, an unknown flag, a non-zero
reserved word or another mod owning the policy rejects the call and changes
nothing. Version 1 and 2 `set` calls reset these fields to the values that
give the version 2 output: models, weather and revisions 0, `bloom_falloff` 1,
`dither` 1, `flags` 0.

`status_v3` fills `AnySceneEffectsStatusV3`: the version 2 status in `base`, then

| Field | Meaning |
| --- | --- |
| `pass_error[16]` | Per pass, indexed like `anyapi.scene_timing` v2 passes (0 ambient occlusion, 1 contact, 2 bounce, 3 water, 4 volumetric, 5 sky, 6 lens, 7 finish, 8 antialiasing): 0 ok or off, −1 inactive by design (a view or night skip, or nothing to change), above 0 the last bypass code |
| `sky_model_active` | The game-clock night sky is drawing |
| `cloud_model_active` | Clouds v2 are drawing |
| `weather_active` | Weather is being applied (needs the game's weather read by `anyapi.scene_controls`) |
| `clock_valid` | A clock was read within the last two frames: the game clock or the parked-night fallback ([scene controls](scene-controls.md) `status_v2` `clock_source` 1 or 2) |
| `native_ssao_skipped` | The game's SSAO was skipped last frame because this service's ambient occlusion rendered |
| `dither_active` | The finishing pass rendered with dither |
| `sun_visibility` | As in version 1: the open-sky fraction of the sun disc, 0 while glare is off |

Bypass codes: finish 2-5 and 101-105, ambient occlusion 200-205, contact
400-424, bounce 500-524, lens 600-624; each pass logs its first bypass and
every 600th with `<PASS> bypass=<reason> code=<n>`.

These fixes apply to every version: night vision, thermal, bunker and
underwater handling; the ocean in glare, bounce light and lens focus; the
contact shadow back light; no lens blur on hands and held items, and none of
the camera's motion on the vehicle you ride; the bright-tap weighting and cut
detection in the lens pass; Soft film keeping strong colours (an orange lamp
stays orange instead of turning yellow); skipping the game's SSAO and unused
metering; and noise that moves only under TAA.
