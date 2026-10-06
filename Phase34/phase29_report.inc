static void p29_report() {
    char message[640]{};
    for(size_t i=0;i<165;++i) {
        const auto& thread=g_p29_hook_threads[i];
        std::snprintf(message,sizeof(message),
            "PHASE33_THREAD slot=%zu target=%s first=%u last=%u calls=%llu other_thread_calls=%llu transitions=%llu worker=%u",
            i,P27_HOOK_SPECS[i].label,thread.first.load(),thread.last.load(),
            (unsigned long long)thread.calls.load(),(unsigned long long)thread.other_thread_calls.load(),
            (unsigned long long)thread.transitions.load(),(unsigned)g_p272_dispatch_thread);
        log_line(ANY_LOG_INFO,"framework",message);
    }
    std::snprintf(message,sizeof(message),
        "PHASE33_CONTRACT generations=%llu reused_addresses=%llu teardowns=%llu inventory_samples=%llu inventory_failures=%llu transform_samples=%llu transform_invalid=%llu ui_resource_changes=%llu external_mods=0 mutation=0 drawing=0 controls_insertion=0 component_metadata_resets=%llu",
        (unsigned long long)g_p29_generations.load(),(unsigned long long)g_p29_reused_addresses.load(),
        (unsigned long long)g_p29_teardowns.load(),(unsigned long long)g_p29_inventory_samples.load(),
        (unsigned long long)g_p29_inventory_failures.load(),(unsigned long long)g_p29_transform_samples.load(),
        (unsigned long long)g_p29_transform_invalid.load(),(unsigned long long)g_p29_ui_resource_changes.load(),(unsigned long long)g_p29_component_metadata_resets.load());
    log_line(ANY_LOG_INFO,"framework",message);
    P29ComponentTransform component{};
    AcquireSRWLockShared(&g_p29_component_transform_lock);component=g_p29_component_transform;ReleaseSRWLockShared(&g_p29_component_transform_lock);
    std::snprintf(message,sizeof(message),"PHASE33_COMPONENT_TRANSFORM side=%s thread=%u revision=%llu component=0x%llX world_ok=%u grid_ok=%u world=(%.6f,%.6f,%.6f) grid=(%.6f,%.6f,%.6f) attempts=%llu both_ok=%llu coverage=CREATION_ONLY",
        component.server?"server":"client",component.thread,(unsigned long long)component.revision,(unsigned long long)component.component,
        component.world_ok,component.grid_ok,component.world[0],component.world[1],component.world[2],component.grid[0],component.grid[1],component.grid[2],
        (unsigned long long)g_p29_component_transform_attempts.load(),(unsigned long long)g_p29_component_transform_ok.load());
    log_line(ANY_LOG_INFO,"framework",message);
    P29InventorySample inventory{};
    AcquireSRWLockShared(&g_p29_inventory_sample_lock);inventory=g_p29_inventory_sample;ReleaseSRWLockShared(&g_p29_inventory_sample_lock);
    std::snprintf(message,sizeof(message),"PHASE33_INVENTORY side=%s thread=%u revision=%llu inventory=0x%llX item=0x%llX definition=0x%llX readable=%u empty=%u definition_readable=%u coverage=HANDHELD_ONLY quantities=POLYMORPHIC_PENDING mutation=0",
        inventory.server?"server":"client",inventory.thread,(unsigned long long)inventory.revision,
        (unsigned long long)inventory.probe.inventory,(unsigned long long)inventory.probe.item,
        (unsigned long long)inventory.probe.definition,inventory.probe.header_readable,inventory.probe.empty,inventory.probe.definition_readable);
    log_line(ANY_LOG_INFO,"framework",message);
    P29UiSnapshot ui{};
    AcquireSRWLockShared(&g_p29_ui_snapshot_lock);ui=g_p29_ui_snapshot;ReleaseSRWLockShared(&g_p29_ui_snapshot_lock);
    // Legacy report values now belong exclusively to the worker.
    g_last_client_ui_renderer=ui.renderer;g_last_ui_origin={ui.origin[0],ui.origin[1]};g_last_ui_viewport={ui.viewport[0],ui.viewport[1]};
    std::snprintf(message,sizeof(message),"PHASE33_RENDER thread=%u revision=%llu renderer=0x%llX viewport=(%.6f,%.6f) resource_changes=%llu projection=UNRESOLVED drawing=0 map_crash=UNRESOLVED stale_player_events=%llu",
        ui.thread,(unsigned long long)ui.revision,(unsigned long long)ui.renderer,ui.viewport[0],ui.viewport[1],
        (unsigned long long)g_p29_ui_resource_changes.load(),(unsigned long long)g_p29_stale_player_events.load());
    log_line(ANY_LOG_INFO,"framework",message);
}
