# Scene antialiasing

AnyAPI 0.28.0. Query `anyapi.scene_antialiasing`, version 1, using
`AnyServicesV1::query`. The immutable service table is declared in
`anyapi_scene_antialiasing_v1.h`.

`set` accepts `AnySceneAntialiasingParametersV1` with the matching struct size
and version, `enabled` 0 or 1, `method` 1 (SMAA 1x) or 2 (Enhanced SMAA), and `quality` 0–3
(Low, Medium, High, Ultra). Call during a mod callback with a valid owner.
Only one active mod can own enabled scene AA. Faulted/inactive owners bypass.
Enhanced SMAA adds contrast-aware spatial blending at diagonal edges. It can
soften fine detail and does not use temporal history. Other methods, quality values and unsupported ABI versions are rejected.

`status` copies availability, GPU readiness, dimensions, rendered frame count,
rejected frame count and the latest initialization HRESULT. Availability means
the exact native scene boundary resolved; readiness means a GPU pass has been
successfully recorded. Neither alone proves a particular visual improvement.
A disabled policy records no AA work. Native rendering is preserved when
binding prerequisites are unavailable.

SMAA runs on the native D3D12 command list right after the game's postprocess
(`pipeline_postprocess.cmd_render`), before the editor, placement and
inspection overlays and before native HUD/menu drawing. Where that inner
boundary is unavailable it runs after the whole postprocess, as before 0.40.2,
and TAA pauses while an editor overlay is on screen. The framework restores
graphics bindings and disables native FXAA while the replacement is ready.
Colour grading and finished-screen filters are not part of this service.

Supported target formats are RGBA8/BGRA8 UNORM, single-sample, up to 8192 on
either axis. MSAA and DLSS are not implemented; TAA is available through version 2 below. This contract does not expose
native resource pointers or allow replacing arbitrary rendering functions.

## Version 2: TAA and sharpening (AnyAPI 0.36.0)

Query `anyapi.scene_antialiasing` version 2 for `AnySceneAntialiasingParametersV2`
in [`anyapi_scene_antialiasing_v2.h`](../../sdk/include/anyapi_scene_antialiasing_v2.h).
It adds `method` 3 (**TAA**) and 4 (**TAA + jitter**) and `sharpening` 0–1.
Status is the V1 structure. V1 callers keep exactly the behaviour above, and a
V1 `set` resets sharpening to 0. Ownership rules are shared between versions.

TAA runs SMAA first, then blends the result with a history image (RGBA16F,
colour plus view distance). History is reprojected through scene depth and the
current and previous camera. Each pixel tries two candidates: the reprojected
world position, accepted when the stored distance matches; then the same pixel,
accepted when its distance matches, which keeps camera-locked content such as
your own vehicle stable. Otherwise history is ignored for that pixel. Accepted
history is clipped to the current 3×3 neighbourhood in YCoCg and blended with
luminance weighting. Quality 0–3 also sets the history weight (0.85–0.92);
version 3's `history_bias` adds one level (up to 0.94).
History resets after a pause over 250 ms, a resize, or a jump over 50 m, and
while a scope, binocular or IR view is shown. Pixels that keep no history show
the current frame moved back by the camera jitter, so rejected edges do not
vibrate. The ocean writes no depth, so ocean pixels reproject through the sea
plane under the camera instead of the sea floor.
Moving objects are not motion-vectored. Fast motion relative to the camera
falls back to the current frame on those pixels rather than trailing.

**TAA + jitter** also offsets the main camera's projection and view-projection
by a Halton(2,3) subpixel pattern in the transient scene data before the game
builds its constants. That adds real subpixel samples, so edges converge to a
supersampled look. The offset is applied only when the projection is
perspective and the view-projection's clip-w row matches the camera's forward
axis. A stale copy from the previous frame is detected and never accumulates.
Night-vision and thermal views, scope views and frames with an editor,
placement or inspection overlay in the last 30 frames are not jittered.
Underground and underwater views keep TAA and jitter. If the camera contract
fails, TAA continues without jitter and the log records `SCENE_TAA_JITTER skipped`.

Sharpening is contrast-adaptive (after AMD FidelityFX CAS) in the final copy to
the scene target, before the HUD. Flat areas and hard edges are unchanged; the
strength is limited near clipping. With sharpening or TAA, SMAA resolves into an
offscreen image and a final pass writes the scene target (with TAA and no
sharpening, the TAA pass writes it directly). That final write adds a static
dither of about one 8-bit step, spared at black and white, against banding;
version 3 can turn it off. Plain SMAA is not dithered.

If depth or camera data are unavailable, TAA falls back to SMAA for that frame
and logs `SCENE_TAA bypass=<reason>`. Night-vision, thermal, scope and editor
overlay frames fall back quietly (`SCENE_TAA inactive=<reason>`). If the TAA
offscreen images cannot be created (out of video memory), TAA falls back to SMAA
without jitter, logs `SCENE_TAA bypass=targets` and retries after 1, 2, 4 .. 60
seconds. GPU cost is
available through [scene timing](scene-timing.md) (`pass_ms[ANY_PASS_AA]`) and
the version 3 status.

See [Implementation and validation](../development/scene-antialiasing.md).

## Version 3: TAA options and status (AnyAPI 0.40.2)

Query `anyapi.scene_antialiasing` version 3 for `AnySceneAntialiasingV3` in
[`anyapi_scene_antialiasing_v3.h`](../../sdk/include/anyapi_scene_antialiasing_v3.h).
`set` takes `AnySceneAntialiasingParametersV3`: a version 2 `base` (validated as
above) plus:

| Field | Values | Default | Effect |
|---|---|---|---|
| `history_bias` | 0–1 | 0 | One more level of TAA history weight (0.94 at Ultra). |
| `taa_flags` | bits of `ANY_TAA_*` | `CATMULL_ROM \| STILL_V2` | `ANY_TAA_CATMULL_ROM` (1): sharper history resampling in motion. `ANY_TAA_STILL_V2` (2): the camera-locked candidate wins only on a tight match that is closer to the current image, up to 80 m on vehicles, tracks, vegetation and clothing, never at screen-entry edges. `ANY_TAA_THIN_RESCUE` (4): thin bright moving features (rain, sparks, tracers) keep at least half their brightness. |
| `dither` | 0–1 | 1 | Output dither (see above). |
| `flags` | bits of `ANY_AA_*` | 0 | Testing switches that turn off a 0.40.2 change made for every version. `ANY_AA_AFTER_OVERLAYS` (1): record after the whole postprocess as in 0.40.1, so TAA pauses while an editor overlay shows. `ANY_AA_OCEAN_DEPTH` (2): TAA reprojects ocean pixels with the depth buffer as in 0.40.1 instead of the sea plane. Other bits are rejected. |
| `reserved` | 0 | 0 | Must be zero. |

Version 1 and 2 setters store `history_bias` 0, `taa_flags` 0 (the 0.36 TAA
candidate rules), `dither` 1 and `flags` 0. Re-centred rejected pixels, scope resets, the
sea-plane ocean distance, the overlay placement and the dither apply to every
version. Ownership is shared across versions.

`status` is the version 1 status. `status_v2` fills
`AnySceneAntialiasingStatusV2`: the version 1 fields in `base`, then

| Field | Meaning |
|---|---|
| `temporal_active` | TAA wrote its history on the last recorded frame. |
| `jitter_active` | This frame's camera is jittered. |
| `last_fallback_reason` | `ANY_AA_FALLBACK_*` of the last recorded frame: 0 none, 1 special vision (night vision, thermal), 2 scope, 3 depth, 4 camera, 5 interface (depth is not a resource of the game's device, or TAA's offscreen images could not be created), 6 editor overlay (TAA paused because AA ran after the overlay), 7 history reset (TAA ran without history), 8 bindings (AA bypassed). 0 for SMAA methods. |
| `editor_overlay` | 0 no overlay this frame; 1 overlay present, AA ran before it; 2 overlay present, AA ran after it. |
| `jittered_frames`, `jitter_skipped` | Frames jittered, and TAA+jitter frames without jitter. |
| `gpu_ms` | Smoothed GPU milliseconds of the AA pass; -1 while off or not measured. |
