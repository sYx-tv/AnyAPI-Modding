#include "../anyapi_services_v1.h"
#include "../anyapi_screen_layout_v1.h"
// Invoke inside an owned mod callback. Restore (0,0) when hidden or shutting down.
bool shift_inventory(bool visible){auto services=AnyAPI_Services();auto api=services?(const AnyScreenLayoutV1*)services->query("anyapi.screen_layout",1):nullptr;return api&&api->struct_size==sizeof(*api)&&api->version==1&&api->set_offset("ui_aligner",2,visible?-.1:0,0);}
