struct ServerUseResolvedItem {
    uintptr_t server_actor{};
    uintptr_t server_item{};
    uintptr_t definition{};
    bool resolved=false;
    DefinitionSnapshot snapshot{};
};

static ServerUseResolvedItem resolve_server_use_item(
    void* server,
    void* peer_data,
    int32_t actor_id,
    int32_t item_id
) {
    ServerUseResolvedItem result{};
    InterlockedIncrement64(&g_server_use_resolve_attempts);

    if(!server || !g_server_actor_get_inventory || !g_server_inventory_get_item) {
        return result;
    }

    uintptr_t peer_actor_state_rpm=0;
    uintptr_t peer_actor_state_native=0;
    uintptr_t server_actor_rpm=0;
    uintptr_t server_actor_native=0;

    if(peer_data) {
        if(safe_read_memory(
            (uintptr_t)peer_data+0x698,
            &peer_actor_state_rpm,
            sizeof(peer_actor_state_rpm)
        )) {
            InterlockedIncrement64(&g_server_use_peer_state_rpm_ok);
        }

        if(safe_read_native(
            (uintptr_t)peer_data+0x698,
            &peer_actor_state_native,
            sizeof(peer_actor_state_native)
        )) {
            InterlockedIncrement64(&g_server_use_peer_state_native_ok);
        }
    }

    uintptr_t peer_actor_state=
        peer_actor_state_rpm ? peer_actor_state_rpm : peer_actor_state_native;

    if(peer_actor_state) {
        InterlockedIncrement64(&g_server_use_peer_state_nonzero);

        if(safe_read_memory(
            peer_actor_state+0x1998,
            &server_actor_rpm,
            sizeof(server_actor_rpm)
        )) {
            InterlockedIncrement64(&g_server_use_peer_actor_rpm_ok);
        }

        if(safe_read_native(
            peer_actor_state+0x1998,
            &server_actor_native,
            sizeof(server_actor_native)
        )) {
            InterlockedIncrement64(&g_server_use_peer_actor_native_ok);
        }
    }

    uintptr_t server_actor=
        server_actor_rpm ? server_actor_rpm : server_actor_native;

    if(server_actor) {
        InterlockedIncrement64(&g_server_use_peer_actor_nonzero);
        InterlockedIncrement64(&g_server_use_actor_chain_ok);
    }

    int32_t peer_actual_id=-1;
    if(server_actor && (!safe_read_memory(server_actor+0x40,&peer_actual_id,sizeof(peer_actual_id)) || peer_actual_id!=actor_id)) server_actor=0;

    // Local/offline worlds can deliver the event through a peer object whose
    // actor link is not populated at the exact hook instant. Fall back to the
    // authoritative server scene's actor container using event.actor_id.
    if(!server_actor && g_server_actor_get_by_id) {
        InterlockedIncrement64(&g_server_use_actor_fallback_attempts);

        uintptr_t server_scene=0;
        if(safe_read_native(
            (uintptr_t)server+0xA0,
            &server_scene,
            sizeof(server_scene)
        ) && server_scene) {
            uintptr_t actor_container=server_scene+0x250;
            uintptr_t found=0;
            bool call_ok=seh_server_actor_get_by_id(
                g_server_actor_get_by_id,
                &found,
                (void*)actor_container,
                &actor_id
            );

            if(call_ok && found) {
                server_actor=found;
                InterlockedIncrement64(&g_server_use_actor_fallback_ok);
            }
        }
    }

    if(!server_actor) {
        LONG64 probe_index=InterlockedIncrement64(&g_server_use_probe_logs);
        if(probe_index<=4) {
            std::string m=
                "PHASE27_SERVER_CHAIN_SAMPLE peer="+hexptr((uintptr_t)peer_data)+
                " peer_state_rpm="+hexptr(peer_actor_state_rpm)+
                " peer_state_native="+hexptr(peer_actor_state_native)+
                " actor_rpm="+hexptr(server_actor_rpm)+
                " actor_native="+hexptr(server_actor_native)+
                " actor_id="+std::to_string(actor_id)+
                " item_id="+std::to_string(item_id);
            log_line(ANY_LOG_WARN,"framework",m.c_str());
        }
        return result;
    }

    result.server_actor=server_actor;
    InterlockedIncrement64(&g_server_use_actor_resolved);

    int32_t actual_actor_id=-1;
    bool actor_id_read=
        safe_read_native(server_actor+0x40,&actual_actor_id,sizeof(actual_actor_id)) ||
        safe_read_memory(server_actor+0x40,&actual_actor_id,sizeof(actual_actor_id));

    if(!actor_id_read || actual_actor_id!=actor_id) {
        return result;
    }
    InterlockedIncrement64(&g_server_use_actor_id_match);

    uintptr_t inventory=0;
    bool inventory_call_ok=seh_server_actor_get_inventory(
        g_server_actor_get_inventory,
        &inventory,
        (void*)server_actor
    ) && inventory!=0;

    if(!inventory_call_ok) return result;
    InterlockedIncrement64(&g_server_use_inventory_getter_ok);

    if(inventory==server_actor+0x780)
        InterlockedIncrement64(&g_server_use_inventory_offset_match);
    p29_capture_inventory(inventory,true);

    uintptr_t item_ref=0;
    uintptr_t grid_container=0;
    uintptr_t grid_element=0;
    bool lookup_call_ok=seh_server_inventory_get_item(
        g_server_inventory_get_item,
        &item_ref,
        (void*)inventory,
        &item_id,
        &grid_container,
        &grid_element
    );

    if(!lookup_call_ok) return result;
    InterlockedIncrement64(&g_server_use_lookup_call_ok);

    // replication.master.container_ref_virtual<T>.get is exactly:
    //     return *(T**)((uint8_t*)ref + 0x20);
    // All three runtime instantiations are byte-identical, so using the proven
    // layout removes an unnecessary ambiguous code-address dependency.
    uintptr_t item=0;
    if(item_ref && (
        safe_read_native(item_ref+0x20,&item,sizeof(item)) ||
        safe_read_memory(item_ref+0x20,&item,sizeof(item))
    )) {
        InterlockedIncrement64(&g_server_use_item_ref_direct_ok);
    }

    if(!item) return result;

    result.server_item=item;
    result.resolved=true;
    InterlockedIncrement64(&g_server_use_item_resolved);

    uintptr_t definition=0;
    if((safe_read_native(item+0x88,&definition,sizeof(definition)) ||
        safe_read_memory(item+0x88,&definition,sizeof(definition))) &&
       definition) {
        result.definition=definition;
        result.snapshot=capture_definition_snapshot(definition);

        if(result.snapshot.readable) {
            InterlockedIncrement64(&g_server_use_definition_ok);

            AcquireSRWLockShared(&g_latest_player_lock);
            if(g_have_latest_player_state) {
                if(result.snapshot.id[0] &&
                   std::strcmp(
                       result.snapshot.id,
                       g_latest_player_state.active_item.definition_id
                   )==0) {
                    InterlockedIncrement64(&g_server_use_local_active_match);
                }
                if(result.snapshot.id[0] &&
                   std::strcmp(
                       result.snapshot.id,
                       g_latest_player_state.handheld_item.definition_id
                   )==0) {
                    InterlockedIncrement64(&g_server_use_local_handheld_match);
                }
            }
            ReleaseSRWLockShared(&g_latest_player_lock);
        }
    }

    return result;
}
