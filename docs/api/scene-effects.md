# Scene effects

AnyAPI 0.36.0. Query `anyapi.scene_effects`, version 1, using
`AnyServicesV1::query`. The service table is declared in
[`anyapi_scene_effects_v1.h`](../../sdk/include/anyapi_scene_effects_v1.h).

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
colour. The game's vignette and screen fades still apply afterwards. With
`tonemap` 0 and no look, bloom, glare or vignette, the pass is an exact round
trip and the image is unchanged.

## Parameters

`set` takes `AnySceneEffectsParametersV1` (matching `struct_size`, `version` 1):

| Field | Range | Meaning |
| --- | --- | --- |
| `enabled` | 0/1 | Turn the service on |
| `finish` | 0/1 | Run the finishing pass (the rows below up to `vignette`). While it runs, the game's own bloom is turned off for the frame because this pass replaces it |
| `tonemap` | 0-3 | 0 Game (the game's ACES curve), 1 Filmic (AgX-style, keeps colour in bright light), 2 Clean (Khronos PBR Neutral), 3 Soft film (AnyAPI 0.40.1; Uchimura curve: a long straight middle and a slow highlight roll-off) |
| `look` | 0-6 | None, Warm, Cool, Teal & orange, Moody, Vivid, Soft & warm (AnyAPI 0.40.1; golden highlights, cool shade) |
| `look_strength` | 0-1 | How strongly the look is applied |
| `exposure` | -3..3 | Manual stops before the curve |
| `eye_adaptation` | 0/1 | Adds up to ±1.5 stops after big brightness changes, then settles back (see below) |
| `adaptation_seconds` | 0.2-10 | Time constant of the fast adaptation |
| `bloom` | 0-1 | Threshold-free 6-level bloom (13-tap downsample with a Karis average on the first level, tent upsample) |
| `glare` | 0-2 | Halo, core and six faint streaks around the sun while it is on screen |
| `vignette` | 0-1 | Darkens the corners |
| `ambient_occlusion` | 0/1 | Replace the game's SSAO output with detailed ambient occlusion (see below). The game's SSAO switch is forced on while this is on |
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
| `hide_ui` | 0/1 | Photo mode: turns on the game's own disable-UI switch, hiding the HUD, crosshair and menus; 0 (default) leaves the UI alone |

Only one active mod owns the policy, as with the other scene services.

Turn `hide_ui` off whenever a menu needs to be seen; AnyGraphics does this whenever the player leaves gameplay. `photo_available` in the status says the switch was found.

## Eye adaptation

A 64x36 centre-weighted log-luminance grid is averaged into a fast value
(`adaptation_seconds`) and a slow baseline (about 45 seconds). The exposure
added is `0.75 × (slow − fast)` stops, clamped to ±1.5. Walking into a cave
brightens the view, and looking out of it dims it, and both settle back as the
baseline catches up. The game's own day and night brightness therefore stays
as designed. Adaptation restarts after a pause of more than half a second.

## Sun glare

Sixteen depth samples around the sun's screen position measure how much of its
disc is open sky (reversed depth: sky is 0). Glare scales with that fraction,
smoothed over a few frames, and fades out at the horizon. Trees, terrain and
buildings in front of the sun dim it.

## Ambient occlusion

The game's SSAO (8 samples, inside `renderer._record_commands_composite`) is
followed by this pass, from the same call cell, before the lighting composite
reads the SSAO target. It overwrites that target with Alchemy ambient
occlusion: 12 spiral samples with a fixed per-pixel rotation (so it does not
crawl on foliage), a world-space radius with falloff capped at 80 pixels,
samples that fall off screen counted as open (so the screen border never
darkens), a depth-aware 4x4 blur and a multi-bounce fit on the
albedo so bright surfaces do not go grey. The game multiplies only ambient and
back light by this value, so direct sunlight is never darkened. Sky and cloud
pixels are left unoccluded.

## Contact shadows

Before volumetric lighting, twelve screen-space steps toward the sun from each
pixel within 60 m look for nearby geometry the game's shadow cascades are too
coarse to catch (under parts, between blocks, where things meet the ground).
Where a step lands behind the depth buffer within a short thickness, the
pixel's sunlight is removed. That sunlight is rebuilt from the G-buffer and
the game's own sun colour (`albedo × sun × (N·L + rim) × 0.4`), and the result
never drops below the pixel's ambient and back light, so pixels the game
already shadows are left alone, and only surfaces turned toward the sun are
shadowed (surfaces facing away are in the game's own shadow already). The pass fades out as the sun sets.

## Bounce light

After contact shadows and before volumetric lighting, ten spiral samples
within 2.5 m (out to 120 m from the camera) gather the lit colour of nearby
surfaces that face the pixel, weighted by both surfaces' angles and distance,
and add it tinted by the pixel's albedo. A red wall warms the ground next to
it. Only what is on screen can bounce, and sky pixels send nothing.

## Depth of field and motion blur

After the night sky and before finishing, the lens pass redraws the HDR scene
from a copy. Depth of field focuses on the nearest of five taps around the
screen centre (ignoring anything within a metre, such as held items) and eases
the focus over about a quarter second; the blur radius follows the difference
in inverse distance from the focus, up to 12 pixels at 1080p (scaled up with
`depth_of_field` above 1, at most 48). Motion blur
reprojects each pixel with last frame's camera (position and axes), so it
blurs camera turns and movement only: the game has no per-object motion.
Within about 6 m of the camera the blur fades out, so cockpits and vehicles
that move with the camera stay sharp. History resets after a pause, a resize
or a jump of more than 20 m.

## Draw distance

`draw_distance` scales the distance thresholds the game passes to its
distance-cull helpers: characters 256 m, props 1024 m, dropped items 128 m,
projectiles 256 m and vehicle parts 128 m at 1. AnyAPI hooks the helpers'
shared call cells and multiplies the threshold before calling the original,
so the game still makes every cull decision. Terrain, trees and grass follow
the game's own render distance. `distance_available` in the status says the
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
only sky pixels are touched.

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
the draw distance hooks. The values are read
back seven frames late without a fence.

## Limits

The pass is bypassed, with a logged reason, in special views (scope, vision
modes), when the native HDR target, depth or command bindings fail their
checks, or when the pass cannot create its resources. Ambient occlusion is bypassed the
same way and the game's own SSAO result is then kept. The game's bloom stays
off while the policy is enabled. Effects cover the scene only: water and glass
are finished like everything else, and no material data is used.
