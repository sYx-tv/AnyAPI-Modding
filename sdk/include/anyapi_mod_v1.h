#pragma once
#include "anyapi_players_v1.h"
#include <cstdint>
#include <cstddef>

// DLL plugin ABI independent of the legacy v16 context. Never exposes native pointers.
constexpr uint32_t ANYAPI_MOD_ABI=1;
enum AnyInputKind:uint32_t { ANY_KEY_DOWN=1,ANY_KEY_UP,ANY_MOUSE_MOVE,ANY_MOUSE_DOWN,ANY_MOUSE_UP,ANY_MOUSE_WHEEL,ANY_FOCUS_LOST,ANY_TEXT_INPUT };
// ANY_TEXT_INPUT carries one Unicode scalar in key; other fields are zero.
// Unknown event kinds should be ignored. Structures and existing enum values stay unchanged.
struct AnyInputV1 {uint32_t struct_size{sizeof(AnyInputV1)},kind{},key{},button{};int32_t x{},y{},wheel{};};
struct AnyFrameV1 {uint32_t struct_size{sizeof(AnyFrameV1)},width{},height{},focused{};uint64_t tick{};};
// BGRA8 premultiplied pixels owned by the plugin; valid until the next render callback.
// Change revision whenever pixels change; the backend reuses the GPU bitmap otherwise.
struct AnyCanvasV1 {uint32_t struct_size{sizeof(AnyCanvasV1)},width{},height{},pitch{};int32_t x{},y{};uint64_t revision{};const uint8_t* pixels{};};
struct AnyModCallbacksV1 {
 uint32_t struct_size{sizeof(AnyModCallbacksV1)},abi{ANYAPI_MOD_ABI};
 const char* id{};void* user{};
 void (*render)(const AnyFrameV1*,AnyCanvasV1*,void*){};
 uint32_t (*input)(const AnyInputV1*,void*){}; // nonzero consumes this event
 void (*shutdown)(void*){}; // process-lifetime plugins; hot unload is unsupported
};
struct AnyModHostV1 {
 uint32_t struct_size{sizeof(AnyModHostV1)},abi{ANYAPI_MOD_ABI};
 const wchar_t* game_directory{};const wchar_t* plugin_directory{};
 void (*log)(uint32_t,const char*,const char*){};
 bool (*copy_players)(AnySessionPlayersV1*,uint32_t* ui_flags){}; // UI bit0 inventory, bit1 known
 void (*capture_input)(uint32_t){}; // scoped to the active DLL callback; releases on focus loss
};
using AnyModInitV1=bool(*)(const AnyModHostV1*,AnyModCallbacksV1*);
static_assert(sizeof(void*)==8,"AnyAPI plugins require x64");
static_assert(offsetof(AnyCanvasV1,pixels)==32);
