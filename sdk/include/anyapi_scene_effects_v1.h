#pragma once
#include <stdint.h>
// anyapi.scene_effects v1: HDR finishing in the scene, after volumetric
// lighting and before the game's bloom, tone mapping and HUD.
// tonemap: 0 Game (the game's own ACES curve), 1 Filmic (AgX-style, keeps
//   colour in bright light), 2 Clean (neutral, bright and true to albedo),
//   3 Soft film (AnyAPI 0.39.3+: long straight middle, slow highlight roll-off).
// look: 0 None, 1 Warm, 2 Cool, 3 Teal & orange, 4 Moody, 5 Vivid,
//   6 Soft & warm (AnyAPI 0.39.3+: golden highlights, cool shade);
//   look_strength 0..1 blends it in.
// exposure: manual stops, -3..3. eye_adaptation 0/1 briefly brightens or
//   darkens after big brightness changes (caves, tunnels, looking at the sun)
//   and settles back to the game's own brightness; adaptation_seconds 0.2..10.
// bloom 0..1 replaces the game's bloom with a wide, threshold-free bloom;
//   0 is no bloom. glare 0..2 adds a glow around the sun when it is visible.
// vignette 0..1 darkens the corners.
// finish 0/1 runs the finishing pass above. While it runs, the game's own bloom
// is turned off for the frame, because this pass replaces it.
// ambient_occlusion 0/1 replaces the output of the game's SSAO with a
//   detailed estimate (ao_radius 0.25..4 m, ao_strength 0..2) before the game
//   lights the scene with it. The game applies it to ambient and back light
//   only. While it is on, the game's SSAO switch is forced on so it runs.
// night_sky 0/1 adds stars, a milky way and a moon to the night sky, all
//   following the game's sun (stars 0..2, moon 0..2 brightness), and
//   moonlight 0..2 lights surfaces from the moon (no moon shadows). Nothing
//   is added in daylight.
// contact_shadows 0/1 adds short-range sun shadows the game's shadow maps are
//   too coarse for (under parts, between blocks); contact_length 0.05..4 m.
// bounce_light 0..2 adds one bounce of screen-space indirect light (lit
//   surfaces tint nearby ones with their colour); 0 is off.
// depth_of_field 0..4 blurs what is nearer or farther than the thing in the
//   middle of the screen (focus eases over a quarter second); motion_blur
//   0..2 blurs the scene as the camera turns and moves (camera only: the game
//   has no per-object motion). Both default to 0 (off); held items and
//   anything within about a metre never blur.
// draw_distance 1..4 multiplies the distances at which the game stops drawing
//   characters, props, dropped items, projectiles and vehicle parts (1 is the
//   game's own; terrain and trees follow the game's render distance).
// sun_disc 0..2 draws a limb-darkened sun in the game's sun colour, and clouds
//   0..2 adds a drifting cloud layer (amount of cover) lit by the sun by day
//   and the moon by night. Both default to 0 (off) and sit behind the game's
//   own clouds.
// hide_ui 0/1 is photo mode: it turns on the game's own "disable UI" switch,
//   which hides the HUD, crosshair and menus, until it is set back to 0.
//   Turn it off whenever a menu needs to be seen.
// Only one active mod owns the policy.
struct AnySceneEffectsParametersV1 {uint32_t struct_size=sizeof(AnySceneEffectsParametersV1),version=1,enabled=0,finish=1,tonemap=1,look=0,eye_adaptation=1;
 float look_strength=1,exposure=0,adaptation_seconds=1.5f,bloom=.35f,glare=1,vignette=0;uint32_t ambient_occlusion=1;float ao_radius=1.5f,ao_strength=1;
 uint32_t night_sky=1;float stars=1,moon=1,moonlight=1;uint32_t contact_shadows=1;float contact_length=.6f,bounce_light=1;
 float depth_of_field=0,motion_blur=0,draw_distance=1,sun_disc=0,clouds=0;uint32_t hide_ui=0;};
// adapted_exposure: the stops currently added by eye adaptation.
// scene_luminance: the measured average scene luminance, for diagnostics.
// ao_*, sky_*, contact_*, gi_* and lens_* describe the ambient occlusion,
// night sky, contact shadow, bounce light and lens passes. distance_available
// says the draw distance hooks are installed and distance_scale is the
// multiplier in use. photo_available says hide_ui can hide the game's UI.
struct AnySceneEffectsStatusV1 {uint32_t struct_size=sizeof(AnySceneEffectsStatusV1),version=1,available=0,ready=0;uint64_t frames=0,rejected_frames=0;
 float gpu_ms=-1,adapted_exposure=0,scene_luminance=-1,sun_visibility=0;int32_t error=0;uint32_t ao_available=0,ao_ready=0;uint64_t ao_frames=0;float ao_ms=-1;
 uint32_t sky_ready=0;float sky_ms=-1,night=0;uint32_t contact_ready=0;float contact_ms=-1;uint32_t gi_ready=0;float gi_ms=-1;
 uint32_t lens_ready=0;float lens_ms=-1;
 uint32_t distance_available=0;float distance_scale=1;uint32_t photo_available=0;};
struct AnySceneEffectsV1 {uint32_t struct_size,version;bool(*set)(const AnySceneEffectsParametersV1*);bool(*status)(AnySceneEffectsStatusV1*);};
