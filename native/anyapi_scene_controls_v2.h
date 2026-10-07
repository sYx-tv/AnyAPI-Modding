#pragma once
#include "anyapi_scene_controls_v1.h"
// v1 remains unchanged. v2 adds scoped native scene-detail switches.
struct AnySceneParametersV2 {
 uint32_t struct_size{sizeof(AnySceneParametersV2)},version{2};
 AnySceneParametersV1 scene;
 uint32_t clouds{},grass{},foliage{}; // 0=game setting, 1=off, 2=on
};
struct AnySceneControlsV2 {
 uint32_t struct_size,version;
 bool(*set)(const AnySceneParametersV2*);
 bool(*status)(AnySceneStatusV1*);
};
