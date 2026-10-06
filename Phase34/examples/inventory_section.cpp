#include "../anyapi_services_v1.h"
#include "../anyapi_inventory_ui_v1.h"
static const AnyInventoryUiV1* inventory;
static void draw_storage(const AnyInventoryUiFrameV1* frame,void*) {
 if(!inventory->begin_row("example",1))return;
 inventory->button("info",frame->storage_name,1);
 inventory->end_row();
}
void register_storage_example(){auto services=AnyAPI_Services();if(!services)return;
 inventory=(const AnyInventoryUiV1*)services->query("anyapi.inventory_ui",1);
 if(inventory&&inventory->struct_size==sizeof(*inventory)&&inventory->version==1)
  inventory->register_section("example_storage",draw_storage,nullptr);
}
