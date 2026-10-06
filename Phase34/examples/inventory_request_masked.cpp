#include "../anyapi_services_v1.h"
#include "../anyapi_inventory_actions_v2.h"
uint64_t request_allowed_item(const char* id){auto services=AnyAPI_Services();auto api=services?(const AnyInventoryActionsV2*)services->query("anyapi.inventory_actions",2):nullptr;return api&&api->struct_size==sizeof(*api)&&api->version==2&&api->v1&&api->v1->available()?api->add_one(id,ANY_MODE_MASK_CREATIVE_SANDBOX):0;}
// Request once per deliberate activation; SENT does not acknowledge placement.
