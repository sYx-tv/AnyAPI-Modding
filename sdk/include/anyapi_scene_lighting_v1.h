#pragma once
#include <cstdint>
// Experimental 0.29.0 contract. Availability is not visual acceptance.
// sun_shafts: finite native-relative radiance scale, 0..15.
struct AnySceneLightingParametersV1 {
 uint32_t struct_size{sizeof(AnySceneLightingParametersV1)},version{1},enabled{},quality{2};
 float fog_density{.002f},sun_shafts{1},fog_strength{1},height_falloff{.02f},base_height{},maximum_distance{500},anisotropy{.35f};
};
struct AnySceneLightingStatusV1 {
 uint32_t struct_size{sizeof(AnySceneLightingStatusV1)},version{1},available{},frame_valid{},ready{},width{},height{},shadow_count{},reason{};
 uint64_t frames{},rejected_frames{};
 int32_t error{};
};
struct AnySceneLightingV1 {
 uint32_t struct_size,version;
 bool(*set)(const AnySceneLightingParametersV1*);
 bool(*status)(AnySceneLightingStatusV1*);
};
