#include "../anyapi_services_v1.h"
#include "../anyapi_inventory_actions_v1.h"
// Call once per deliberate Add activation. Never retry SENT as if it were a failure.
uint64_t request_item(const char* stable_id){auto services=AnyAPI_Services();auto api=services?(const AnyInventoryActionsV1*)services->query("anyapi.inventory_actions",1):nullptr;return api&&api->struct_size==sizeof(*api)&&api->version==1&&api->available()?api->add_one(stable_id):0;}
// Retain the ticket and poll state on later frames. SENT only means dispatch.
