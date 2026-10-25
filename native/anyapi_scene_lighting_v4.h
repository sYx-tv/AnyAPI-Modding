#pragma once
#include "anyapi_scene_lighting_v3.h"
// V1-V3 setters reset every V4 extra to: lamp_model 0, flags 0, atmosphere_flags 0, amd_path 0, weather_response 0.
enum : uint32_t {ANY_LAMP_UNDERGROUND=1,ANY_LAMP_TRACKING=2,ANY_LAMP_FLASHES=4,ANY_LAMP_OCEAN=8};
enum : uint32_t {ANY_ATMOS_SKY_FOG=1,ANY_ATMOS_SKY_INSCATTER=2,ANY_ATMOS_CLARITY_METRES=4,ANY_ATMOS_SEA_PLANE=8,
                 ANY_ATMOS_WEATHER_DENSITY=16,ANY_ATMOS_PHASE_CAP=32,ANY_ATMOS_FLASH_GLOW=64};
struct AnySceneLightingParametersV4 {
 uint32_t struct_size{sizeof(AnySceneLightingParametersV4)},version{4};
 AnySceneLightingParametersV3 shaped;  // V3 meaning unchanged; shaped.lighting.local_budget is ignored (lamp_budget wins)
 uint32_t lamp_model{0};      // 0 = V3 marched lamps. Values above status_v2.lamp_model_max are rejected (0 in 0.40.2)
 uint32_t lamp_quality{2};    // 0..3, for lamp_model 1 (future); validated
 uint32_t lamp_budget{8};     // 1..16, clamped to status_v2.lamp_budget_max (8 in 0.40.2)
 uint32_t lamp_shadows{1};    // 0 off, 1 auto (where supported), 2 on (still off where unsupported, see status_v2)
 uint32_t lamp_shape{0};      // 0..3, for lamp_model 1 (future)
 float weather_response{0};   // 0..1: lamp scattering thickens with the game's fog and rain
 float ambient_washout{0};    // 0..1, for lamp_model 1 (future)
 uint32_t flags{ANY_LAMP_UNDERGROUND|ANY_LAMP_TRACKING|ANY_LAMP_FLASHES|ANY_LAMP_OCEAN};
 uint32_t native_halos{0};    // must be 0 in 0.40.2
 uint32_t amd_path{0};        // 0 auto (= 1); 1 constant-buffer path with per-count permutations; 2 same with a uniform branch per slot (experiment). Ignored off AMD
 uint32_t atmosphere_flags{0};// ANY_ATMOS_*; bits outside status_v2.atmosphere_flags_supported are rejected
 float sky_inscatter{1};      // 0..2, for ANY_ATMOS_SKY_INSCATTER (future)
 uint32_t reserved[4]{};      // must be 0
};
struct AnySceneLightingStatusV2 {
 uint32_t struct_size{sizeof(AnySceneLightingStatusV2)},version{2};
 AnySceneLightingStatusV1 base;
 uint32_t lamps_drawn{},lamps_shadowed{},flashes_seen{},candidates_omni{},candidates_spot{},game_spot_maps{};
 uint32_t lamp_budget_max{8},lamp_model_max{0},lamp_model_active{0},lamp_shadows_active{0},amd_path_active{0},underground_active{0};
 uint32_t atmosphere_flags_supported{},atmosphere_flags_active{}; // 0.40.2 supports SEA_PLANE|WEATHER_DENSITY|PHASE_CAP|FLASH_GLOW
 uint32_t raw29_hits{},raw29_misses{},ring_waits{},bypass_reason{};
 float capture_us{-1};
 uint32_t reserved[4]{};
};
struct AnySceneLightingV4 {uint32_t struct_size,version;bool(*set)(const AnySceneLightingParametersV4*);bool(*status)(AnySceneLightingStatusV1*);bool(*status_v2)(AnySceneLightingStatusV2*);};
