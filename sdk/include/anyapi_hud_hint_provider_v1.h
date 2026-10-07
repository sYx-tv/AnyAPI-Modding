#pragma once
#include <cstdint>
// Publish as hud.hints.<mod_id>, version 1. Strings are copied every native HUD build.
struct AnyHudHintV1 {
 uint32_t struct_size{sizeof(AnyHudHintV1)},version{1},key{};
 char label[96]{};
};
struct AnyHudHintProviderV1 {
 uint32_t struct_size{sizeof(AnyHudHintProviderV1)},version{1};
 bool (*copy)(AnyHudHintV1*){};
};
