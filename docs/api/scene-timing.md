# Scene timing

AnyAPI 0.36.0. Query `anyapi.scene_timing`, version 1, using
`AnyServicesV1::query`. The service table is declared in
[`anyapi_scene_timing_v1.h`](../../sdk/include/anyapi_scene_timing_v1.h).

`copy` fills `AnySceneTimingV1` (matching `struct_size` and `version` 1):

| Field | Meaning |
| --- | --- |
| `lighting_ms` | Smoothed GPU time of the [scene lighting](../scene-lighting.md) pass, or negative while it is off or not yet measured |
| `antialiasing_ms` | Smoothed GPU time of the [scene antialiasing](scene-antialiasing.md) pass (SMAA, TAA and sharpening together), or negative |
| `frame_ms` | Smoothed CPU interval between scene frames; `1000 / frame_ms` is the frame rate |

GPU times come from D3D12 timestamp queries recorded around each pass on the
native command list. Results are read back seven frames later from an 8-slot
ring, so there is no fence wait and no stall, and values lag by a few frames.
They are estimates for a settings readout, not a profiler: they exclude the
game's own rendering and include the copies each pass makes.

The call is cheap and may be made every frame, for example from a menu `draw`
callback. AnyGraphics 0.30.0 shows it under **Show GPU cost**.
