#pragma once
#include "anyapi_gpu_info_v1.h"
struct AnyGpuInfoV2 {uint32_t struct_size=sizeof(AnyGpuInfoV2),version=2;AnyGpuInfoV1 v1;
 uint32_t uma=0,dedicated_mb=0,shared_mb=0,budget_mb=0,usage_mb=0; // budget/usage: QueryVideoMemoryInfo(LOCAL) every 2 s; 0 = unknown
 uint32_t width=0,height=0;              // the game's HDR scene target
 uint32_t lamp_shadows_supported=1;      // 0 where lamp beams run without spot shadow maps (AMD in 0.40.2)
 char adapter_name[128]={};              // UTF-8
 uint32_t reserved[4]={};};
struct AnyGpuInfoServiceV2 {uint32_t struct_size,version;bool(*copy)(AnyGpuInfoV1*);bool(*copy_v2)(AnyGpuInfoV2*);};
