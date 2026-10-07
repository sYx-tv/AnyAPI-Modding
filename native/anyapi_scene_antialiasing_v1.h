#pragma once
#include <stdint.h>
// method: 1=SMAA 1x, 2=Enhanced SMAA (extra spatial subpixel blending).
struct AnySceneAntialiasingParametersV1 {uint32_t struct_size=sizeof(AnySceneAntialiasingParametersV1),version=1,enabled=0,method=1,quality=2;};
struct AnySceneAntialiasingStatusV1 {uint32_t struct_size=sizeof(AnySceneAntialiasingStatusV1),version=1,available=0,ready=0,width=0,height=0;uint64_t frames=0,rejected_frames=0;int32_t error=0;};
struct AnySceneAntialiasingV1 {uint32_t struct_size,version;bool(*set)(const AnySceneAntialiasingParametersV1*);bool(*status)(AnySceneAntialiasingStatusV1*);};
