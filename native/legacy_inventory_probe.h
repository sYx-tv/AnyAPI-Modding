#pragma once
#include <cstdint>

// A bounded read-only view of ONE proven handheld slot. This is not complete
// inventory enumeration: nested grids, worn slots and quantities remain pending.
struct P29InventoryProbe {
    uintptr_t inventory{}, item{}, definition{};
    bool header_readable{}, empty{}, definition_readable{};
};
template<class Read> P29InventoryProbe p29_probe_handheld(uintptr_t inventory,bool server,Read read) {
    P29InventoryProbe out{}; out.inventory=inventory;
    if(!inventory) return out;
    // get_handheld_item_world: inline ref at +0x28; master ref +0x20,
    // slave ref +0x10. Pinned GCL 41348/6300 + 38622/5045.
    out.header_readable=read(inventory+0x28+(server?0x20:0x10),&out.item,sizeof(out.item));
    if(!out.header_readable) return out;
    out.empty=out.item==0;
    if(out.empty) return out;
    out.definition_readable=read(out.item+(server?0x88:0x28),&out.definition,sizeof(out.definition)) && out.definition!=0;
    return out;
}
