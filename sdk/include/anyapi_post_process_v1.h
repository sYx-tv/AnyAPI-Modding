#pragma once
#include <cstdint>
// Generic GPU shader passes. All data is copied; no graphics pointers cross the ABI.
// Register from ModInit/ModReady; set parameters from an owned mod callback.
// HLSL main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target, ps_5_0.
// t0=input (token 0=current previous full-resolution result), t1=original finished
// game image, t2=auxiliary (token 0=original). s0=linear clamp. b0=float4
// output_size(width,height,1/width,1/height),float4 input_size. b1=float4 user[16].
// Dependencies must be previously registered passes owned by the same mod.
// Disabled passes resolve to their input. Downsampled passes never become the
// displayed image. Each enabled full-resolution pass becomes the next result.
// This v1 hook is AFTER native HUD composition, BEFORE AnyAPI drawing; no depth,
// motion vectors, HDR scene input, native AA replacement or render-scale control.
struct AnyPostPassV1 {
 uint32_t struct_size{sizeof(AnyPostPassV1)},version{1};
 const char* id{};const char* shader{};uint32_t shader_bytes{},downsample{1};
 uint64_t input{},auxiliary{};
};
struct AnyPostParametersV1 {
 uint32_t struct_size{sizeof(AnyPostParametersV1)},enabled{};
 float values[64]{};
};
struct AnyPostStatusV1 {
 uint32_t struct_size{sizeof(AnyPostStatusV1)},version{1};
 uint32_t ready{},active_passes{},width{},height{};
 uint64_t frames{},resource_generation{};
 double cpu_submit_ms{}; // CPU submission only; never presented as GPU time.
 char error[256]{};
};
struct AnyPostProcessV1 {
 uint32_t struct_size{sizeof(AnyPostProcessV1)},version{1};
 uint64_t (*register_pass)(const AnyPostPassV1*){};
 bool (*set)(uint64_t,const AnyPostParametersV1*){};
 bool (*status)(AnyPostStatusV1*){};
};
