// Called from any API thread: only copied state under the registry lock.
// Never seed entries or prune native objects from the framework worker.
static bool get_client_actor_state(AnymakerActorHandle actor,AnymakerClientActorStateV1* out) {
    if(!out) return false;
    InterlockedIncrement64(&g_client_actor_state_queries);
    AcquireSRWLockShared(&g_client_actor_registry_lock);
    bool ok=p29_copy_live_snapshot(g_client_actor_registry,
        (size_t)g_client_actor_registry_highwater,(uintptr_t)actor,out);
    ReleaseSRWLockShared(&g_client_actor_registry_lock);
    return ok;
}

static size_t enumerate_client_actors(AnymakerClientActorEnumerateCallback callback,void* user_data) {
    if(!callback) return 0;
    std::vector<AnymakerClientActorStateV1> snapshots;
    snapshots.reserve(CLIENT_ACTOR_REGISTRY_CAPACITY);
    AcquireSRWLockShared(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        const auto& entry=g_client_actor_registry[i];
        if(entry.live && entry.have_snapshot && entry.identity.live) snapshots.push_back(entry.last_state);
    }
    ReleaseSRWLockShared(&g_client_actor_registry_lock);
    size_t emitted=0;
    for(const auto& state:snapshots) {
        ++emitted;
        if(!seh_client_actor_enumerate_callback(callback,&state,user_data)) break;
    }
    return emitted;
}
