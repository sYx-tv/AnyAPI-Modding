#pragma once
#include "anyapi_menu_v2.h"
// Optional native settings widgets; V1/V2 layouts remain unchanged.
// Query anyapi.menu, version 3, through AnyAPI_GetServices(1).
struct AnyMenuV3 {
    uint32_t struct_size{sizeof(AnyMenuV3)},version{3};
    const AnyMenuV2* tabs{};
    // Widgets work only in the registered tab's draw callback. Values are drafts.
    uint32_t (*toggle_row)(const char* id,const char* label,uint32_t* value){};
    uint32_t (*number_row)(const char* id,const char* label,double* value,
                          double minimum,double maximum,double step,uint32_t integer){};
    uint32_t (*choice_row)(const char* id,const char* label,const char* const* choices,
                          uint32_t count,uint32_t* index){};
    uint32_t (*text_row)(const char* id,const char* label,char* utf8,uint32_t capacity){};
};
