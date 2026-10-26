#pragma once
#include "anyapi_scene_effects_v1.h"
// anyapi.scene_effects v2 (AnyAPI 0.40.2): the v1 parameters plus ocean
// reflections. Query version 2; version 1 stays available and unchanged.
// water_reflections 0..1: screen-space reflections on the ocean of whatever is
//   on screen above the water (boats, coasts, hills), scaled by Fresnel
//   (Schlick, 0.02 head-on). The game's water already reflects its sky, so only
//   the difference from that sky is added: where a reflected ray finds nothing
//   on screen the water is unchanged. 0 is off.
// sun_glint 0..2: a sharp highlight of the sun on the rippled water (GGX,
//   roughness about 0.1) in the game's sun colour, while the sun is up. 0 is off.
// reflection_quality 0..2: 16, 24 or 32 ray steps at half resolution, each
//   followed by 4 refinement steps.
// Small ripples, fixed in the world and drifting with the game's wind, shape
// both. Setting the v1 parameters turns both off.
struct AnySceneEffectsParametersV2 {uint32_t struct_size=sizeof(AnySceneEffectsParametersV2),version=2;AnySceneEffectsParametersV1 effects;
 float water_reflections=0,sun_glint=0;uint32_t reflection_quality=1;};
// water_ready, water_ms (smoothed GPU time, negative while off or not yet
// measured) and water_frames describe the ocean reflection pass.
struct AnySceneEffectsStatusV2 {uint32_t struct_size=sizeof(AnySceneEffectsStatusV2),version=2;AnySceneEffectsStatusV1 effects;
 uint32_t water_ready=0;float water_ms=-1;uint64_t water_frames=0;};
// status fills the v1 status; status_v2 fills the v1 status and the water pass.
struct AnySceneEffectsV2 {uint32_t struct_size,version;bool(*set)(const AnySceneEffectsParametersV2*);bool(*status)(AnySceneEffectsStatusV1*);bool(*status_v2)(AnySceneEffectsStatusV2*);};
