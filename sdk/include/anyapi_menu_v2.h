#pragma once
#include "anyapi_menu_v1.h"
// Query anyapi.menu version 2 through AnyAPI_GetServices(1).
// Version 1 remains available with its original layout and Controls section slot.
constexpr uint32_t ANY_MENU_SETTINGS_TAB=2;
constexpr uint32_t ANY_MENU_DEACTIVATE=5; // leaving a tab releases capture, retaining pending edits
// Native tabs require an atlas icon. NONE uses the default Settings gear.
// Missing requested icons fall back to the gear; missing both suppresses the tab.
enum AnyMenuIconV2:uint32_t { ANY_MENU_ICON_NONE,ANY_MENU_ICON_SETTINGS,ANY_MENU_ICON_KEYBOARD };
enum AnyMenuRowResultV2:uint32_t { ANY_MENU_ROW_REBIND=1,ANY_MENU_ROW_CLEAR=2 };
struct AnyMenuTabV2 {
 uint32_t struct_size{sizeof(AnyMenuTabV2)};
 const char* id{};const char* title{};int32_t order{};uint32_t icon{ANY_MENU_ICON_NONE};void* user{};
 void (*draw)(const AnyMenuFrameV1*,void*){};
 void (*event)(uint32_t,void*){};
 uint32_t (*dirty)(void*){};
 uint32_t (*defaults)(void*){};
};
struct AnyMenuV2 {
 uint32_t struct_size{sizeof(AnyMenuV2)},version{2};
 bool (*add_tab)(const AnyMenuTabV2*){};
 uint32_t (*available)(){};
 uint32_t (*settings_open)(){};
 // Valid inside draw only. IDs are automatically owner/tab-namespaced.
 void (*heading)(const char* id,const char* text){};
 void (*label)(const char* id,const char* text){};
 // Compact native row: label left, key field and clear icon right.
 uint32_t (*key_row)(const char* id,const char* label,const char* key_text,uint32_t disabled){};
};
