#include "anyapi_services_v1.h"
#include "anyapi_session_v1.h"
bool can_show_add(){auto services=AnyAPI_Services();auto api=services?(const AnySessionV1*)services->query("anyapi.session",1):nullptr;AnySessionStateV1 state;return api&&api->struct_size==sizeof(*api)&&api->version==1&&api->copy(&state)&&(state.mode==ANY_MODE_SANDBOX||state.mode==ANY_MODE_CREATIVE);}
