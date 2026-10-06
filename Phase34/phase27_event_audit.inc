#include "phase272_callback_audit.inc"
static volatile LONG64 g_p27_audit_events[6]{},g_p27_audit_invalid[6]{};
static void p27_record_event(size_t kind,bool valid) {InterlockedIncrement64(&g_p27_audit_events[kind]);if(!valid) InterlockedIncrement64(&g_p27_audit_invalid[kind]);}
static void p27_check_item(const AnymakerItemLifecycleEventV2* e,void*) {
    g_p29_api_audit[1].functional(e!=nullptr);
    p272_primary(0,e?e->sequence:0,e && e->item && (e->side==ANY_ITEM_SIDE_CLIENT || e->side==ANY_ITEM_SIDE_SERVER) && e->kind>=ANY_ITEM_CREATED && e->kind<=ANY_ITEM_DESTROYED);
    p27_record_event(0,e && e->struct_size==sizeof(*e) && e->event_version==ANYMAKER_ITEM_LIFECYCLE_EVENT_VERSION && e->sequence &&
        e->definition_id[sizeof(e->definition_id)-1]==0 && e->definition_name[sizeof(e->definition_name)-1]==0 && e->definition_class[sizeof(e->definition_class)-1]==0);
}
static void p27_check_input(const AnymakerDigitalActionEventV2* e,void*) {
    g_p29_api_audit[2].functional(e!=nullptr);
    p272_primary(1,e?e->sequence:0,e && e->actor && (e->phase==ANY_DIGITAL_ACTION_BEGIN || e->phase==ANY_DIGITAL_ACTION_END));
    p27_record_event(1,e && e->struct_size==sizeof(*e) && e->event_version==ANYMAKER_DIGITAL_ACTION_EVENT_VERSION && e->sequence);
}
static void p27_check_use(const AnymakerServerItemUseEventV2* e,void*) {
    g_p29_api_audit[3].functional(e!=nullptr);
    p272_primary(2,e?e->sequence:0,e && p272_valid_use(*e));
    p27_record_event(2,e && e->struct_size==sizeof(*e) && e->event_version==ANYMAKER_SERVER_ITEM_USE_EVENT_VERSION && e->sequence);
}
static void p27_check_player(const AnymakerLocalPlayerStateV2* e,void*) {
    g_p29_api_audit[4].functional(e!=nullptr);
    p272_primary(3,e?e->sequence:0,e && p272_valid_player(*e));
    AnymakerLocalPlayerStateV2 cached{};if(get_local_player_state(&cached))++g_p272_queries[3];
    p27_record_event(3,e && e->struct_size==sizeof(*e) && e->state_version==ANYMAKER_LOCAL_PLAYER_STATE_VERSION && e->sequence &&
        (!(e->valid_fields&ANY_PLAYER_VALID_POSITION) || finite3(e->position)));
}
static void p27_check_component(const AnymakerVehicleComponentEventV1* e,void*) {
    g_p29_api_audit[6].functional(e!=nullptr);
    p272_primary(4,e?e->sequence:0,e && e->component && (e->side==ANY_COMPONENT_SIDE_CLIENT || e->side==ANY_COMPONENT_SIDE_SERVER) && (e->kind==ANY_COMPONENT_CREATED || e->kind==ANY_COMPONENT_DESTROYED));
    if(e){char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
        bool live=g_ctx.is_vehicle_component_live(e->component);g_p29_api_audit[7].functional(true);
        g_ctx.get_vehicle_component_vehicle(e->component);g_p29_api_audit[8].functional(true);
        size_t copied=g_ctx.copy_vehicle_component_class(e->component,cls,sizeof(cls));
        g_p29_api_audit[9].functional(copied==ANYMAKER_API_INVALID_SIZE?!cls[0]:cls[sizeof(cls)-1]==0);
        (void)live;++g_p272_queries[4];}
    p27_record_event(4,e && e->struct_size==sizeof(*e) && e->event_version==ANYMAKER_VEHICLE_COMPONENT_EVENT_VERSION && e->sequence && e->class_name[sizeof(e->class_name)-1]==0);
}
static void p27_check_actor(const AnymakerClientActorLifecycleEventV1* e,void*) {
    g_p29_api_audit[20].functional(e!=nullptr);
    p272_primary(5,e?e->sequence:0,e && p272_valid_actor(*e));
    if(e){is_client_actor_live(e->state.actor);++g_p272_queries[5];}
    p27_record_event(5,e && e->struct_size==sizeof(*e) && e->event_version==ANYMAKER_CLIENT_ACTOR_EVENT_VERSION && e->sequence && e->state.struct_size==sizeof(e->state));
}
static void p27_register_event_audit() {
    g_p272_dispatch_thread=GetCurrentThreadId();
    bool ok=register_item_lifecycle("framework.audit",p27_check_item,nullptr) &&
        register_digital_action("framework.audit",p27_check_input,nullptr) &&
        register_server_item_use("framework.audit",p27_check_use,nullptr) &&
        register_local_player_state("framework.audit",p27_check_player,nullptr) &&
        register_vehicle_component("framework.audit",p27_check_component,nullptr) &&
        register_client_actor_lifecycle("framework.audit",p27_check_actor,nullptr);
    bool rejected_null=!register_item_lifecycle("framework.audit",nullptr,nullptr) && !register_digital_action("framework.audit",nullptr,nullptr) && !register_server_item_use("framework.audit",nullptr,nullptr) && !register_local_player_state("framework.audit",nullptr,nullptr) && !register_vehicle_component("framework.audit",nullptr,nullptr) && !register_client_actor_lifecycle("framework.audit",nullptr,nullptr);
    ok=ok && rejected_null;
    ok=register_item_lifecycle("framework.audit.witness",p272_witness_item,nullptr) && ok;
    ok=register_digital_action("framework.audit.witness",p272_witness_input,nullptr) && ok;
    ok=register_server_item_use("framework.audit.witness",p272_witness_use,nullptr) && ok;
    ok=register_local_player_state("framework.audit.witness",p272_witness_player,nullptr) && ok;
    ok=register_vehicle_component("framework.audit.witness",p272_witness_component,nullptr) && ok;
    ok=register_client_actor_lifecycle("framework.audit.witness",p272_witness_actor,nullptr) && ok;
    log_line(ok?ANY_LOG_INFO:ANY_LOG_ERROR,"framework",ok?"PHASE27_EVENT_AUDIT internal_subscribers=12 external_mods=0":"PHASE27_EVENT_AUDIT registration_failed=1");
}
static void p27_report_events() {
    const char* names[]={"item_lifecycle","digital_input","server_item_use","player_state","vehicle_component","client_actor"};
    for(size_t i=0;i<6;++i) {
        char message[256]{};std::snprintf(message,sizeof(message),"PHASE27_EVENT_AUDIT kind=%s received=%lld invalid=%lld",names[i],
            (long long)InterlockedCompareExchange64(&g_p27_audit_events[i],0,0),(long long)InterlockedCompareExchange64(&g_p27_audit_invalid[i],0,0));
        log_line(ANY_LOG_INFO,"framework",message);
    }
    char message[192]{};std::snprintf(message,sizeof(message),"PHASE27_REGISTRY_AUDIT component_slots=%ld component_capacity=%ld overflows=%lld",(long)InterlockedCompareExchange(&g_component_registry_count,0,0),(long)COMPONENT_REGISTRY_CAPACITY,(long long)InterlockedCompareExchange64(&g_p27_component_registry_overflow,0,0));
    log_line(ANY_LOG_INFO,"framework",message);
}

static void p271_report_semantics() {
    char message[512]{};std::snprintf(message,sizeof(message),"PHASE27_SEMANTIC_AUDIT server_use_attempts=%lld actor_id_match=%lld inventory_getter_ok=%lld item_resolved=%lld definition_ok=%lld bulk_destroy_calls=%lld bulk_actors_invalidated=%lld",
        (long long)InterlockedCompareExchange64(&g_server_use_resolve_attempts,0,0),
        (long long)InterlockedCompareExchange64(&g_server_use_actor_id_match,0,0),
        (long long)InterlockedCompareExchange64(&g_server_use_inventory_getter_ok,0,0),
        (long long)InterlockedCompareExchange64(&g_server_use_item_resolved,0,0),
        (long long)InterlockedCompareExchange64(&g_server_use_definition_ok,0,0),
        (long long)InterlockedCompareExchange64(&g_p27_hook_calls[23],0,0),
        (long long)InterlockedCompareExchange64(&g_p271_bulk_invalidated,0,0));
    log_line(ANY_LOG_INFO,"framework",message);
}

static void p273_report_metadata(){
    char message[512]{};const char* names[]={"client","server"};
    for(size_t i=0;i<2;++i){std::snprintf(message,sizeof(message),"PHASE273_COMPONENT_METADATA side=%s attempts=%lld definition_ok=%lld definition_missing=%lld backfilled=%lld existing_match=%lld mismatch=%lld",names[i],(long long)read64(&g_p273_definition_attempts[i]),(long long)read64(&g_p273_definition_ok[i]),(long long)read64(&g_p273_definition_missing[i]),(long long)read64(&g_p273_metadata_filled[i]),(long long)read64(&g_p273_metadata_match[i]),(long long)read64(&g_p273_metadata_mismatch[i]));log_line(ANY_LOG_INFO,"framework",message);}
}

static void p28_report_metadata_details(){
    static bool printed[64]{};char message[1024]{};
    for(size_t i=0;i<64;++i)if(!printed[i] && InterlockedCompareExchange64(&g_p28_sample_ready[i],0,0)){
        const auto& sample=g_p28_samples[i];const auto& c=sample.context;
        std::snprintf(message,sizeof(message),"PHASE28_METADATA_SAMPLE side=%s reason=%s component=0x%llX definition=0x%llX previous_definition=0x%llX source=%u previous_side=%u previous_live=%u previous_vehicle=0x%llX string_data=0x%llX length=%u capacity=%u existing='%s' native='%s'",sample.side==ANY_COMPONENT_SIDE_SERVER?"server":"client",P28_REASON_NAMES[sample.reason],(unsigned long long)c.component,(unsigned long long)c.definition,(unsigned long long)c.previous_definition,c.source,c.previous_side,c.previous_live,(unsigned long long)c.previous_vehicle,(unsigned long long)sample.string_data,sample.length,sample.capacity,sample.existing,sample.native_name);
        log_line(sample.reason==P28_NAME_CONFLICT?ANY_LOG_WARN:ANY_LOG_INFO,"framework",message);printed[i]=true;
    }
    for(size_t i=0;i<2;++i)for(size_t j=0;j<P28_REASON_COUNT;++j){std::snprintf(message,sizeof(message),"PHASE28_METADATA_REASON side=%s reason=%s count=%lld",i?"server":"client",P28_REASON_NAMES[j],(long long)read64(&g_p28_reasons[i][j]));log_line(ANY_LOG_INFO,"framework",message);}
}
