#pragma once
#include <stdint.h>
// The graphics card the game renders with (AnyAPI 0.39.1). known stays 0 until AnyAPI has seen a scene frame.
// vendor_id is the PCI vendor: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel.
// lamp_beams_supported is 0 where AnyAPI switches local lamp beams off: settings for them do nothing there.
// AnyAPI 0.39.1 did that on AMD GPUs; 0.39.2 runs lamp beams everywhere and reports 1.
struct AnyGpuInfoV1 {uint32_t struct_size=sizeof(AnyGpuInfoV1),version=1,known=0,vendor_id=0,device_id=0,lamp_beams_supported=1;};
struct AnyGpuInfoServiceV1 {uint32_t struct_size,version;bool(*copy)(AnyGpuInfoV1*);};
