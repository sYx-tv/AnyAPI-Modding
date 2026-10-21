#pragma once
#include "anyapi_scene_controls_v2.h"
// v3 (AnyAPI 0.39.3) adds light colour tints and a wind scale on top of v2.
// key_tint multiplies the sun and the lit-side sky light (main, up, view up);
// shade_tint multiplies the light that fills shadows (sky, back, down sides).
// Each tint is 0.5..1.5 per channel; AnyAPI keeps each light's brightness
// (Rec.709 luminance) unchanged, so a tint changes colour only.
// wind_scale 0..4 scales the wind the game sways trees and grass with, for
// rendering only (restored after the scene is built; physics never sees it).
struct AnySceneParametersV3 {
 uint32_t struct_size{sizeof(AnySceneParametersV3)},version{3};
 AnySceneParametersV2 detailed;
 float key_tint[3]{1,1,1},shade_tint[3]{1,1,1};
 float wind_scale{1};
};
struct AnySceneControlsV3 {
 uint32_t struct_size,version;
 bool(*set)(const AnySceneParametersV3*);
 bool(*status)(AnySceneStatusV1*);
};
