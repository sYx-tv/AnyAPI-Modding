#pragma once
#include <cstdint>
// Version 1 exposes a native section slot in Settings > Controls.
// Additional menu locations require separate, verified native contracts.
enum AnyMenuLocation:uint32_t { ANY_MENU_SETTINGS_CONTROLS=1 };
enum AnyMenuEvent:uint32_t { ANY_MENU_OPEN=1,ANY_MENU_APPLY,ANY_MENU_RESET,ANY_MENU_CANCEL };
struct AnyMenuFrameV1 {uint32_t struct_size{sizeof(AnyMenuFrameV1)},location{ANY_MENU_SETTINGS_CONTROLS};uint64_t tick{};};
struct AnyMenuSectionV1 {
 uint32_t struct_size{sizeof(AnyMenuSectionV1)},location{ANY_MENU_SETTINGS_CONTROLS};
 const char* id{};const char* title{};int32_t order{};void* user{};
 void (*draw)(const AnyMenuFrameV1*,void*){};
 void (*event)(uint32_t,void*){};
 uint32_t (*dirty)(void*){};
 uint32_t (*defaults)(void*){};
};
struct AnyMenuV1 {
 uint32_t struct_size{sizeof(AnyMenuV1)},version{1};
 bool (*add_section)(const AnyMenuSectionV1*){};
 uint32_t (*available)(){}; // verified native bridge is installed
 uint32_t (*settings_open)(){};
 // Widgets are valid only inside draw. IDs are automatically section-namespaced.
 void (*heading)(const char* id,const char* text){};
 bool (*begin_table)(const char* id,int32_t columns,int32_t width_cells){};
 void (*end_table)(){};
 uint32_t (*button)(const char* id,const char* text,uint32_t disabled){};
};
