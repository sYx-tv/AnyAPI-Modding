#pragma once
#include "anyapi_mod_v1.h"
enum AnyGpuKindV1:uint32_t {ANY_GPU_RECT=1,ANY_GPU_ROUND_RECT,ANY_GPU_ELLIPSE,ANY_GPU_LINE,ANY_GPU_POLYGON,ANY_GPU_TEXT,ANY_GPU_IMAGE,ANY_GPU_CLIP_PUSH,ANY_GPU_CLIP_POP};
enum AnyGpuFlagsV1:uint32_t {ANY_GPU_STROKE=1,ANY_GPU_BOLD=2,ANY_GPU_CENTER=4,ANY_GPU_VCENTER=8,ANY_GPU_NOWRAP=16,ANY_GPU_DASH=32};
struct AnyGpuPointV1 {float x{},y{};};
// Pixel coordinates, affine row-vector transform, straight ARGB color.
// emit copies text/points immediately; texture copies BGRA premultiplied pixels
// only when its revision changes. No graphics-device pointers cross the ABI.
struct AnyGpuCommandV1 {
 uint32_t struct_size{sizeof(AnyGpuCommandV1)},kind{},flags{},color{0xffffffff};
 float rect[4]{},source[4]{},matrix[6]{1,0,0,1,0,0};
 float stroke{1},radius{},font_size{14},opacity{1};
 uint64_t texture{};const wchar_t* text{};uint32_t text_length{};
 const AnyGpuPointV1* points{};uint32_t point_count{};
};
struct AnyGpuDrawV1 {
 uint32_t struct_size{sizeof(AnyGpuDrawV1)},version{1};
 bool (*register_renderer)(void(*)(const AnyFrameV1*,void*),void*){};
 bool (*texture)(uint64_t,uint32_t,uint32_t,uint32_t,const uint8_t*,uint64_t){};
 bool (*emit)(const AnyGpuCommandV1*){};
 bool (*measure)(const wchar_t*,uint32_t,float,uint32_t,float*,float*){};
};
