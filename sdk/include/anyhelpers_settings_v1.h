#pragma once
#include <cstdint>
// Service supplied by AnyHelpers.dll, not by AnyAPI.
// Query anyhelpers.settings version 1 in AnyAPI_ModReady().
enum AnySettingKindV1:uint32_t {
    ANY_SETTING_BOOL=1,ANY_SETTING_INTEGER,ANY_SETTING_NUMBER,ANY_SETTING_CHOICE,ANY_SETTING_TEXT
};
struct AnyModSettingV1 {
    uint32_t struct_size{sizeof(AnyModSettingV1)},kind{ANY_SETTING_BOOL};
    const char* mod_id{};const char* mod_name{};const char* setting_id{};
    const char* label{};const char* description{};int32_t order{};
    double default_number{},minimum{},maximum{1},step{1};
    const char* default_text{};
    const char* const* choices{};uint32_t choice_count{};
    uint32_t text_limit{256};
};
struct AnySettingValueV1 {
    uint32_t struct_size{sizeof(AnySettingValueV1)},kind{};
    double number{}; // bool 0/1, integer, decimal, or choice index
    char text[257]{}; // UTF-8, always terminated; meaningful only for TEXT
};
struct AnyHelpersSettingsV1 {
    uint32_t struct_size{sizeof(AnyHelpersSettingsV1)},version{1};
    uint64_t (*register_setting)(const AnyModSettingV1*){};
    bool (*get)(uint64_t token,AnySettingValueV1* committed){};
    uint64_t (*revision)(){}; // increases after a successful commit with changes
};
