#pragma once
#include "anyapi_inventory_ui_v2.h"
// Compact native rows and symbol buttons; tooltip strings are copied on hover.
struct AnyInventoryUiV3 {
 uint32_t struct_size{sizeof(AnyInventoryUiV3)},version{3};
 const AnyInventoryUiV2* sections{};
 bool (*begin_compact_row)(const char* id,uint32_t columns){};
 uint32_t (*symbol_button)(const char* id,const char* symbol,const char* tooltip,uint32_t disabled){};
};
