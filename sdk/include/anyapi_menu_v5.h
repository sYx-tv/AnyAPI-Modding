#pragma once
#include "anyapi_menu_v4.h"
// Optional hover reporting for settings rows; V1-V4 tables remain unchanged.
// Query anyapi.menu, version 5, through AnyAPI_GetServices(1).
struct AnyMenuV5 {
    uint32_t struct_size{sizeof(AnyMenuV5)},version{5};
    const AnyMenuV4* buttons{};
    // The id, as passed to toggle_row, number_row, choice_row, text_row or button_row, of the row whose value the mouse
    // or controller focus is on, or "" when none. The row reports it while drawing, so read it after drawing that row (or
    // in the next frame). Valid until the next draw callback returns.
    const char* (*hovered_row)(){};
};
