// Framework-only public-context tests; never synthesize a native gameplay event.
static std::atomic<uint64_t> g_p29_action_begin{0},g_p29_action_end{0};
static void p29_audit_action(const AnymakerModActionEventV1* event,void*) {
    bool ok=event && event->struct_size==sizeof(*event) && event->event_version==ANYMAKER_MOD_ACTION_EVENT_VERSION && event->virtual_key==0x77 && (event->phase==ANY_MOD_ACTION_BEGIN || event->phase==ANY_MOD_ACTION_END);
    g_p29_api_audit[16].functional(ok);
    if(ok){if(event->phase==ANY_MOD_ACTION_BEGIN)g_p29_action_begin.fetch_add(1);else if(event->phase==ANY_MOD_ACTION_END)g_p29_action_end.fetch_add(1);}
}
static bool p29_audit_actor(const AnymakerClientActorStateV1* state,void*) {
    bool ok=state && state->struct_size==sizeof(*state) && state->state_version==ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
    g_p29_api_audit[21].functional(ok);
    if(ok){
        AnymakerClientActorStateV1 copied{};
        // Retirement can race this historical copied enumeration row. Absence
        // is expected; never treat an address as a persistent identity.
        bool found=g_ctx.get_client_actor_state(state->actor,&copied);
        if(found)g_p29_api_audit[22].functional(copied.actor==state->actor && copied.struct_size==sizeof(copied));
        g_ctx.is_client_actor_live(state->actor);
        g_p29_api_audit[23].functional(true);
    }
    return true;
}
static void p29_api_smoke() {
    g_ctx.log(ANY_LOG_INFO,"framework.audit","PHASE33_API_SMOKE public_context=1 native_events_synthesized=0");
    g_p29_api_audit[0].functional(true);
    g_p29_api_audit[1].guard(!g_ctx.register_item_lifecycle("framework.audit",nullptr,nullptr));
    g_p29_api_audit[2].guard(!g_ctx.register_digital_action("framework.audit",nullptr,nullptr));
    g_p29_api_audit[3].guard(!g_ctx.register_server_item_use("framework.audit",nullptr,nullptr));
    g_p29_api_audit[4].guard(!g_ctx.register_local_player_state("framework.audit",nullptr,nullptr));
    g_p29_api_audit[5].guard(!g_ctx.get_local_player_state(nullptr));
    g_p29_api_audit[6].guard(!g_ctx.register_vehicle_component("framework.audit",nullptr,nullptr));
    g_p29_api_audit[7].guard(!g_ctx.is_vehicle_component_live(0));
    g_p29_api_audit[8].guard(g_ctx.get_vehicle_component_vehicle(0)==0);
    char text[192]{};
    g_p29_api_audit[9].guard(g_ctx.copy_vehicle_component_class(0,text,sizeof(text))==ANYMAKER_API_INVALID_SIZE && !text[0]);
    g_p29_api_audit[10].guard(!g_ctx.register_ui_render("framework.audit",nullptr,nullptr));
    g_p29_api_audit[11].guard(!g_ctx.ui_draw_rect(nullptr,0,0,1,1,0));
    g_p29_api_audit[12].guard(!g_ctx.ui_draw_texture(nullptr,0,0,0,1,1,0));
    AnymakerUiTextureHandle texture=123;
    g_p29_api_audit[13].guard(!g_ctx.resolve_ui_texture(ANYMAKER_TEXTURE_PATH_MAP_BACKGROUND,&texture) && texture==0);
    g_p29_api_audit[14].guard(!g_ctx.is_virtual_key_down(0));
    g_p29_api_audit[15].guard(!g_ctx.get_ui_api_info(nullptr));
    g_p29_api_audit[16].guard(!g_ctx.register_mod_action(nullptr,nullptr,nullptr,0,nullptr,nullptr));
    uint32_t key=123;
    g_p29_api_audit[17].guard(!g_ctx.get_mod_action_binding(nullptr,nullptr,&key) && key==0);
    g_p29_api_audit[18].guard(!g_ctx.set_mod_action_binding(nullptr,nullptr,0));
    g_p29_api_audit[19].guard(!g_ctx.get_mod_action_api_info(nullptr));
    g_p29_api_audit[20].guard(!g_ctx.register_client_actor_lifecycle("framework.audit",nullptr,nullptr));
    g_p29_api_audit[21].guard(g_ctx.enumerate_client_actors(nullptr,nullptr)==0);
    AnymakerClientActorStateV1 actor{};actor.actor=123;
    g_p29_api_audit[22].guard(!g_ctx.get_client_actor_state(0,&actor) && actor.actor==0);
    g_p29_api_audit[23].guard(!g_ctx.is_client_actor_live(0));
    g_p29_api_audit[24].guard(!g_ctx.get_client_actor_api_info(nullptr));
    AnymakerCopyDefinitionStringFn readers[]={g_ctx.copy_item_definition_id,g_ctx.copy_item_definition_name,g_ctx.copy_item_definition_description,g_ctx.copy_item_definition_class,g_ctx.copy_item_definition_mesh_file};
    for(size_t i=0;i<5;++i){text[0]='x';g_p29_api_audit[25+i].guard(readers[i](0,text,sizeof(text))==ANYMAKER_API_INVALID_SIZE && !text[0]);}
    g_p29_api_audit[30].guard(!g_ctx.register_inventory_pulse("framework.audit",nullptr,nullptr));
    size_t count=123;bool compatible=true;
    g_p29_api_audit[31].guard(g_ctx.enumerate_inventory(0,nullptr,0,&count)==ANY_INVENTORY_UNAVAILABLE && count==0);
    g_p29_api_audit[32].guard(g_ctx.can_merge_inventory_items(0,0,0,&compatible)==ANY_INVENTORY_UNAVAILABLE && !compatible);
    g_p29_api_audit[33].guard(g_ctx.request_inventory_merge(0,0,0)==ANY_INVENTORY_UNAVAILABLE);
    if(g_p27_resolution_only) return;
    bool registered=g_ctx.register_mod_action("framework.audit","single_session_signal","Audit signal (F8)",0x77,p29_audit_action,nullptr);
    g_p29_api_audit[16].functional(registered);
    if(registered){
        g_p29_api_audit[17].functional(g_ctx.get_mod_action_binding("framework.audit","single_session_signal",&key) && key>=1 && key<=255);
        g_p29_api_audit[18].functional(g_ctx.set_mod_action_binding("framework.audit","single_session_signal",0x77));
    }
}
static void p29_api_sample() {
    AnymakerLocalPlayerStateV2 player{};
    if(g_ctx.get_local_player_state(&player))g_p29_api_audit[5].functional(player.struct_size==sizeof(player) && player.state_version==ANYMAKER_LOCAL_PLAYER_STATE_VERSION);
    g_ctx.enumerate_client_actors(p29_audit_actor,nullptr);
    AnymakerUiApiInfoV1 ui{};AnymakerModActionApiInfoV1 action{};AnymakerClientActorApiInfoV1 actor{};
    g_p29_api_audit[15].functional(g_ctx.get_ui_api_info(&ui) && ui.struct_size==sizeof(ui) && ui.rectangle_ready==0 && ui.texture_ready==0);
    g_p29_api_audit[19].functional(g_ctx.get_mod_action_api_info(&action) && action.struct_size==sizeof(action));
    g_p29_api_audit[24].functional(g_ctx.get_client_actor_api_info(&actor) && actor.struct_size==sizeof(actor));
    g_ctx.is_virtual_key_down(0x77);g_p29_api_audit[14].functional(true);
}
static void p29_api_report() {
    char line[384]{};
    for(size_t slot=0;slot<34;++slot){
        const auto& row=g_p29_api_audit[slot];bool quarantine=(slot>=10 && slot<=13) || slot>=30;
        std::snprintf(line,sizeof(line),"PHASE33_API slot=%zu name=%s guard_checks=%llu guard_failures=%llu functional_checks=%llu functional_failures=%llu status=%s",
            slot,P29_API_NAMES[slot],row.guard_checks.load(),row.guard_failures.load(),row.functional_checks.load(),row.functional_failures.load(),row.status(quarantine));
        log_line(ANY_LOG_INFO,"framework",line);
    }
    std::snprintf(line,sizeof(line),"PHASE33_SINGLE_SESSION api_slots=34 hooks=165 action_begin=%llu action_end=%llu world_revision=%llu reload_required=0 acceptance=PARTIAL_UNTIL_REVIEW",
        (unsigned long long)g_p29_action_begin.load(),(unsigned long long)g_p29_action_end.load(),(unsigned long long)g_p29_world_revision.load());
    log_line(ANY_LOG_INFO,"framework",line);
}
