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

The candidate targets Anymaker 0.1.23, Steam build 25755694, and the framework's
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
editing are exposed by this contract. Use [post-processing](post-processing.md)
for optional finished-image shaders; they remain a separate stage.
