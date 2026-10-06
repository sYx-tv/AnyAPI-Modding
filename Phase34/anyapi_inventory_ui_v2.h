#pragma once
#include "anyapi_inventory_ui_v1.h"
#include "anyapi_item_catalog_v1.h"
// Optional owned container predicate and reserved native layout rows.
// Metadata is copied; unknown definitions carry the native display name.
struct AnyInventoryUiV2 {
 uint32_t struct_size{sizeof(AnyInventoryUiV2)},version{2};
 const AnyInventoryUiV1* operations{};
 bool (*register_section)(const char* id,bool(*visible)(const AnyItemDefinitionV1*,void*),void(*draw)(const AnyInventoryUiFrameV1*,void*),void* user,uint32_t reserved_rows){};
};
