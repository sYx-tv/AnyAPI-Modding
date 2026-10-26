# Native scene controls

Service in AnyAPI 0.28.0: `anyapi.scene_controls`, version 1.
The exact ABI is [anyapi_scene_controls_v1.h](../../sdk/include/anyapi_scene_controls_v1.h).
Resolve it during `AnyAPI_ModReady` and validate its version, structure size and
function pointers before use. Native lighting and effect switches have passed user testing;
the additional scene-detail switches await live acceptance.

Version 2 uses [anyapi_scene_controls_v2.h](../../sdk/include/anyapi_scene_controls_v2.h).
It embeds the unchanged version 1 policy in `scene` and adds `clouds`, `grass`,
and `foliage`: 0 follows the game, 1 disables rendering, 2 enables rendering.
Both versions share one owner and submit their policy atomically. Submitting
version 1 clears the three additional overrides. Version 2 uses the same status
structure and readiness checks. These switches change rendering, not collision
or saved world objects, and restore the original flags even on exceptional exits.

Version 3 (AnyAPI 0.40.1) uses [anyapi_scene_controls_v3.h](../../sdk/include/anyapi_scene_controls_v3.h).
It embeds a version 2 policy in `detailed` and adds:

| Parameter | Meaning |
| --- | --- |
| `key_tint` | RGB tint, 0.5–1.5 per channel, for the lit side: the sun, the upper sky light and the upper view colour |
| `shade_tint` | RGB tint, 0.5–1.5 per channel, for the light that fills shadows: sky, back light and the lower sides |
| `wind_scale` | 0–4; scales the wind the game sways trees and grass with |

Each tinted light keeps its Rec.709 luminance, so tints change colour, not
brightness. The wind is scaled around `_build_scene` only and put back right
after, so physics, weather and other players never see it. Submitting version
1 or 2 resets the tints and wind to 1.

Version 4 (AnyAPI 0.40.2) uses [anyapi_scene_controls_v4.h](../../sdk/include/anyapi_scene_controls_v4.h).
It embeds a version 3 policy in `looked`; `key_tint` and `shade_tint` stay the
static tints. Submitting version 1, 2 or 3 resets every version 4 field to the
values in the first column below, which reproduce the version 3 output.

| Parameter | V1–V3 value | Meaning |
| --- | --- | --- |
| `tint_targets` | 0 | 0: version 3 targets. 1 (the header default): key on the main light and the back light (sun bounce), shade on the lower light and the sky light; the two view colours are left alone |
| `time_shaped` | 0 | 1: the key and shade tints blend `noon_*`, `golden_*`, `blue_*` and `moon_*` by the sun height of the game clock. Without a game clock the static tints apply |
| `noon_key` … `moon_shade` | 1, 1, 1 | Eight RGB palettes, 0.5–1.5 per channel; luminance is kept as in version 3 |
| `moon_mode` | 0 | 1: adds moonlight to the game's night main light, so the game's own shadows, fog, SSAO and ocean see it. 2 is reserved and rejected |
| `moonlight` | 0 | 0–0.03: luminance the night main light is raised to; the light is never lowered |
| `moon_phase_floor` | 0.35 | 0–1: the least moon phase brightness used |
| `weather_fog_keep` | 0 | 1: while a storm thickens the game's fog, a `fog` scale below 1 gives way to the game's fog. It needs the weather source |
| `shadow_split`, `shadow_steps`, `grass_far` | 0 | Reserved for later game-lever patches; must be 0 |
| `reserved` | 0 | Must be 0 |

The moonlight follows the game clock: it fades in over 0.03 of a day after the
game parks its sun at 0.8 and fades out over 0.03 of a day before 0.2. The
applied amount also moves by at most 0.03 per frame. Rain and snow lower it, by
up to 42% in the heaviest storm, only while [scene effects](scene-effects.md)
version 3 `weather` is 1, the switch that also dims the moon disc. Its direction is the game's parked night
direction, so the night shadows fall the way the game already casts them.

`status_v2` returns `AnySceneStatusV2`: the version 1 status in `base`, plus
values read from the last frames. `clock_source` is 0 without a clock, 1 for the
game clock (`t` is its cycle, 0–1) and 2 for the parked-night fallback (`t` is
−1). `sun_y` and `moon_y` are the vertical directions, `parked` is 1 while the
game holds its noon direction at night, and `moon_mode_active` and `moon_light`
report the moonlight added in the last frame. `weather_source` is 1 when the
game's rain and snow amounts were read; `rain` and `snow` are those raw amounts
(0–1), `cover` the storm cover from the smoothed rain and snow (0–0.6), `wet`
the slow wetness and `fog_ratio` the game's base fog against its default (0.5–4,
1 when unknown). `tint_targets_active` is the mapping used in the last frame (0
while no policy is active). `flash_frames` counts the lightning-flash frames
that scene lighting has detected (0 without it). Values older than 30 renderer
frames, for example on a loading screen, read as absent.

The clock and the weather are read only: this service never writes the game's
time, weather or sun. Version 4 status and moonlight are unproven in a live
world until owner testing.

Hook behaviour in 0.40.2: the effects finish replaces the game's bloom only
while it rendered within the last 15 renderer frames, so an effects bypass
brings the game's bloom back. A finish that becomes wanted (start-up, a
Gameplay only menu closing) turns the game's bloom off from its first frame. A substituted policy, used when AnyAPI
antialiasing or effects need a renderer switch but no owner policy is active,
does not apply cloud, grass or foliage overrides. With AnyAPI ambient occlusion
the game's SSAO runs only when AnyAPI did not write the occlusion that frame.
When the game's clock jumps or clouds turn on, AnyAPI asks the game to rebuild
its cloud lighting.

This service changes scene inputs and native renderer switches before the game
builds scene constants and records its render commands. It does not operate on
the finished screen image. Native bloom remains in the game's own pipeline.

| Parameter | Meaning |
| --- | --- |
| `enabled` | 0 bypasses all overrides; 1 enables the submitted policy |
| `aa` | Native FXAA: 0 game setting, 1 off, 2 on |
| `bloom`, `ssao`, `shadows`, `fog_blur` | 0 game setting, 1 off, 2 on |
| `bloom_threshold` | Native bloom threshold, 0–4 |
| `bloom_intensity` | Native bloom intensity, 0–2; applied when bloom is forced on |
| `sun`, `sky`, `ambient` | Lighting multipliers, 0–3; 1 preserves the input |
| `fog` | Base fog multiplier, 0–3; 1 preserves the input |
| `light_exposure` | Scene lighting multiplier in stops, −2–2; not an HDR exposure operator |

`set` copies the whole policy; the caller retains ownership of its structure.
It requires a valid plugin callback context. One active plugin owns the policy;
another plugin cannot replace an enabled policy from an active owner. Values
must be finite and in range. Rejection leaves the previous policy intact.
`status` returns hook readiness, renderer/scene call counters and rejected frame
counts. Service availability alone does not establish hook readiness.

Overrides are consumed on subsequent native calls. Renderer switches and custom
bloom values are scoped to the native render invocation and restored afterwards,
including exceptional exits. Lighting and fog affect only transient frame scene
data, not saved world state. An inactive owner or unavailable hooks bypasses the
policy. Providers and hook code stay loaded for the process lifetime.

## Reviewed native contracts

The candidate targets Anymaker 0.1.27, Steam build 26113663, and the framework's
exact executable/GCL hash guard. The SDK reference identifies:

- `renderer`: FXAA through fog-blur flags at `0x638`–`0x63c`, bloom threshold
  at `0x648`, intensity at `0x650`. Version 2 adds clouds at `0x63e`, grass
  at `0x63f`, and vegetation at `0x640`. The installed GCL reads these in
  `_build_cloud_particles`, `_build_scene`, and `_record_commands_gbuffer`;
  vegetation also gates shadow and rain-depth recording.
- `scene_renderer`: seven double-precision lighting vectors at `0xa90`–`0xb20`,
  base fog at `0xb60`. Main and sky light are scaled separately; the remaining
  view, ambient and back-light vectors share the ambient multiplier.
- `on_render`: relocation slot 1 after code end 32 references the shared
  `renderer.render` call cell.
- `renderer._build_renderables`: relocation slot 8 after code end 872 references
  the shared `renderer._build_scene` call cell.

Caller signatures and reviewed 32-byte target prefixes must match before atomic
call-cell attachment. The short `on_render` body is not unique: candidates are
filtered by their callee and must resolve to exactly one distinct shared cell.
Repeated wrappers pointing to the same cell are accepted; distinct valid cells
are rejected. If another owner has replaced the target, attachment is
rejected rather than overwriting its hook. No instructions are rewritten.
Readable target data and writable object bounds are checked; malformed inputs
skip overrides while preserving the native call.

Automated fixtures exercise policy validation, ownership, native switches,
exceptional restoration, lighting/fog changes and unavailable/mismatching hooks.
These fixtures do not establish live rendering order or visual acceptance.

No raw renderer pointers, resource textures, motion vectors, depth access, TAA,
DLSS, custom native shader replacement, resolution scaling or weather/time
editing are exposed by this contract; version 4 reads the clock and weather only. Use [post-processing](post-processing.md)
for optional finished-image shaders; they remain a separate stage.
