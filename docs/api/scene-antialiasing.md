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
either axis. TAA/MSAA/DLSS are not implemented. This contract does not expose
native resource pointers or allow replacing arbitrary rendering functions.

See [Implementation and validation](../development/scene-antialiasing.md).
