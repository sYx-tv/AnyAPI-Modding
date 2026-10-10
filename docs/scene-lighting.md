# Scene lighting

AnyAPI and AnyGraphics 0.29.0 add HDR fog, sun shafts and local-light scattering
for Anymaker 0.1.24 / Steam build 25826614.

## Render integration

The pass records immediately after `renderer._record_commands_additive`, before
native bloom, tone mapping, scene antialiasing and the HUD. Sun shafts, fog,
spotlights and point lights share one half-resolution integration pass and one
depth-aware composite. There is no additional full-screen pass for local lights.

Copied native data includes HDR colour, scene depth, camera axes and projection,
sun colour/direction, up to eight sun-shadow cascades, and the current GPU light
vectors. Light positions use the native graphics-relative coordinate system.
A small GPU data texture carries matrices and local-light records through
immutable command-list constants. Borrowed resources and caller bindings are
restored before native rendering resumes. Special vision, underwater and
underground views bypass the effect.

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

| Preset | Volumetric fog | Sun / local beams | Samples | Local lights | Sun / local intensity | Focus |
|---|---|---|---|---|---|---|
| Off | Game settings | Off | No pass | None | Bypassed | Bypassed |
| Performance | Off | Off | No pass | None | Bypassed | Balanced |
| Low | Off | Low | 16 | 2 | 1 / 1 | Balanced |
| Medium | Low | Medium | 24 | 4 | 1 / 1 | Balanced |
| High | Medium | High | 32 | 6 | 1.3 / 1.2 | Focused |
| Ultra | High | Ultra | 48 | 8 | 1.5 / 1.3 | Focused |

Height falloff stays Off in every preset so hills do not lose the effect. Ultra
uses High fog density to preserve scene visibility; standalone Ultra fog remains
available. These are visual/performance starting points, not measured FPS guarantees.

Sun and local beams use a fixed clear-air scattering density, independent of the
Volumetric fog tier and Fog height falloff. With Volumetric fog Off, beams add
light without globally dimming the scene. With fog enabled, fog alone controls
extinction and ambient haze; it also attenuates beams through dense air. Native
base fog remains a separate game setting. Local beams can operate with fog and
sun shafts Off. Headlights, torches and other
sources participate when the game includes them in its current point/spot light
vectors. Selection prioritizes colour intensity, radius and camera distance.
Spotlights respect their native cone and radius. Up to four matching native
spotlight shadow maps provide occlusion. Point lights without a native shadow
map and unmatched spotlights remain unshadowed; new shadow maps are not generated.

## API contract

Query `anyapi.scene_lighting` through the service registry:

| Version | Header | Policy |
|---|---|---|
| 1 | [anyapi_scene_lighting_v1.h](../sdk/include/anyapi_scene_lighting_v1.h) | Fog and sun scattering; local lights disabled |
| 2 | [anyapi_scene_lighting_v2.h](../sdk/include/anyapi_scene_lighting_v2.h) | V1 scene policy plus local strength and light budget |

V2 preserves the V1 ABI. Local strength is finite, 0–15; budget is 1–8. Shaft
radiance is 0–15; the menu reaches 12 through its tier and multiplier. Only an
active mod can own an enabled policy. Missing/inactive owners disable it. A
second active owner cannot replace an enabled policy. Query status to distinguish
availability from a successfully recorded frame.

Native capture is gated to the supported game build and checks array/ring
layouts, dimensions, formats, projection and finite data. Failed contracts bypass
the effect and log a reason. The service exposes copied policy and status,
not raw native resource pointers.

## Validation and acceptance

The automated suite contains 39 checks, including 54 D3D12 lighting cases.
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
local shadow counts to assist world validation.

## Limits

There is no temporal accumulation, new point-light shadow generation, cascade
cross-fading, volumetric cloud replacement or material/water overhaul. Samples
outside all sun cascades remain unshadowed. More lights and march samples increase
GPU cost. This candidate is a lighting extension, not a complete shader pack.
