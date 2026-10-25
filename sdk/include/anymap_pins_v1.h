#pragma once
#include <cstdint>
// Map pins other mods place on AnyMap's full map and minimap (traders, mission spots). AnyMap publishes it as
// anymap.pins, version 1, in its AnyAPI_ModReady; query it lazily (it may be published after yours) and treat a
// missing service as "AnyMap is not installed". Pins live until removed or the game closes; AnyMap does not save them.
// x and z are world metres (the plane players move on). Strings are copied; ids are per owner.
struct AnyMapPinV1 {
 uint32_t struct_size{sizeof(AnyMapPinV1)},version{1};
 double x{},z{};
 uint32_t color{0xffe8c66a}; // ARGB
 uint32_t reserved{};
 char owner[32]{},id[32]{},label[64]{};
};
struct AnyMapPinsV1 {
 uint32_t struct_size{sizeof(AnyMapPinsV1)},version{1};
 bool (*set)(const AnyMapPinV1*){};           // adds the pin, or moves/renames it when owner+id exists
 bool (*remove)(const char* owner,const char* id){};
};
