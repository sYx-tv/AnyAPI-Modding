#pragma once
#include "anyapi_scene_timing_v1.h"
enum AnyScenePassV2 : uint32_t {ANY_PASS_AO=0,ANY_PASS_CONTACT=1,ANY_PASS_GI=2,ANY_PASS_WATER=3,ANY_PASS_VOLUMETRIC=4,ANY_PASS_SKY=5,
                                ANY_PASS_LENS=6,ANY_PASS_FINISH=7,ANY_PASS_AA=8,ANY_PASS_OVERLAY=9,ANY_PASS_COUNT=16};
struct AnySceneTimingV2 {
 uint32_t struct_size=sizeof(AnySceneTimingV2),version=2;
 AnySceneTimingV1 v1;                    // same values as copy()
 uint64_t frame_index=0;                 // presented frames
 float frame_ms_p50=-1,frame_ms_p95=-1,frame_ms_p99=-1; // present to present, last 2 s
 float present_block_ms=-1;              // p50 time inside the game's Present call
 uint32_t sync_interval=0;float refresh_hz=0; // 0 = unknown
 float pass_ms[16]{};                    // GPU ms per AnyScenePassV2 (OVERLAY: CPU ms p50); -1 = off or not measured
 float overlay_ms_p95=-1;
 float anyapi_cpu_ms=-1;                 // p50 main-thread ms inside AnyAPI scene hooks
 uint32_t native_reads=0;                // ReadProcessMemory calls in the last frame
 float gpu_scene_ms=-1,gpu_busy=-1;      // reserved, -1 in 0.40.2
 uint32_t flags=0;                       // 1 window valid, 2 settling (resize/device change within 2 s), 8 overlay down
 uint32_t reserved[4]={};
};
struct AnySceneTimingServiceV2 {uint32_t struct_size,version;bool(*copy)(AnySceneTimingV1*);bool(*copy_v2)(AnySceneTimingV2*);};
