using P271BulkDestroyFn=void (*)(void*,void*);
static P271BulkDestroyFn g_p271_bulk_original=nullptr;
static uintptr_t g_p271_bulk_candidate=0;
static volatile LONG64 g_p271_bulk_invalidated=0;
static void p271_bulk_destroy_hook(void* container,void* physics) {
    InterlockedIncrement64(&g_p27_hook_calls[23]); p29_note_hook(23);
    // Allocate before taking registry locks; no game memory is read during cleanup.
    std::vector<AnymakerClientActorStateV1> snapshots;
    snapshots.reserve(CLIENT_ACTOR_REGISTRY_CAPACITY);
    uintptr_t local=(uintptr_t)InterlockedCompareExchange64(&g_local_actor_hint,0,0);
    bool clear_local=false;
    AcquireSRWLockExclusive(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        auto& entry=g_client_actor_registry[i];
        if(!entry.live || entry.container!=(uintptr_t)container) continue;
        auto state=entry.last_state;
        if(!entry.have_snapshot) {
            state={};state.struct_size=sizeof(state);state.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
            state.actor=(AnymakerActorHandle)entry.actor;state.actor_id=entry.actor_id;state.valid_fields=ANY_CLIENT_ACTOR_VALID_ID;
        }
        snapshots.push_back(state);
        p33_invalidate(p33_token(entry.p33_epoch,entry.identity.ticket.generation,size_t(i),ANY_OBJECT_ACTOR));entry.live=false;
        entry.identity.retire();
        if(local && local==entry.actor) clear_local=true;
    }
    ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
    // Conservatively invalidate the public epoch on any container bulk teardown.
    p33_retire_world();
    if(clear_local) {
        InterlockedCompareExchange64(&g_local_actor_hint,0,(LONG64)local);
        InterlockedExchange64(&g_local_scene_hint,0);
        AcquireSRWLockExclusive(&g_latest_player_lock);
        g_have_latest_player_state=false;g_latest_player_state={};
        ReleaseSRWLockExclusive(&g_latest_player_lock);
    }
    g_p29_teardowns.fetch_add(1);
    g_p271_bulk_original(container,physics);
    for(const auto& state:snapshots) {
        AnymakerClientActorLifecycleEventV1 event{};
        event.struct_size=sizeof(event);event.event_version=ANYMAKER_CLIENT_ACTOR_EVENT_VERSION;
        event.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);event.kind=ANY_CLIENT_ACTOR_DESTROYED;event.state=state;
        enqueue_client_actor_event(event);
        InterlockedIncrement64(&g_client_actor_destroyed_captured);
        InterlockedIncrement64(&g_p271_bulk_invalidated);
    }
}
