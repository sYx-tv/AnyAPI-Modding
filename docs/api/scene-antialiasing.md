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

SMAA runs after native scene composition and before native HUD/menu drawing,
on the native D3D12 command list. The framework restores graphics bindings and
disables native FXAA while the replacement is ready. Colour grading, sharpening
and finished-screen filters are not part of this service.

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
luminance weighting. Quality 0–3 also sets the history weight (0.85–0.92).
History resets after a pause over 250 ms, a resize, or a jump over 50 m.
Moving objects are not motion-vectored. Fast motion relative to the camera
falls back to the current frame on those pixels rather than trailing.

**TAA + jitter** also offsets the main camera's projection and view-projection
by a Halton(2,3) subpixel pattern in the transient scene data before the game
builds its constants. That adds real subpixel samples, so edges converge to a
supersampled look. The offset is applied only when the projection is
perspective and the view-projection's clip-w row matches the camera's forward
axis. A stale copy from the previous frame is detected and never accumulates.
Preview scenes are skipped. If the camera contract fails, TAA continues
without jitter and the log records `SCENE_TAA_JITTER skipped`.

Sharpening is contrast-adaptive (after AMD FidelityFX CAS) in the final copy to
the scene target, before the HUD. Flat areas and hard edges are unchanged; the
strength is limited near clipping. With sharpening or TAA, SMAA resolves into an
offscreen image and a final pass writes the scene target.

If depth or camera data are unavailable at the pre-HUD boundary, TAA falls back
to SMAA for that frame and logs `SCENE_TAA bypass`. GPU cost is available through
[scene timing](scene-timing.md).

See [Implementation and validation](../development/scene-antialiasing.md).
