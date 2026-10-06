#include "anymaker_mod_api.h"
#include <type_traits>
#include <cassert>
#include <iostream>
static_assert(std::is_standard_layout_v<AnymakerModContextV16>);
static_assert(offsetof(AnymakerModContextV16,register_inventory_pulse)==sizeof(AnymakerModContextV15));
static_assert(ANYMAKER_MOD_API_VERSION==16);
int main() {
    assert(sizeof(void*)==8);
    assert(offsetof(AnymakerUiRenderFrameV1,visible_panels)>offsetof(AnymakerUiRenderFrameV1,ui_cell_size));
    std::cout<<"PASS: x64 API v16 preserves v15 context prefix\n";
}
