#pragma once
#include "anyapi_scene_effects_v2.h"
// V1/V2 setters map to: sky_model 0, cloud_model 0, weather 0, moon_disc_follows_light 0, shading_revision 0,
// tone_revision 0, bloom_falloff 1, dither 1, flags 0. Bug fixes listed in docs/api/scene-effects.md apply to all versions.
enum : uint32_t {ANY_FX_KEEP_NATIVE_SSAO=1};
struct AnySceneEffectsParametersV3 {
 uint32_t struct_size{sizeof(AnySceneEffectsParametersV3)},version{3};
 AnySceneEffectsParametersV2 water;     // V2 meaning unchanged
 uint32_t sky_model{1};                 // 0 = V2 night sky; 1 = game clock (stars and moon all night, phases, dawn fade, sparse stars)
 uint32_t cloud_model{1};               // 0 = V2 clouds; 1 = clouds v2 (mean-filled far octaves, dense cloud hides the sun, sun/moon keys, game wind)
 uint32_t weather{0};                   // 1 = rain, snow and storms dim stars, moon and sun disc, thicken clouds, roughen the sea, hold exposure on lightning
 uint32_t moon_disc_follows_light{0};   // 1 = moon disc at the game's night light direction (with scene_controls v4 moon_mode 1)
 uint32_t shading_revision{1};          // 0 = V2 AO/bounce; 1 = screen-share AO cap, thickness, sky weighting; bounce occlusion test
 uint32_t tone_revision{0};             // 0 = V2 Filmic; 1 = Filmic with more midtone contrast
 float bloom_falloff{1};                // 0.5..1: weight r^n per bloom level; 1 = V2
 uint32_t dither{1};                    // about 1 LSB before 8-bit output
 uint32_t flags{0};                     // ANY_FX_*
 uint32_t reserved[4]{};                // must be 0
};
struct AnySceneEffectsStatusV3 {
 uint32_t struct_size{sizeof(AnySceneEffectsStatusV3)},version{3};
 AnySceneEffectsStatusV2 base;
 int32_t pass_error[16]{};              // AnyScenePassV2 index: 0 ok/off, >0 last bypass code, -1 inactive by design
 uint32_t sky_model_active{},cloud_model_active{},weather_active{},clock_valid{},native_ssao_skipped{},dither_active{};
 float sun_visibility{0};
 uint32_t reserved[4]{};
};
struct AnySceneEffectsV3 {uint32_t struct_size,version;bool(*set)(const AnySceneEffectsParametersV3*);bool(*status)(AnySceneEffectsStatusV1*);bool(*status_v2)(AnySceneEffectsStatusV2*);bool(*status_v3)(AnySceneEffectsStatusV3*);};
