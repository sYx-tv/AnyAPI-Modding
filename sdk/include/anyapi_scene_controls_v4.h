#pragma once
#include "anyapi_scene_controls_v3.h"
// V1-V3 setters reset V4 extras to: tint_targets 0 (V3 targets), time_shaped 0, moon_mode 0, weather_fog_keep 0.
struct AnySceneParametersV4 {
 uint32_t struct_size{sizeof(AnySceneParametersV4)},version{4};
 AnySceneParametersV3 looked;           // V3 meaning; key_tint/shade_tint are the static tints
 uint32_t tint_targets{1};              // 0 = V3 (key: view up, light up, main); 1 = key on main + back, shade on sky + light down, view up/down untouched
 uint32_t time_shaped{0};               // 1 = palettes below weighted by the game clock's sun height; static tints while the clock is invalid
 float noon_key[3]{1,1,1},noon_shade[3]{1,1,1},golden_key[3]{1,1,1},golden_shade[3]{1,1,1};
 float blue_key[3]{1,1,1},blue_shade[3]{1,1,1},moon_key[3]{1,1,1},moon_shade[3]{1,1,1}; // each channel 0.5..1.5, luminance kept
 uint32_t moon_mode{0};                 // 0 off; 1 = moonlight in the game's night main light (parked direction); 2 reserved (rejected)
 float moonlight{0};                    // 0..0.03 absolute luminance target for light_main in the parked window
 float moon_phase_floor{.35f};          // 0..1
 uint32_t weather_fog_keep{0};          // 1 = storms keep the game's fog when the fog scale is below 1 (needs a weather source)
 float shadow_split{0}; uint32_t shadow_steps{0}; float grass_far{0}; // game-lever patches: must be 0 in 0.40.2
 uint32_t reserved[4]{};                // must be 0
};
struct AnySceneStatusV2 {
 uint32_t struct_size{sizeof(AnySceneStatusV2)},version{2};
 AnySceneStatusV1 base;
 uint32_t clock_source{},parked{},moon_mode_active{},weather_source{},tint_targets_active{};
 float t{-1},sun_y{0},moon_y{0},moon_light{0},rain{0},snow{0},cover{0},fog_ratio{1},wet{0};
 uint32_t flash_frames{};
 uint32_t reserved[4]{};
};
struct AnySceneControlsV4 {uint32_t struct_size,version;bool(*set)(const AnySceneParametersV4*);bool(*status)(AnySceneStatusV1*);bool(*status_v2)(AnySceneStatusV2*);};
