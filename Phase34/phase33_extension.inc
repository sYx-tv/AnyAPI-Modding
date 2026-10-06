// Registry-only public extension. Never resolve/dereference game memory here.
static AnyExtResult p33_info(AnyExtensionInfoV1* out,size_t bytes) {
    if(!out || bytes<sizeof(*out))return ANY_EXT_BAD_ARGUMENT;
    *out={sizeof(*out),1,ANY_EXT_OBJECT_TOKENS|ANY_EXT_COPIED_ACTORS|
        ANY_EXT_COPIED_COMPONENTS|ANY_EXT_HANDHELD_CACHE|ANY_EXT_INVALIDATION_JOURNAL,
        g_p33_ready.load()?1u:0u,g_p29_world_revision.load(),0,0};
    return ANY_EXT_OK;
}
static AnyExtResult p33_enumerate(uint32_t kind,AnyObjectTokenV1* out,size_t capacity,size_t* count,size_t* total) {
    if(!count || !total || (capacity && !out) ||
        (kind!=ANY_OBJECT_ACTOR && kind!=ANY_OBJECT_COMPONENT))return ANY_EXT_BAD_ARGUMENT;
    *count=*total=0;
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    uint64_t epoch=g_p29_world_revision.load();
    if(!epoch)return ANY_EXT_UNAVAILABLE;
    auto copy=[&](const auto& entries,size_t n) {
        for(size_t i=0;i<n;++i){const auto& e=entries[i];
            if(!e.live || !e.identity.live || e.p33_epoch!=epoch || !e.identity.ticket.generation)continue;
            if(*count<capacity)out[(*count)++]=p33_token(epoch,e.identity.ticket.generation,i,kind);
            ++*total;
        }
    };
    if(kind==ANY_OBJECT_ACTOR){AcquireSRWLockShared(&g_client_actor_registry_lock);
        copy(g_client_actor_registry,size_t(g_client_actor_registry_highwater));
        ReleaseSRWLockShared(&g_client_actor_registry_lock);
    }else{AcquireSRWLockShared(&g_component_registry_lock);
        copy(g_component_registry,size_t(g_component_registry_count));
        ReleaseSRWLockShared(&g_component_registry_lock);}
    if(epoch!=g_p29_world_revision.load()){*count=*total=0;return ANY_EXT_STALE_TOKEN;}
    return *count<*total?ANY_EXT_BUFFER_TOO_SMALL:ANY_EXT_OK;
}
static AnyExtResult p33_validate(AnyObjectTokenV1 token) {
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    bool valid=false;uint64_t epoch=g_p29_world_revision.load();
    if(token.kind==ANY_OBJECT_ACTOR && p33_token_shape(token,ANY_OBJECT_ACTOR,CLIENT_ACTOR_REGISTRY_CAPACITY)) {
        AcquireSRWLockShared(&g_client_actor_registry_lock);
        if(token.object_id<=uint64_t(g_client_actor_registry_highwater))
            valid=p33_token_accepts(g_client_actor_registry[token.object_id-1],token,epoch);
        ReleaseSRWLockShared(&g_client_actor_registry_lock);
    }else if(token.kind==ANY_OBJECT_COMPONENT && p33_token_shape(token,ANY_OBJECT_COMPONENT,COMPONENT_REGISTRY_CAPACITY)) {
        AcquireSRWLockShared(&g_component_registry_lock);
        if(token.object_id<=uint64_t(g_component_registry_count))
            valid=p33_token_accepts(g_component_registry[token.object_id-1],token,epoch);
        ReleaseSRWLockShared(&g_component_registry_lock);
    }else return ANY_EXT_BAD_ARGUMENT;
    return valid && epoch==g_p29_world_revision.load()?ANY_EXT_OK:ANY_EXT_STALE_TOKEN;
}
static AnyExtResult p33_actor(AnyObjectTokenV1 token,AnyActorSnapshotV1* out,size_t bytes) {
    if(!out || bytes<sizeof(*out))return ANY_EXT_BAD_ARGUMENT;
    *out={};out->struct_size=sizeof(*out);out->version=1;
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    if(!p33_token_shape(token,ANY_OBJECT_ACTOR,CLIENT_ACTOR_REGISTRY_CAPACITY))return ANY_EXT_BAD_ARGUMENT;
    uint64_t epoch=g_p29_world_revision.load();bool valid=false,have=false;
    AcquireSRWLockShared(&g_client_actor_registry_lock);
    if(token.object_id<=uint64_t(g_client_actor_registry_highwater)) {
        const auto& e=g_client_actor_registry[token.object_id-1];valid=p33_token_accepts(e,token,epoch);have=e.have_snapshot;
        if(valid && have){out->token=token;out->actor_id=e.actor_id;out->sampled_tick=e.last_seen_ms;
            out->valid_fields=e.last_state.valid_fields;out->flags=e.last_state.flags;
            std::memcpy(out->position,e.last_state.position,sizeof(out->position));
            std::memcpy(out->orientation,e.last_state.orientation,sizeof(out->orientation));
            std::memcpy(out->linear_velocity,e.last_state.linear_velocity,sizeof(out->linear_velocity));}
    }
    ReleaseSRWLockShared(&g_client_actor_registry_lock);
    if(!valid || epoch!=g_p29_world_revision.load()){*out={};out->struct_size=sizeof(*out);out->version=1;return ANY_EXT_STALE_TOKEN;}
    return have?ANY_EXT_OK:ANY_EXT_UNAVAILABLE;
}
static AnyExtResult p33_component(AnyObjectTokenV1 token,AnyComponentSnapshotV1* out,size_t bytes) {
    if(!out || bytes<sizeof(*out))return ANY_EXT_BAD_ARGUMENT;
    *out={};out->struct_size=sizeof(*out);out->version=1;
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    if(!p33_token_shape(token,ANY_OBJECT_COMPONENT,COMPONENT_REGISTRY_CAPACITY))return ANY_EXT_BAD_ARGUMENT;
    uint64_t epoch=g_p29_world_revision.load();bool valid=false;
    AcquireSRWLockShared(&g_component_registry_lock);
    if(token.object_id<=uint64_t(g_component_registry_count)){
        const auto& e=g_component_registry[token.object_id-1];valid=p33_token_accepts(e,token,epoch);
        if(valid){out->token=token;out->side=uint32_t(e.side);out->class_name_valid=e.metadata_definition?1u:0u;
            std::strncpy(out->class_name,e.class_name,sizeof(out->class_name)-1);}}
    ReleaseSRWLockShared(&g_component_registry_lock);
    if(!valid || epoch!=g_p29_world_revision.load()){*out={};out->struct_size=sizeof(*out);out->version=1;return ANY_EXT_STALE_TOKEN;}
    return ANY_EXT_OK;
}
static AnyExtResult p33_handheld(uint32_t side,AnyHandheldSnapshotV1* out,size_t bytes) {
    if(!out || bytes<sizeof(*out) || side>1)return ANY_EXT_BAD_ARGUMENT;
    *out={};out->struct_size=sizeof(*out);out->version=1;
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    uint64_t epoch=g_p29_world_revision.load();
    AcquireSRWLockShared(&g_p33_handheld_lock);auto sample=g_p33_handheld[side];ReleaseSRWLockShared(&g_p33_handheld_lock);
    if(!epoch || sample.world_epoch!=epoch || epoch!=g_p29_world_revision.load() || !sample.readable)return ANY_EXT_UNAVAILABLE;
    *out=sample;return ANY_EXT_OK;
}
static AnyExtResult p33_changes(uint64_t cursor,AnyInvalidationV1* out,size_t capacity,size_t* count,uint64_t* next) {
    if(!count || !next || (capacity && !out))return ANY_EXT_BAD_ARGUMENT;
    *count=0;*next=cursor;
    if(!g_p33_ready.load())return ANY_EXT_UNAVAILABLE;
    AcquireSRWLockShared(&g_p33_journal_lock);
    auto result=g_p33_journal.read(cursor,out,capacity,count,next);
    ReleaseSRWLockShared(&g_p33_journal_lock);return result;
}
static const AnymakerModExtensionV1 g_p33_extension={sizeof(AnymakerModExtensionV1),1,
    p33_info,p33_enumerate,p33_validate,p33_actor,p33_component,p33_handheld,p33_changes};
#ifndef P33_EXTENSION_TEST
extern "C" __declspec(dllexport) const AnymakerModExtensionV1* AnymakerGetExtensionV1(uint32_t version,size_t bytes) {
    return version==1 && bytes>=sizeof(g_p33_extension)?&g_p33_extension:nullptr;
}
#endif
