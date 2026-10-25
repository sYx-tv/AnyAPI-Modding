# Scene lighting

AnyAPI and AnyGraphics (0.29.0, overhauled in 0.30.0) add HDR fog, sun shafts and local-light scattering
for Anymaker 0.1.27 / Steam build 26113663.

## Render integration

The pass records immediately after `renderer._record_commands_additive`, before
native bloom, tone mapping, scene antialiasing and the HUD. Sun shafts, fog,
spotlights and point lights share one half-resolution integration pass and one
depth-aware composite. There is no additional full-screen pass for local lights.

Native data read each frame: scene depth, camera axes and projection, sun
colour/direction, up to eight sun-shadow cascades, the current GPU light vectors
and, for V4 callers that use the sea plane, `gbuffer_ocean`. Light positions use
the native graphics-relative coordinate system. A small GPU data texture carries
matrices and local-light records through immutable command-list constants.
Borrowed resources and caller bindings are restored before native rendering
resumes. Since AnyAPI 0.40.2 the HDR target is never copied: the composite is a
fixed-function blend, `target.rgb = light + target.rgb × transmittance`, and it
never writes the target's alpha. Native memory is read in process
(`scene_frame::read`), never with ReadProcessMemory.

Underwater, night vision and thermal views are inactive by design: nothing is
drawn, status reason is 7 and nothing is logged as a failure. Underground views
are the same, except for a V4 owner with `ANY_LAMP_UNDERGROUND`: there the pass
draws lamp beams only (no sun shafts, no fog, no daylight wash-out).

## Settings

The mod adds these controls to the native Graphics tab, marked Modded:

- Volumetric fog: Off, Low, Medium, High, Ultra.
- Sun shafts and Local light beams: independent strength tiers.
- Sun shaft intensity and Local beam intensity: independent 0–4 multipliers.
- Beam focus: Soft, Balanced, Focused, Strong, shared by sun and local scattering.
- Volumetric quality: Low, Medium, High, Ultra; 16, 24, 32 or 48 march samples.
- Local light budget: 2, 4, 6 or 8 influential nearby lights per frame.
- Fog height falloff: concentrates fog near sea level; Off preserves density on hills.

Apply commits changes; Cancel discards drafts; Reset stages defaults. Settings
persist in AnyGraphics/settings.tsv. Previously saved settings are retained until a preset is selected. Selecting a
preset stages all its lighting choices together:

| Preset | Volumetric fog | Sun / local beams | Samples | Local lights | Sun / local intensity | Focus | Haze between beams | Beam reach |
|---|---|---|---|---|---|---|---|---|
| Off | Game settings | Off | No pass | None | Bypassed | Bypassed | Bypassed | Bypassed |
| Performance | Off | Off | No pass | None | Bypassed | Balanced | Light | Near |
| Low | Off | Low | 16 | 2 | 1 / 1 | Balanced | Light | Near |
| Medium | Low | Medium | 24 | 4 | 1 / 1 | Balanced | Light | Medium |
| High | Low | Medium / High | 32 | 6 | 1 / 1 | Balanced | Light | Medium |
| Ultra | Medium | High | 48 | 8 | 1 / 1.1 | Focused | Light | Far |
| Cinematic | Medium | High / Ultra | 48 | 8 | 1.3 / 1.3 | Focused | Natural | Far |

All presets turn on Follow sun height and Smooth beams and fog (AnyAPI 0.36.0
or newer). Height falloff stays Off in every preset so hills do not lose the
effect. Since AnyGraphics 0.30.0 no preset goes above Medium volumetric fog;
stronger fog stays available individually. These are visual/performance starting points, not measured FPS guarantees.

Sun and local beams use a fixed clear-air scattering density, independent of the
Volumetric fog tier and Fog height falloff. With Volumetric fog Off, beams add
light without globally dimming the scene. With fog enabled, fog alone controls
extinction and ambient haze; it also attenuates beams through dense air. Native
base fog remains a separate game setting. Local beams can operate with fog and
sun shafts Off. Headlights, torches and other
sources participate when the game includes them in its current point/spot light
vectors. Selection prioritizes colour intensity, radius and camera distance.
Spotlights respect their native cone and radius. Up to four matching native
spotlight shadow maps provide occlusion. The spotlight's own shadow index (the
game stores it in the light record) names the camera; one matrix compare confirms
it, otherwise every spot camera is compared as before. Point lights without a
native shadow map and unmatched spotlights remain unshadowed; new shadow maps are
not generated. Weather lightning (a point light of 4096 m radius or more) never
takes a lamp slot. Lamp colour is clamped to 10 000 per channel.

## API contract

Query `anyapi.scene_lighting` through the service registry:

| Version | Header | Policy |
|---|---|---|
| 1 | [anyapi_scene_lighting_v1.h](../sdk/include/anyapi_scene_lighting_v1.h) | Fog and sun scattering; local lights disabled |
| 2 | [anyapi_scene_lighting_v2.h](../sdk/include/anyapi_scene_lighting_v2.h) | V1 scene policy plus local strength and light budget |
| 3 | [anyapi_scene_lighting_v3.h](../sdk/include/anyapi_scene_lighting_v3.h) | V2 plus beam clarity, beam reach, sun-height response and temporal smoothing (0.36.0) |
| 4 | [anyapi_scene_lighting_v4.h](../sdk/include/anyapi_scene_lighting_v4.h) | V3 plus lamp tracking, underground lamps, weather response, sea plane, lightning glow, AMD path, and status v2 (0.40.2) |

V2 preserves the V1 ABI. Local strength is finite, 0–15; budget is 1–8. Shaft
radiance is 0–15; the menu reaches 12 through its tier and multiplier. Only an
active mod can own an enabled policy. Missing/inactive owners disable it. A
second active owner cannot replace an enabled policy. Query status to distinguish
availability from a successfully recorded frame.

Native capture is gated to the supported game build and checks array/ring
layouts, dimensions, formats, projection and finite data. Failed contracts bypass
the effect and log a reason. The service exposes copied policy and status,
not raw native resource pointers.

## Beam shaping and temporal smoothing (V3, AnyAPI 0.36.0)

Earlier sun shafts scattered sunlight from every lit point along each ray, out
to 500 m, without dimming the scene behind. In open terrain every ray is lit end
to end, so the result was a uniform white veil that read as fog. A strong beam
focus multiplied it again looking toward the sun (up to about 19× at Strong).
V3 keeps that behaviour available (clarity 0, reach 0) and adds:

- **clarity** 0–1: the integrator tracks how much of each ray is in shadow.
  Rays that cross shadow edges (gaps in trees, windows, terrain) keep full
  beams; rays that are lit end to end are reduced to `1 - clarity` of their
  glow. With clarity above 0 the phase peak is capped at 4×.
- **beam_reach** metres: clear-air beam scattering fades as `exp(-d/reach)`, so
  distant air no longer piles up into haze. 0 is unlimited, otherwise 10–2000.
- **sun_response**: beams are full at sunrise and sunset, about a third at
  noon, off below the horizon, and warm-tinted while the sun is low.
- **temporal**: ray samples are jittered per pixel and per frame
  (interleaved gradient noise), then blended at half resolution with history
  reprojected through scene depth and the previous camera, clamped to the
  current 3×3 neighbourhood. History weight is 0.85–0.92 by quality. History
  resets after a pause, a resize or a jump of more than 50 m. The graphics
  origin offset is applied, so history survives origin shifts.

V1 and V2 callers get zeros for all four, which is the original look. Per-frame
values travel in rows 20–21 of the existing matrix texture, so the root
constants and the V1/V2 ABI are unchanged.

AnyGraphics 0.30.0 exposes these as **Haze between beams** (Clear / Light /
Natural / Classic), **Beam reach** (Near 60 m / Medium 120 m / Far 250 m /
Unlimited), **Follow sun height** and **Smooth beams and fog**.

## V4 (AnyAPI 0.40.2)

`AnySceneLightingV4 {struct_size, version=4, set, status, status_v2}`. `set` takes
`AnySceneLightingParametersV4`; `shaped` is checked exactly as V3, but its
`lighting.local_budget` is ignored because `lamp_budget` replaces it. A V1, V2 or
V3 `set` resets every V4 extra (lamp_model 0, flags 0, atmosphere_flags 0,
amd_path 0, weather_response 0), so older callers keep the 0.40.1 output.

| Field | Accepted | Meaning in 0.40.2 |
|---|---|---|
| `lamp_model` | 0 | Marched lamps (the V3 model). Above `status_v2.lamp_model_max` is rejected |
| `lamp_quality`, `lamp_shape` | 0–3 | Validated and stored; no effect yet |
| `lamp_budget` | 1–16 | Lamps per frame, clamped to `lamp_budget_max` (8) |
| `lamp_shadows` | 0–2 | 0 off; 1 auto and 2 on both use native spot shadow maps where supported (never on AMD) |
| `weather_response` | 0–1 | Lamp air thickens with the game's own fog and with rain or snow, by up to 4× |
| `ambient_washout` | 0–1 | Validated and stored; no effect yet |
| `flags` | `ANY_LAMP_*` | `UNDERGROUND`: lamps-only underground. `TRACKING`: stable lamp selection. `FLASHES`: lightning may glow (with `ANY_ATMOS_FLASH_GLOW`). `OCEAN`: lamp light stops at the sea surface |
| `native_halos` | 0 | Must be 0 |
| `amd_path` | 0–2 | AMD only: 0 auto (= 1); 1 constant-buffer lamps with per-count shader builds; 2 the same with a uniform branch per lamp (experiment) |
| `atmosphere_flags` | `SEA_PLANE`, `WEATHER_DENSITY`, `PHASE_CAP`, `FLASH_GLOW` | Other bits are rejected (`status_v2.atmosphere_flags_supported` = 120) |
| `sky_inscatter` | 0–2 | Validated and stored; no effect yet |
| `reserved` | 0 | Must be 0 |

- **Tracking** (`ANY_LAMP_TRACKING`). Lights keep an identity across frames
  (position within max(1.5 m, 10% of the radius) plus 60 m/s, the same colour
  direction, spots the same aim). A selected light scores ×1.5, a newcomer ÷1.2
  while the budget is full, and a point light younger than 3 frames ×0.25, so a
  muzzle flash does not evict a headlight. Scores use a 0.3 s colour average. A
  light that enters the budget fades in over 0.2 s; one that leaves it fades out
  in a spare slot while fewer than 8 are drawn (at a budget of 8 it stops at
  once). A light the game removes is cut. A track survives 0.75 s without its
  light. A pause over 0.25 s snaps every fade; over 1 s resets the tracks.
- **Sea plane** (`ANY_ATMOS_SEA_PLANE`). The ocean writes no depth, so without it
  rays over the sea run to the sea floor or the sky. While the game reports an
  ocean (`m_is_ocean`) and `gbuffer_ocean` is usable, rays on ocean pixels end at
  world height 0. `ANY_LAMP_OCEAN` alone only stops lamp light there.
- **Weather density** (`ANY_ATMOS_WEATHER_DENSITY`). Fog density follows the
  game's own fog (`fog_ratio`, 0.5–4×), capped at 0.1.
- **Phase cap** (`ANY_ATMOS_PHASE_CAP`). The sun's forward peak is capped at 4×
  also without clarity.
- **Lightning glow** (`ANY_ATMOS_FLASH_GLOW` with `ANY_LAMP_FLASHES`). During a
  flash, fogged air adds `flash colour × falloff × 0.5 × (1 − transmittance)`.

`status_v2(AnySceneLightingStatusV2*)` fills the V1 status in `base` and adds,
for the last frame: lamps drawn and shadowed, candidate point and spot lights,
game spot shadow maps, whether lamp shadows, the AMD path (1 or 2) and the
underground mode were in use, and the atmosphere flags in effect. Totals:
lightning flashes seen, `raw29_hits`/`raw29_misses` (spot shadow index confirmed
or not) and `ring_waits`. `bypass_reason` is the last non-zero bypass reason;
`capture_us` the smoothed CPU time of the native capture.

Status reason codes: 0 drawn; 2 command bindings or target state; 3 no queue or
device; 4 GPU resource contract; 5 GPU record failed; 7 inactive view (quiet);
8 nothing to draw this frame (quiet: no fog, sun or lamp light, so the frame is
skipped and the image is the same); 100 + stage for a native capture contract.
`anyapi.scene_timing` v2 reports the pass as `ANY_PASS_VOLUMETRIC`: GPU ms when
drawn, 0 when skipped, −1 when off or bypassed.

## Validation and acceptance

The automated suite contains 39 checks, including 54 D3D12 lighting cases.
0.40.2 adds: the AMD constant-buffer path at 0–8 lamps (the per-count builds are
bit-identical to the 8-lamp build on WARP), two temporal frames, the descriptor
ring without a queue idle, the sea plane, lightning glow, alpha preservation of the
blend, and that a skipped empty frame leaves the next frame bit-identical. The
contract test covers native read counts, the 0.40.1 lamp order, the spot shadow
index, view flags, `gbuffer_ocean`, the tracker, and V4 validation and reset.
Coverage includes standard/reversed-depth sun shadows, native R16 shadows,
farther cascades, measured native sun colours, intensity scaling, point-light
scattering, spotlight cones and shadow blocking, fog depth, zero-density identity,
resizing, alpha preservation and a crisp HUD rendered afterward. Native fixtures
cover wrapped light vectors, light prioritization and matching spotlight cameras.
Plugin tests cover V1 fallback, V2 local-only operation and Apply/Cancel/bypass.

The user confirmed visible volumetric fog and calibrated sun shafts in a world.
The user also confirmed the local-light build looks correct. The preset update was user-approved for publication. Native FPS measurements
remain pending. Synthetic GPU checks do not establish those outcomes.

A one-shot 32×24 offscreen probe logs sun shadow coverage/lit fractions through
`SHAFT_PROBE`. Readback is fenced after native command submission and polled
without waiting; it is never composited. Frame logs include local light and
local shadow counts to assist world validation. Every 600 frames `LAMP_DIAG`
logs candidate, flash, rejected, drawn and shadowed lamp counts, the shadow-index
hits and misses, tracks, game spot maps, capture time, lamp path, ring waits and
queue idles.

## Limits

AMD GPUs: local lamp beams turned the whole frame into flat magenta, green and yellow on a Radeon RX 6800.
AnyAPI 0.39.1 switched them off there. From 0.39.2 AMD GPUs use their own lamp shader, `IntegrateConstants`: the
lights come from a constant buffer read in an unrolled loop instead of a texture `Load` inside a dynamic loop, and
lamps cast no shadow maps. That fixed the RX 6800. Other GPUs keep the original `Integrate` shader, byte for byte.
Since 0.40.2 the AMD shader is built for 0, 2, 4 and 8 lamps and each frame uses the smallest build that holds its
lamps, with the same result. V4 `amd_path` 2 is an experiment with a uniform branch per lamp instead.

Descriptor tables live in a 4-entry ring fenced by the frame's own submission, so a change of game targets no longer
waits for the whole GPU queue; only a resize, a device change or an AMD path change does.

The pass releases its GPU objects 120 frames after it is switched off (one queue drain on that transition) and
re-creates them when it is switched on again. A failed GPU build is retried after 1, 2, 4 ... 60 s, not every frame.


Temporal smoothing covers the volumetric pass only. There is no new point-light shadow generation, cascade
cross-fading, volumetric cloud replacement or material/water overhaul. Since AnyAPI 0.40.1
beams fade out over the edge of the last sun cascade, so air beyond it adds no beam light. More lights and march samples increase
GPU cost. This candidate is a lighting extension, not a complete shader pack.
