#include "anyapi_ui_state_v1.h"
namespace ui_state_runtime {
static std::atomic<int32_t> observed{-1};static std::atomic<uint64_t> generation{1};
static bool copy(AnyUiSnapshotV1* out){
 if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;
 *out={};int32_t state=-1;
 if(!g_p34_frontend||!safe_read_memory(g_p34_frontend+0x828,&state,sizeof(state))||state<0||state>=128)return false;
 if(observed.exchange(state)!=state)++generation;
 out->native_state=state;out->kind=state==1?ANY_UI_GAMEPLAY:state==2?ANY_UI_INVENTORY:ANY_UI_MENU;return true;
}
static const AnyUiStateV1 api{sizeof(AnyUiStateV1),1,copy};
}
static const AnyUiStateV1* anyapi_ui_state_service(){return &ui_state_runtime::api;}
