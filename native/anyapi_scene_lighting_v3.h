#pragma once
#include "anyapi_scene_lighting_v2.h"
// V3 adds beam shaping and temporal smoothing without changing the V1/V2 ABI.
// clarity 0..1: 0 keeps the original uniform clear-air scattering; higher
//   values keep beams where rays cross shadow edges and thin fully lit air.
// beam_reach: metres over which clear-air beams fade; 0 = unlimited, else 10..2000.
// sun_response 0/1: scale beams by sun height (strong low sun, subtle noon,
//   none below the horizon) and warm them while the sun is low.
// temporal 0/1: jitter ray samples and blend with reprojected history.
struct AnySceneLightingParametersV3 {
 uint32_t struct_size{sizeof(AnySceneLightingParametersV3)},version{3};
 AnySceneLightingParametersV2 lighting;
 float clarity{.85f},beam_reach{120};uint32_t sun_response{1},temporal{1};
};
struct AnySceneLightingV3 {
 uint32_t struct_size,version;
 bool(*set)(const AnySceneLightingParametersV3*);
 bool(*status)(AnySceneLightingStatusV1*);
};
