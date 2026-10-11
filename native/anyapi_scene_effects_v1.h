#pragma once
#include <stdint.h>
// anyapi.scene_effects v1: HDR finishing in the scene, after volumetric
// lighting and before the game's bloom, tone mapping and HUD.
// tonemap: 0 Game (the game's own ACES curve), 1 Filmic (AgX-style, keeps
//   colour in bright light), 2 Clean (neutral, bright and true to albedo).
// look: 0 None, 1 Warm, 2 Cool, 3 Teal & orange, 4 Moody, 5 Vivid;
//   look_strength 0..1 blends it in.
// exposure: manual stops, -3..3. eye_adaptation 0/1 briefly brightens or
//   darkens after big brightness changes (caves, tunnels, looking at the sun)
//   and settles back to the game's own brightness; adaptation_seconds 0.2..10.
// bloom 0..1 replaces the game's bloom with a wide, threshold-free bloom;
//   0 is no bloom. glare 0..2 adds a glow around the sun when it is visible.
// vignette 0..1 darkens the corners.
// Only one active mod owns the policy. With enabled=1 the game's own bloom is
// turned off for the frame, because this pass replaces it.
struct AnySceneEffectsParametersV1 {uint32_t struct_size=sizeof(AnySceneEffectsParametersV1),version=1,enabled=0,tonemap=1,look=0,eye_adaptation=1;
 float look_strength=1,exposure=0,adaptation_seconds=1.5f,bloom=.35f,glare=1,vignette=0;};
// adapted_exposure: the stops currently added by eye adaptation.
// scene_luminance: the measured average scene luminance, for diagnostics.
struct AnySceneEffectsStatusV1 {uint32_t struct_size=sizeof(AnySceneEffectsStatusV1),version=1,available=0,ready=0;uint64_t frames=0,rejected_frames=0;
 float gpu_ms=-1,adapted_exposure=0,scene_luminance=-1,sun_visibility=0;int32_t error=0;};
struct AnySceneEffectsV1 {uint32_t struct_size,version;bool(*set)(const AnySceneEffectsParametersV1*);bool(*status)(AnySceneEffectsStatusV1*);};
