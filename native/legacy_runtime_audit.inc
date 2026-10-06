static void p27_retry_hooks() {
    uintptr_t bulk=(uintptr_t)InterlockedCompareExchangePointer((void* volatile*)&g_p271_bulk_candidate,nullptr,nullptr);
    if(bulk && !g_p271_bulk_original) install_detour("client_actor_container_bulk_destroy",bulk,BODY_CLIENT_ACTOR_BULK_DESTROY,16,(uintptr_t)&p271_bulk_destroy_hook,(void**)&g_p271_bulk_original);
    if(!g_p26_inventory_ui_original) { TargetResolution t{"p26_inventory_ui_page",BODY_P26_INVENTORY_UI,sizeof(BODY_P26_INVENTORY_UI),0,0}; if(resolve_target(t)) install_detour("p26_inventory_ui_page",t.address,BODY_P26_INVENTORY_UI,19,(uintptr_t)&p26_inventory_ui_hook,(void**)&g_p26_inventory_ui_original); }
    if(!g_client_create_original) { TargetResolution t{"client_create",BODY_CLIENT_CREATE,sizeof(BODY_CLIENT_CREATE),0,0}; if(resolve_target(t)) install_detour("client_create",t.address,BODY_CLIENT_CREATE,17,(uintptr_t)&client_create_hook,(void**)&g_client_create_original); }
    if(!g_client_remove_original) { TargetResolution t{"client_remove",BODY_CLIENT_REMOVE,sizeof(BODY_CLIENT_REMOVE),0,0}; if(resolve_target(t)) install_detour("client_remove",t.address,BODY_CLIENT_REMOVE,17,(uintptr_t)&client_remove_hook,(void**)&g_client_remove_original); }
    if(!g_server_create_original) { TargetResolution t{"server_create",BODY_SERVER_CREATE,sizeof(BODY_SERVER_CREATE),0,0}; if(resolve_target(t)) install_detour("server_create",t.address,BODY_SERVER_CREATE,17,(uintptr_t)&server_create_hook,(void**)&g_server_create_original); }
    if(!g_server_destroy_original) { TargetResolution t{"server_destroy",BODY_SERVER_DESTROY,sizeof(BODY_SERVER_DESTROY),0,0}; if(resolve_target(t)) install_detour("server_destroy",t.address,BODY_SERVER_DESTROY,20,(uintptr_t)&server_destroy_hook,(void**)&g_server_destroy_original); }
    if(!g_input_begin_original) { TargetResolution t{"input_begin",BODY_INPUT_BEGIN,sizeof(BODY_INPUT_BEGIN),0,0}; if(resolve_target(t)) install_detour("input_begin",t.address,BODY_INPUT_BEGIN,20,(uintptr_t)&input_begin_hook,(void**)&g_input_begin_original); }
    if(!g_input_end_original) { TargetResolution t{"input_end",BODY_INPUT_END,sizeof(BODY_INPUT_END),0,0}; if(resolve_target(t)) install_detour("input_end",t.address,BODY_INPUT_END,17,(uintptr_t)&input_end_hook,(void**)&g_input_end_original); }
    if(!g_use_self_original) { TargetResolution t{"server_use_self",BODY_USE_SELF,sizeof(BODY_USE_SELF),0,0}; if(resolve_target(t)) install_detour("server_use_self",t.address,BODY_USE_SELF,21,(uintptr_t)&use_self_hook,(void**)&g_use_self_original); }
    if(!g_use_actor_original) { TargetResolution t{"server_use_actor",BODY_USE_ACTOR,sizeof(BODY_USE_ACTOR),0,0}; if(resolve_target(t)) install_detour("server_use_actor",t.address,BODY_USE_ACTOR,21,(uintptr_t)&use_actor_hook,(void**)&g_use_actor_original); }
    if(!g_use_vehicle_original) { TargetResolution t{"server_use_vehicle",BODY_USE_VEHICLE,sizeof(BODY_USE_VEHICLE),0,0}; if(resolve_target(t)) install_detour("server_use_vehicle",t.address,BODY_USE_VEHICLE,29,(uintptr_t)&use_vehicle_hook,(void**)&g_use_vehicle_original); }
    if(!g_use_transform_original) { TargetResolution t{"server_use_transform",BODY_USE_TRANSFORM,sizeof(BODY_USE_TRANSFORM),0,0}; if(resolve_target(t)) install_detour("server_use_transform",t.address,BODY_USE_TRANSFORM,21,(uintptr_t)&use_transform_hook,(void**)&g_use_transform_original); }
    if(!g_player_tick_original) { TargetResolution t{"player_tick_inventory",BODY_PLAYER_TICK,sizeof(BODY_PLAYER_TICK),0,0}; if(resolve_target(t)) install_detour("player_tick_inventory",t.address,BODY_PLAYER_TICK,19,(uintptr_t)&player_tick_hook,(void**)&g_player_tick_original); }
    if(!g_vc_client_class_original) { TargetResolution t{"vc_client_class",BODY_VC_CLIENT_CLASS,sizeof(BODY_VC_CLIENT_CLASS),0,0}; if(resolve_target(t)) install_detour("vc_client_class",t.address,BODY_VC_CLIENT_CLASS,17,(uintptr_t)&vc_client_class_hook,(void**)&g_vc_client_class_original); }
    if(!g_vc_client_create_original) { TargetResolution t{"vc_client_create",BODY_VC_CLIENT_CREATE,sizeof(BODY_VC_CLIENT_CREATE),0,0}; if(resolve_target(t)) install_detour("vc_client_create",t.address,BODY_VC_CLIENT_CREATE,17,(uintptr_t)&vc_client_create_hook,(void**)&g_vc_client_create_original); }
    if(!g_vc_client_destroy_original) { TargetResolution t{"vc_client_destroy",BODY_VC_CLIENT_DESTROY,sizeof(BODY_VC_CLIENT_DESTROY),0,0}; if(resolve_target(t)) install_detour("vc_client_destroy",t.address,BODY_VC_CLIENT_DESTROY,15,(uintptr_t)&vc_client_destroy_hook,(void**)&g_vc_client_destroy_original); }
    if(!g_vc_server_class_original) { TargetResolution t{"vc_server_class",BODY_VC_SERVER_CLASS,sizeof(BODY_VC_SERVER_CLASS),0,0}; if(resolve_target(t)) install_detour("vc_server_class",t.address,BODY_VC_SERVER_CLASS,17,(uintptr_t)&vc_server_class_hook,(void**)&g_vc_server_class_original); }
    if(!g_vc_server_create_original) { TargetResolution t{"vc_server_create",BODY_VC_SERVER_CREATE,sizeof(BODY_VC_SERVER_CREATE),0,0}; if(resolve_target(t)) install_detour("vc_server_create",t.address,BODY_VC_SERVER_CREATE,16,(uintptr_t)&vc_server_create_hook,(void**)&g_vc_server_create_original); }
    if(!g_vc_server_destroy_original) { TargetResolution t{"vc_server_destroy",BODY_VC_SERVER_DESTROY,sizeof(BODY_VC_SERVER_DESTROY),0,0}; if(resolve_target(t)) install_detour("vc_server_destroy",t.address,BODY_VC_SERVER_DESTROY,17,(uintptr_t)&vc_server_destroy_hook,(void**)&g_vc_server_destroy_original); }
    if(!g_client_actor_create_original) { TargetResolution t{"client_actor_container_create_object",BODY_ACTOR_CREATE_OBJECT,sizeof(BODY_ACTOR_CREATE_OBJECT),0,0}; if(resolve_target(t)) install_detour("client_actor_container_create_object",t.address,BODY_ACTOR_CREATE_OBJECT,17,(uintptr_t)&client_actor_create_hook,(void**)&g_client_actor_create_original); }
    if(!g_client_actor_destroy_original) { TargetResolution t{"client_actor_container_destroy_object",BODY_ACTOR_DESTROY_OBJECT,sizeof(BODY_ACTOR_DESTROY_OBJECT),0,0}; if(resolve_target(t)) install_detour("client_actor_container_destroy_object",t.address,BODY_ACTOR_DESTROY_OBJECT,18,(uintptr_t)&client_actor_destroy_hook,(void**)&g_client_actor_destroy_original); }
    if(!g_controls_builder_original) { TargetResolution t{"controls_builder_prefix",BODY_CONTROLS_BUILDER_PREFIX,sizeof(BODY_CONTROLS_BUILDER_PREFIX),0,0}; if(resolve_target(t)) install_detour("controls_builder_prefix",t.address,BODY_CONTROLS_BUILDER_PREFIX,19,(uintptr_t)&controls_builder_hook,(void**)&g_controls_builder_original); }
    if(!g_ui_render_original) { TargetResolution t{"phase26_ui_render",BODY_UI_RENDER,sizeof(BODY_UI_RENDER),0,0}; if(resolve_target(t)) install_detour("phase26_ui_render",t.address,BODY_UI_RENDER,15,(uintptr_t)&ui_render_hook,(void**)&g_ui_render_original); }
    if(!g_p26_server_tick_original) { TargetResolution t{"p26_authoritative_server_tick",BODY_P26_SERVER_TICK,sizeof(BODY_P26_SERVER_TICK),0,0}; if(resolve_target(t)) install_detour("p26_authoritative_server_tick",t.address,BODY_P26_SERVER_TICK,19,(uintptr_t)&p26_server_tick,(void**)&g_p26_server_tick_original); }
}
static void p27_retry_helpers() {
    if(!g_get_transform) { TargetResolution t{"player_get_transform",BODY_GET_TRANSFORM,sizeof(BODY_GET_TRANSFORM),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_transform,(void*)t.address,nullptr); }
    if(!g_get_item_active) { TargetResolution t{"player_get_item_active",BODY_GET_ITEM_ACTIVE,sizeof(BODY_GET_ITEM_ACTIVE),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_item_active,(void*)t.address,nullptr); }
    if(!g_get_item_handheld) { TargetResolution t{"player_get_item_handheld",BODY_GET_ITEM_HANDHELD,sizeof(BODY_GET_ITEM_HANDHELD),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_item_handheld,(void*)t.address,nullptr); }
    if(!g_get_incapacitated) { TargetResolution t{"player_get_incapacitated",BODY_GET_INCAPACITATED,sizeof(BODY_GET_INCAPACITATED),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_incapacitated,(void*)t.address,nullptr); }
    if(!g_get_swimming) { TargetResolution t{"player_get_swimming",BODY_GET_SWIMMING,sizeof(BODY_GET_SWIMMING),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_swimming,(void*)t.address,nullptr); }
    if(!g_get_hunger) { TargetResolution t{"player_get_hunger",BODY_GET_HUNGER,sizeof(BODY_GET_HUNGER),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_hunger,(void*)t.address,nullptr); }
    if(!g_get_infection) { TargetResolution t{"player_get_infection",BODY_GET_INFECTION,sizeof(BODY_GET_INFECTION),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_infection,(void*)t.address,nullptr); }
    if(!g_get_oxygen) { TargetResolution t{"player_get_oxygen",BODY_GET_OXYGEN,sizeof(BODY_GET_OXYGEN),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_oxygen,(void*)t.address,nullptr); }
    if(!g_get_stamina) { TargetResolution t{"player_get_stamina",BODY_GET_STAMINA,sizeof(BODY_GET_STAMINA),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_stamina,(void*)t.address,nullptr); }
    if(!g_get_temp_cold) { TargetResolution t{"player_get_temp_cold",BODY_GET_TEMP_COLD,sizeof(BODY_GET_TEMP_COLD),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_temp_cold,(void*)t.address,nullptr); }
    if(!g_get_temp_hot) { TargetResolution t{"player_get_temp_hot",BODY_GET_TEMP_HOT,sizeof(BODY_GET_TEMP_HOT),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_temp_hot,(void*)t.address,nullptr); }
    if(!g_get_thirst) { TargetResolution t{"player_get_thirst",BODY_GET_THIRST,sizeof(BODY_GET_THIRST),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_thirst,(void*)t.address,nullptr); }
    if(!g_get_wetness) { TargetResolution t{"player_get_wetness",BODY_GET_WETNESS,sizeof(BODY_GET_WETNESS),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_get_wetness,(void*)t.address,nullptr); }
    if(!g_server_actor_get_inventory) { TargetResolution t{"server_actor_get_inventory",BODY_SERVER_ACTOR_GET_INVENTORY,sizeof(BODY_SERVER_ACTOR_GET_INVENTORY),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_server_actor_get_inventory,(void*)t.address,nullptr); }
    if(!g_server_inventory_get_item) { TargetResolution t{"server_inventory_get_item",BODY_SERVER_INVENTORY_GET_ITEM,sizeof(BODY_SERVER_INVENTORY_GET_ITEM),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_server_inventory_get_item,(void*)t.address,nullptr); }
    if(!g_ui_render_texture) { TargetResolution t{"map_ui_render_texture",BODY_UI_RENDER_TEXTURE,sizeof(BODY_UI_RENDER_TEXTURE),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_ui_render_texture,(void*)t.address,nullptr); }
    if(!g_ui_render_circle) { TargetResolution t{"map_ui_render_circle",BODY_UI_RENDER_CIRCLE,sizeof(BODY_UI_RENDER_CIRCLE),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_ui_render_circle,(void*)t.address,nullptr); }
    if(!g_ui_render_rectangle) { TargetResolution t{"ui_render_rectangle",BODY_UI_RENDER_RECTANGLE,sizeof(BODY_UI_RENDER_RECTANGLE),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_ui_render_rectangle,(void*)t.address,nullptr); }
    if(!g_renderer_create_map_textures) { TargetResolution t{"map_create_textures",BODY_RENDERER_CREATE_MAP_TEXTURES,sizeof(BODY_RENDERER_CREATE_MAP_TEXTURES),0,0}; if(resolve_target(t)) InterlockedCompareExchangePointer((void* volatile*)&g_renderer_create_map_textures,(void*)t.address,nullptr); }
    for(auto& o:P27_VIRTUAL_OWNERS) if(!o.address) { TargetResolution t{o.label,o.owner,o.owner_size,0,0};if(resolve_target(t)) InterlockedExchangePointer((void* volatile*)&o.address,(void*)t.address); }
}
static uintptr_t p27_checked_virtual(size_t slot,uintptr_t object,const unsigned char* expected,size_t size) {
    uintptr_t owner=(uintptr_t)InterlockedCompareExchangePointer((void* volatile*)&P27_VIRTUAL_OWNERS[slot].address,nullptr,nullptr);
    uintptr_t delta=0,type=0,methods=0,cell=0,fn=0;
    if(!owner || !object || !safe_read_memory(owner+P27_VIRTUAL_OWNERS[slot].dep_offset,&delta,8) || delta>0x100000 || delta%8 ||
       !safe_read_memory(object,&type,8) || !type || !safe_read_memory(type+0x80,&methods,8) || !methods ||
       !safe_read_memory(methods+delta,&cell,8) || !cell || !safe_read_memory(cell,&fn,8) || !fn) return 0;
    // ReadProcessMemory compares without dereferencing untrusted executable memory.
    unsigned char bytes[512]{};if(size>sizeof(bytes) || !safe_read_memory(fn,bytes,size) || std::memcmp(bytes,expected,size)) return 0;
    return fn;
}
static void p27_validate_actor_virtuals(void* actor) {
    if(!g_get_orientation) {uintptr_t fn=p27_checked_virtual(0,(uintptr_t)actor,BODY_GET_ORIENTATION,sizeof(BODY_GET_ORIENTATION));if(fn) InterlockedCompareExchangePointer((void* volatile*)&g_get_orientation,(void*)fn,nullptr);}
    if(!g_get_linear_velocity) {uintptr_t fn=p27_checked_virtual(1,(uintptr_t)actor,BODY_GET_LINEAR_VELOCITY,sizeof(BODY_GET_LINEAR_VELOCITY));if(fn) InterlockedCompareExchangePointer((void* volatile*)&g_get_linear_velocity,(void*)fn,nullptr);}
}
static void p27_validate_server_virtuals(void* scene) {
    if(!g_server_actor_get_by_id) {uintptr_t fn=p27_checked_virtual(2,(uintptr_t)scene+0x250,BODY_SERVER_ACTOR_BY_ID,sizeof(BODY_SERVER_ACTOR_BY_ID));if(fn) InterlockedCompareExchangePointer((void* volatile*)&g_server_actor_get_by_id,(void*)fn,nullptr);}
}
static void p27_report_hooks() {
    unsigned installed=0,exercised=0;
    for(size_t i=0;i<std::size(P27_HOOK_SPECS);++i) {
        LONG ready=InterlockedCompareExchange(&g_p27_hook_installed[i],0,0);
        LONG64 calls=InterlockedCompareExchange64(&g_p27_hook_calls[i],0,0);
        installed+=ready?1:0;exercised+=calls?1:0;
        char message[256]{};std::snprintf(message,sizeof(message),"PHASE27_HOOK_AUDIT target=%s installed=%ld calls=%lld status=%s",
            P27_HOOK_SPECS[i].label,(long)ready,(long long)calls,ready?(calls?"EXERCISED":"UNEXERCISED"):(g_p27_resolution_only?"RESOLUTION_ONLY":"UNRESOLVED_OR_REJECTED"));
        log_line(ANY_LOG_INFO,"framework",message);
    }
    char message[384]{};std::snprintf(message,sizeof(message),"PHASE27_AUDIT_SUMMARY hooks_installed=%u/%zu hooks_exercised=%u/%zu mods_loaded=%zu protection_failures=%lld mode=observer_only stability=WINDOWS_TEST_PENDING",
        installed,std::size(P27_HOOK_SPECS),exercised,std::size(P27_HOOK_SPECS),g_mods.size(),(long long)InterlockedCompareExchange64(&g_p27_protection_failures,0,0));
    log_line(ANY_LOG_INFO,"framework",message);
    std::snprintf(message,sizeof(message),"PHASE27_HELPER_AUDIT orientation=%d velocity=%d actor_by_id=%d inventory_getter=%d controls_insertion=QUARANTINED rendering=QUARANTINED inventory_api=QUARANTINED mod_actions=UNEXERCISED",
        g_get_orientation?1:0,g_get_linear_velocity?1:0,g_server_actor_get_by_id?1:0,g_server_actor_get_inventory?1:0);
    log_line(ANY_LOG_INFO,"framework",message);
}

static void p27_capture_actor_container(void* container) {
    uintptr_t fn=p27_checked_virtual(3,(uintptr_t)container,BODY_CLIENT_ACTOR_BULK_DESTROY,sizeof(BODY_CLIENT_ACTOR_BULK_DESTROY));
    if(fn) InterlockedCompareExchangePointer((void* volatile*)&g_p271_bulk_candidate,(void*)fn,nullptr);
}
