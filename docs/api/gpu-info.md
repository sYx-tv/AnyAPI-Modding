# GPU info

AnyAPI 0.39.1 (lamp beams on AMD from 0.39.2). Query `anyapi.gpu_info`, version 1, using `AnyServicesV1::query`.
The service table is declared in
[`anyapi_gpu_info_v1.h`](../../sdk/include/anyapi_gpu_info_v1.h).

`copy` fills `AnyGpuInfoV1` (matching `struct_size` and `version` 1):

| Field | Meaning |
| --- | --- |
| `known` | 1 once AnyAPI has seen a scene frame and read the adapter the game renders with; 0 before |
| `vendor_id` | PCI vendor of that adapter: `0x10DE` NVIDIA, `0x1002` AMD, `0x8086` Intel |
| `device_id` | PCI device id of that adapter |
| `lamp_beams_supported` | 0 where AnyAPI switches local lamp beams off; 0.39.1 did that on AMD GPUs, 0.39.2 reports 1 everywhere |

The adapter comes from the game's own D3D12 device, so a laptop with two GPUs
reports the one the game uses. AnyGraphics 0.31.2 hides its lamp beam settings
when `lamp_beams_supported` is 0. See [scene lighting](../scene-lighting.md#limits)
for the AMD lamp path.
