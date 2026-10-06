#pragma once
#include <cstdint>
// Native screen translation only: drawing and hit-testing share the same origin.
// Offsets are viewport fractions, each between -0.25 and +0.25, additive per owner.
// Exact native screen ID and UI-state selector scope each mod's contribution.
struct AnyScreenLayoutV1 {uint32_t struct_size{sizeof(AnyScreenLayoutV1)},version{1};uint32_t (*available)(){};bool (*set_offset)(const char* screen_id,int32_t ui_state,double x,double y){};};
