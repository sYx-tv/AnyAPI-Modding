#pragma once
#include <cstdint>
// Service "custommap.maps", version 1, published by the CustomMap mod in AnyAPI_ModInit.
// Map packs live in <game>\AnyAPI and Modding\maps\<name>\ (map.json + tiles\). The selected pack replaces the
// game's world tiles for every world created or loaded after select(); "" selects the normal world.
// Call select() before a world starts generating or streaming tiles (for example when Start game is pressed, or
// when a save's settings are applied). All functions may be called from any thread. Names are UTF-8.
struct CustomMapsV1 {
    uint32_t struct_size{sizeof(CustomMapsV1)}, version{1};
    uint32_t (*count)(){};                                       // installed map packs (rescans the maps folder)
    bool (*name)(uint32_t index, char* buffer, uint32_t capacity){};  // pack folder name, sorted, NUL-terminated
    bool (*select)(const char* name){};                          // false if the pack is missing or not loadable
    bool (*selected)(char* buffer, uint32_t capacity){};         // current pack, "" for the normal world
};
