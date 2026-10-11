#pragma once
#include "anyapi_scene_antialiasing_v1.h"
// V2 adds temporal methods and sharpening without changing the V1 ABI.
// method: 1 SMAA 1x, 2 Enhanced SMAA, 3 TAA (SMAA blended with history
// reprojected through scene depth and the camera), 4 TAA with subpixel camera
// jitter. sharpening 0..1: contrast-adaptive sharpening after AA, before HUD.
// Status is the V1 structure.
struct AnySceneAntialiasingParametersV2 {uint32_t struct_size=sizeof(AnySceneAntialiasingParametersV2),version=2,enabled=0,method=1,quality=2;float sharpening=0;};
struct AnySceneAntialiasingV2 {uint32_t struct_size,version;bool(*set)(const AnySceneAntialiasingParametersV2*);bool(*status)(AnySceneAntialiasingStatusV1*);};
