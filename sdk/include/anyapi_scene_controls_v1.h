#pragma once
#include <cstdint>
// Native renderer controls applied before scene building/command recording.
// No raw pointers, depth/motion textures, TAA or native shader replacement.
struct AnySceneParametersV1 {
 uint32_t struct_size{sizeof(AnySceneParametersV1)},version{1},enabled{};
 uint32_t aa{},bloom{},ssao{},shadows{},fog_blur{}; // 0=game setting, 1=off, 2=on
 float bloom_threshold{.75f},bloom_intensity{.2f};
 float sun{1},sky{1},ambient{1},fog{1},light_exposure{};
};
struct AnySceneStatusV1 {
 uint32_t struct_size{sizeof(AnySceneStatusV1)},version{1},ready{},reserved{};
 uint64_t renderer_calls{},scene_calls{},rejected_frames{};
};
struct AnySceneControlsV1 {
 uint32_t struct_size{sizeof(AnySceneControlsV1)},version{1};
 bool (*set)(const AnySceneParametersV1*){};
 bool (*status)(AnySceneStatusV1*){};
};
