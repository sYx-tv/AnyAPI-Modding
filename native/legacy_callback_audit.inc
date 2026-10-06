#include "legacy_payload_checks.h"
static DWORD g_p272_dispatch_thread=0;
static uint64_t g_p272_last_sequence[6]{},g_p272_primary[6]{},g_p272_witness[6]{},g_p272_sequence_regressions[6]{};
static uint64_t g_p272_callback_errors[6]{},g_p272_payload_errors[6]{},g_p272_queries[6]{};
static void p272_primary(size_t i,uint64_t seq,bool valid){
    if(GetCurrentThreadId()!=g_p272_dispatch_thread)++g_p272_callback_errors[i];
    if(g_p272_last_sequence[i] && seq<=g_p272_last_sequence[i])++g_p272_sequence_regressions[i];
    g_p272_last_sequence[i]=seq;++g_p272_primary[i];if(!valid)++g_p272_payload_errors[i];
}
static void p272_witness(size_t i,uint64_t seq){++g_p272_witness[i];if(GetCurrentThreadId()!=g_p272_dispatch_thread || seq!=g_p272_last_sequence[i])++g_p272_callback_errors[i];}
static void p272_witness_item(const AnymakerItemLifecycleEventV2* e,void*){p272_witness(0,e?e->sequence:0);}
static void p272_witness_input(const AnymakerDigitalActionEventV2* e,void*){p272_witness(1,e?e->sequence:0);}
static void p272_witness_use(const AnymakerServerItemUseEventV2* e,void*){p272_witness(2,e?e->sequence:0);}
static void p272_witness_player(const AnymakerLocalPlayerStateV2* e,void*){p272_witness(3,e?e->sequence:0);}
static void p272_witness_component(const AnymakerVehicleComponentEventV1* e,void*){p272_witness(4,e?e->sequence:0);}
static void p272_witness_actor(const AnymakerClientActorLifecycleEventV1* e,void*){p272_witness(5,e?e->sequence:0);}
static void p272_report_callbacks(){
    const char* names[]={"item_lifecycle","digital_input","server_item_use","player_state","vehicle_component","client_actor"};char text[512]{};
    for(size_t i=0;i<6;++i){std::snprintf(text,sizeof(text),"PHASE272_CALLBACK kind=%s primary=%llu witness=%llu delivery_errors=%llu payload_errors=%llu sequence_regressions=%llu api_queries=%llu",names[i],(unsigned long long)g_p272_primary[i],(unsigned long long)g_p272_witness[i],(unsigned long long)g_p272_callback_errors[i],(unsigned long long)g_p272_payload_errors[i],(unsigned long long)g_p272_sequence_regressions[i],(unsigned long long)g_p272_queries[i]);log_line(ANY_LOG_INFO,"framework",text);}
    AnymakerUiApiInfoV1 ui{};AnymakerModActionApiInfoV1 actions{};AnymakerClientActorApiInfoV1 actors{};
    bool info=get_ui_api_info(&ui) && get_mod_action_api_info(&actions) && get_client_actor_api_info(&actors);
    AnymakerLocalPlayerStateV2 cached{};bool cache_present=get_local_player_state(&cached);
    std::snprintf(text,sizeof(text),"PHASE272_PUBLIC_INFO headers_ok=%d actor_live=%u local_cache_present=%d rendering_enabled=%u actions_registered=%u",info && ui.struct_size==sizeof(ui) && actions.struct_size==sizeof(actions) && actors.struct_size==sizeof(actors)?1:0,actors.registry_live,cache_present?1:0,ui.rectangle_ready,actions.registered_actions);log_line(ANY_LOG_INFO,"framework",text);
}
