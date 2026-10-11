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
`pipeline_postprocess` on Anymaker 0.1.24:

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
| `enabled` | 0/1 | Run the pass. While enabled, the game's own bloom is turned off for the frame because this pass replaces it |
| `tonemap` | 0-2 | 0 Game (the game's ACES curve), 1 Filmic (AgX-style, keeps colour in bright light), 2 Clean (Khronos PBR Neutral) |
| `look` | 0-5 | None, Warm, Cool, Teal & orange, Moody, Vivid |
| `look_strength` | 0-1 | How strongly the look is applied |
| `exposure` | -3..3 | Manual stops before the curve |
| `eye_adaptation` | 0/1 | Adds up to ±1.5 stops after big brightness changes, then settles back (see below) |
| `adaptation_seconds` | 0.2-10 | Time constant of the fast adaptation |
| `bloom` | 0-1 | Threshold-free 6-level bloom (13-tap downsample with a Karis average on the first level, tent upsample) |
| `glare` | 0-2 | Halo, core and six faint streaks around the sun while it is on screen |
| `vignette` | 0-1 | Darkens the corners |

Only one active mod owns the policy, as with the other scene services.

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

## Status

`status` fills `AnySceneEffectsStatusV1`: `available`, `ready`, frame and
rejected-frame counts, `gpu_ms` (smoothed GPU time of the pass),
`adapted_exposure` (stops currently added by eye adaptation),
`scene_luminance` (measured average) and `sun_visibility`. The values are read
back seven frames late without a fence.

## Limits

The pass is bypassed, with a logged reason, in special views (scope, vision
modes), when the native HDR target, depth or command bindings fail their
checks, or when the pass cannot create its resources. The game's bloom stays
off while the policy is enabled. Effects cover the scene only: water and glass
are finished like everything else, and no material data is used.
