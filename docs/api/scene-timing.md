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

## Version 2 (AnyAPI 0.40.2)

Query `anyapi.scene_timing`, version 2. The table is `AnySceneTimingServiceV2`
in [`anyapi_scene_timing_v2.h`](../../sdk/include/anyapi_scene_timing_v2.h):
`copy` is the version 1 call above, and `copy_v2` fills `AnySceneTimingV2`
(matching `struct_size` and `version` 2). Pass indices are `AnyScenePassV2`
(`ANY_PASS_AO` 0 … `ANY_PASS_AA` 8, `ANY_PASS_OVERLAY` 9).

| Field | Meaning |
| --- | --- |
| `v1` | The same values as `copy()` |
| `frame_index` | Frames presented on the game's swapchain |
| `frame_ms_p50`, `frame_ms_p95`, `frame_ms_p99` | Present-to-present ms over the last 2 s; gaps over 2 s (loading, minimised) are skipped; -1 with no presents in the window |
| `present_block_ms` | p50 ms spent inside the game's own `Present` (vsync or queue wait) |
| `sync_interval` | The last `Present` SyncInterval |
| `refresh_hz` | Refresh rate of the output holding the window, polled every 2 s and on display changes; 0 unknown |
| `pass_ms[16]` | GPU ms per `AnyScenePassV2` from each pass's timer, -1 off or not measured; `pass_ms[ANY_PASS_OVERLAY]` is the p50 CPU ms of AnyAPI's overlay composition (mod canvases, gpu_draw, post-process and the Direct2D flush) |
| `overlay_ms_p95` | p95 of the same overlay CPU time |
| `anyapi_cpu_ms` | p50 main-thread ms per frame inside AnyAPI scene hooks; -1 until measured |
| `native_reads` | `ReadProcessMemory` calls during the last frame (0 is the target in gameplay) |
| `gpu_scene_ms`, `gpu_busy` | Reserved, -1 in 0.40.2 |
| `flags` | 1 window valid (8 or more presents in the last 2 s); 2 settling (resize, new swapchain or GPU change in the last 2 s, ignore the percentiles); 8 overlay down (mod canvases are not drawn; scene passes and mod callbacks still run) |

`copy_v2` recomputes at most 10 times a second and is otherwise a cached copy,
so it is safe to call every frame. AnyAPI logs `SCENE_TIMING_V2` every 600
presents and `PLUGIN_RENDER overlay_ms` (p50, p95 and the collect, record,
EndDraw and flush split) every 600 overlay frames.
