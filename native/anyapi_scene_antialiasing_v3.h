#pragma once
#include "anyapi_scene_antialiasing_v2.h"
// V1/V2 setters map to: history_bias 0, taa_flags 0, dither 1. Re-centred rejected pixels (AA-7) and scope resets (AA-10) apply to all versions.
enum : uint32_t {ANY_TAA_CATMULL_ROM=1,ANY_TAA_STILL_V2=2,ANY_TAA_THIN_RESCUE=4};
enum : uint32_t {ANY_AA_FALLBACK_NONE=0,ANY_AA_FALLBACK_SPECIAL_VISION=1,ANY_AA_FALLBACK_SCOPE=2,ANY_AA_FALLBACK_DEPTH=3,
                 ANY_AA_FALLBACK_CAMERA=4,ANY_AA_FALLBACK_INTERFACE=5,ANY_AA_FALLBACK_EDITOR_OVERLAY=6,ANY_AA_FALLBACK_HISTORY_RESET=7,ANY_AA_FALLBACK_BINDINGS=8};
struct AnySceneAntialiasingParametersV3 {uint32_t struct_size=sizeof(AnySceneAntialiasingParametersV3),version=3;AnySceneAntialiasingParametersV2 base;
 uint32_t history_bias=0;               // 0..1: one more level of TAA history (Extra smooth), also at quality 3
 uint32_t taa_flags=ANY_TAA_CATMULL_ROM|ANY_TAA_STILL_V2;
 uint32_t dither=1;                     // about 1 LSB in the output pass on 8-bit swapchains
 uint32_t reserved[4]={};};
struct AnySceneAntialiasingStatusV2 {uint32_t struct_size=sizeof(AnySceneAntialiasingStatusV2),version=2;AnySceneAntialiasingStatusV1 base;
 uint32_t temporal_active=0,jitter_active=0,last_fallback_reason=0,editor_overlay=0;uint64_t jittered_frames=0,jitter_skipped=0;float gpu_ms=-1;uint32_t reserved[4]={};};
struct AnySceneAntialiasingV3 {uint32_t struct_size,version;bool(*set)(const AnySceneAntialiasingParametersV3*);bool(*status)(AnySceneAntialiasingStatusV1*);bool(*status_v2)(AnySceneAntialiasingStatusV2*);};
