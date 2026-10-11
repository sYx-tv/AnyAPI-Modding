#pragma once
#include <stdint.h>
// Smoothed GPU cost of AnyAPI's scene passes, for settings readouts.
// Milliseconds are negative while a pass is off or not yet measured.
// frame_ms is the CPU interval between scene frames (1000/frame_ms = FPS).
struct AnySceneTimingV1 {uint32_t struct_size=sizeof(AnySceneTimingV1),version=1;float lighting_ms=-1,antialiasing_ms=-1,frame_ms=-1;};
struct AnySceneTimingServiceV1 {uint32_t struct_size,version;bool(*copy)(AnySceneTimingV1*);};
