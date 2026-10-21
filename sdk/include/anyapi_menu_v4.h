#pragma once
#include "anyapi_menu_v3.h"
// Optional native settings button rows; V1/V2/V3 tables remain unchanged.
// Query anyapi.menu, version 4, through AnyAPI_GetServices(1).
struct AnyMenuV4 {
    uint32_t struct_size{sizeof(AnyMenuV4)},version{4};
    const AnyMenuV3* rows{};
    // A label and 1-4 native buttons sharing the value column, for one-shot actions. Works only in a registered native
    // section or tab draw callback. Returns the 1-based index of the button selected this frame, or 0.
    uint32_t (*button_row)(const char* id,const char* label,const char* const* buttons,uint32_t count){};
};
