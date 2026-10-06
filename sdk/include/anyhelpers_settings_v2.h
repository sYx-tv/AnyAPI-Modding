#pragma once
#include "anyhelpers_settings_v1.h"
// Optional presentation service. Saved values and v1 consumers remain unchanged.
// Both conditions must match draft values; token 0 means no condition.
struct AnyHelpersSettingsV2 {
    uint32_t struct_size{sizeof(AnyHelpersSettingsV2)},version{2};
    uint64_t (*register_setting)(const AnyModSettingV1*){};
    bool (*get)(uint64_t,AnySettingValueV1*){};
    uint64_t (*revision)(){};
    bool (*visibility)(uint64_t row,uint64_t first,double first_value,uint64_t second,double second_value){};
};
