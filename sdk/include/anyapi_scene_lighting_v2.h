#pragma once
#include "anyapi_scene_lighting_v1.h"
// V2 adds local-light scattering without changing the V1 ABI.
struct AnySceneLightingParametersV2 {
 uint32_t struct_size{sizeof(AnySceneLightingParametersV2)},version{2};
 AnySceneLightingParametersV1 scene;
 float local_strength{1};uint32_t local_budget{4};
};
struct AnySceneLightingV2 {
 uint32_t struct_size,version;
 bool(*set)(const AnySceneLightingParametersV2*);
 bool(*status)(AnySceneLightingStatusV1*);
};
