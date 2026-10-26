# GPU info

AnyAPI 0.39.1 (lamp beams on AMD from 0.39.2). Query `anyapi.gpu_info`, version 1, using `AnyServicesV1::query`.
The service table is declared in
[`anyapi_gpu_info_v1.h`](../../sdk/include/anyapi_gpu_info_v1.h).

`copy` fills `AnyGpuInfoV1` (matching `struct_size` and `version` 1):

| Field | Meaning |
| --- | --- |
| `known` | 1 once AnyAPI has identified the adapter the game renders with. From 0.40.2 that happens at the game's first GPU submission (before 0.40.2, after the first scene frame); 0 before |
| `vendor_id` | PCI vendor of that adapter: `0x10DE` NVIDIA, `0x1002` AMD, `0x8086` Intel |
| `device_id` | PCI device id of that adapter |
| `lamp_beams_supported` | 0 where AnyAPI switches local lamp beams off; 0.39.1 did that on AMD GPUs, 0.39.2 reports 1 everywhere |

The adapter comes from the game's own D3D12 device, so a laptop with two GPUs
reports the one the game uses. AnyGraphics 0.31.2 hides its lamp beam settings
when `lamp_beams_supported` is 0. See [scene lighting](../scene-lighting.md#limits)
for the AMD lamp path.

## Version 2 (AnyAPI 0.40.2)

Query `anyapi.gpu_info`, version 2. The table is `AnyGpuInfoServiceV2` in
[`anyapi_gpu_info_v2.h`](../../sdk/include/anyapi_gpu_info_v2.h): `copy` is the
version 1 call above, and `copy_v2` fills `AnyGpuInfoV2` (matching
`struct_size` and `version` 2).

| Field | Meaning |
| --- | --- |
| `v1` | The same values as `copy()` |
| `uma` | 1 on a unified-memory adapter (D3D12 architecture query) |
| `dedicated_mb`, `shared_mb` | Dedicated video memory and shared system memory from the adapter description |
| `budget_mb`, `usage_mb` | `QueryVideoMemoryInfo`, local segment, polled every 2 s while `copy_v2` has been called in the last 5 s (the first call after a pause schedules a poll on the next present, so it can return 0 or an older value); on integrated GPUs and the Steam Deck this is the shared system-memory budget; 0 unknown |
| `width`, `height` | The game's HDR scene target; 0 until a scene pass has run |
| `lamp_shadows_supported` | 0 on AMD in 0.40.2: lamp beams run without spot shadow maps there |
| `adapter_name` | Adapter description, UTF-8 |

The adapter is the one that owns the game's swapchain, so a laptop with two
GPUs reports the one the game uses. AnyAPI logs `GPU_IDENTITY` once per device.
