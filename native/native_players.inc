#include "anyapi_players_v1.h"
static void inventory_actions_tick(void*,void*,void*);
static void anyapi_client_tasks_tick(uint64_t);
#include "native_player_patterns.h"
#include "anyapi_player_heading.h"
// Only this producer is activated. It forwards the real native call exactly once.
using P34MapIterFn=void(*)(uintptr_t*,void*);
using P34MapValueFn=void(*)(uintptr_t*,void*,const uintptr_t*);
using P34ActorLookupFn=void(*)(uintptr_t*,void*,const int32_t*);
static PlayerTickFn g_p34_original{};
static player_heading::NativeMath g_p34_math{};static uintptr_t g_p34_view_slot=UINTPTR_MAX;
static uintptr_t g_p34_peer_tick{},g_p34_actor_slot{},g_p34_transform_slot{},g_p34_orientation_slot{};
static uintptr_t g_p34_peer_lookup_slot{},g_p34_prop_s32_slot{},g_p34_frontend{};
static bool g_p34_peer_contracts_ready{};
static P34MapIterFn g_p34_begin{},g_p34_end{};
static P34MapValueFn g_p34_value{},g_p34_next{};
static HANDLE g_p34_channel_handle{};
static AnyPlayersChannelV1* g_p34_channel{};
static volatile LONG g_p34_faulted{};
static uint64_t g_p34_last_tick{},g_p34_last_scene{},g_p34_epoch{1},g_p34_samples{};
static uint32_t g_p34_last_count=UINT32_MAX;
static SRWLOCK g_p34_publish_lock=SRWLOCK_INIT;
// Check that a native function address points into committed, executable, unguarded memory.
static bool p34_executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address&&VirtualQuery((void*)address,&info,sizeof(info))&&info.State==MEM_COMMIT&&
      (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))&&!(info.Protect&PAGE_GUARD);
}
// Read a native dependency cell and validate the resolved executable function pointer.
static bool p34_cell(uintptr_t owner,size_t offset,uintptr_t& target) {
    uintptr_t cell{};return safe_read_memory(owner+offset,&cell,8)&&cell&&
      safe_read_memory(cell,&target,8)&&target&&p34_executable(target);
}
// Native borrowed strings use capacity zero. Bound reads by length and validate UTF-8;
// nonzero owned capacities must cover the length. Never truncate a native name.
static bool p34_copy_string(uintptr_t object,char* out,size_t capacity) {
    GeoString16 value{};if(!capacity)return false;out[0]=0;
    if(!safe_read_memory(object,&value,sizeof(value))||value.length>4096||
       (value.capacity&&value.capacity<value.length)||value.capacity>1024*1024||(!value.data&&value.length))return false;
    if(value.length>=capacity)return false; // Never emit a broken/truncated UTF-8 name.
    if(value.length&&!safe_read_memory(value.data,out,value.length))return false;
    out[value.length]=0;
    return !value.length||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out,int(value.length),nullptr,0)>0;
}
// Publish copied players and inventory visibility flags under the shared channel sequence counter.
static void p34_publish(const AnySessionPlayersV1& value) {
    if(!g_p34_channel)return;
    InterlockedIncrement64(&g_p34_channel->sequence);
    MemoryBarrier();g_p34_channel->snapshot=value;
    int32_t ui_state=-1;g_p34_channel->reserved=0;
    if(g_p34_frontend&&safe_read_memory(g_p34_frontend+0x828,&ui_state,4)&&ui_state>=0&&ui_state<128)
        g_p34_channel->reserved=2u|(ui_state==2?1u:0u);
    MemoryBarrier();
    InterlockedIncrement64(&g_p34_channel->sequence);
}
// Resolve a native virtual function through the validated class/table layout and require executable memory.
static bool p34_virtual(uintptr_t object,uintptr_t slot,uintptr_t& fn) {
    uintptr_t klass{},table{},cell{};
    return safe_read_memory(object,&klass,8)&&klass&&safe_read_memory(klass+0x80,&table,8)&&table&&
      safe_read_memory(table+slot,&cell,8)&&cell&&safe_read_memory(cell,&fn,8)&&p34_executable(fn);
}
// Compare a native function against its exact expected byte body before relying on its contract.
template<size_t N> static bool p34_body(uintptr_t fn,const unsigned char(&expected)[N]) {
    unsigned char actual[N]{};return safe_read_memory(fn,actual,N)&&!memcmp(actual,expected,N);
}
// Validate actor identity and transform/orientation bodies, then copy finite position and facing fields.
static void p34_actor_state(uintptr_t actor,int32_t actor_id,AnySessionPlayerV1* p) {
    int32_t id_check{};
    if(!actor||!safe_read_memory(actor+8,&id_check,4)||id_check!=actor_id)return;
    uintptr_t fn{};double transform[12]{};
    if(!p34_virtual(actor,g_p34_transform_slot,fn)||
       !(p34_body(fn,P34_BASE_TRANSFORM)||p34_body(fn,P34_CHARACTER_TRANSFORM)))return;
    using TransformFn=void(*)(double*,void*);((TransformFn)fn)(transform,(void*)actor);
    memcpy(p->position,transform+9,sizeof(p->position));
    if(!std::isfinite(p->position[0])||!std::isfinite(p->position[1])||!std::isfinite(p->position[2]))return;
    p->valid_fields|=PLAYER_POSITION;
    if(g_p34_view_slot!=UINTPTR_MAX&&p34_virtual(actor,g_p34_view_slot,fn)&&p34_body(fn,P34_CHARACTER_VIEW)){
        if(player_heading::view_yaw((void*)actor,(player_heading::ViewFn)fn,p->yaw_radians,g_p34_math))p->valid_fields|=PLAYER_FACING;}

}
// Keep SEH in a POD-only leaf, separate from C++ objects/destructors.
// Read the Steam peer roster, associate replicated controlled actor IDs, and collect at most 64 players with local fallback; catch native faults.
static bool p34_native_sample(void* client,void* scene,AnySessionPlayersV1* result) {
    __try {
        uintptr_t peers=(uintptr_t)client+0x28,actor_container=(uintptr_t)scene+0x158;
        uintptr_t klass{},table{},cell{},fn{};
        if(!safe_read_memory(actor_container,&klass,8)||!klass||
           !safe_read_memory(klass+0x80,&table,8)||!table||
           !safe_read_memory(table+g_p34_actor_slot,&cell,8)||!cell||
           !safe_read_memory(cell,&fn,8)||!p34_executable(fn))return false;
        unsigned char actual[sizeof(P34_ACTOR_BY_ID)]{};
        if(!safe_read_memory(fn,actual,sizeof(actual))||memcmp(actual,P34_ACTOR_BY_ID,sizeof(actual)))return false;
        auto lookup=(P34ActorLookupFn)fn;
        uintptr_t iter{},end{},local{};int32_t local_peer_id=-1;bool local_seen=false;
        safe_read_memory((uintptr_t)client+0x68,&local_peer_id,4);
        if(!safe_read_memory(actor_container+0x30,&local,8))return false;
        if(!local)safe_read_memory(actor_container+0x38,&local,8);
        g_p34_begin(&iter,(void*)peers);g_p34_end(&end,(void*)peers);
        for(unsigned visited=0;iter!=end&&visited<256;++visited) {
            uintptr_t peer{},next{},actor{};int32_t actor_id{},peer_id{};
            g_p34_value(&peer,(void*)peers,&iter);
            if(peer&&safe_read_memory(peer+0x48,&actor_id,4)) {
                    AnySessionPlayerV1 p{};p.actor_id=actor_id;
                    if(p34_copy_string(peer+0x10,p.steam_name,sizeof(p.steam_name))&&p.steam_name[0])p.valid_fields|=PLAYER_NAME;
                    if(p34_copy_string(peer+0x20,p.steam_id,sizeof(p.steam_id))&&p.steam_id[0])p.valid_fields|=PLAYER_STEAM_ID;
                    if(actor_id!=-1)lookup(&actor,(void*)actor_container,&actor_id);
                    // Object ID is corroborated by the ID used by the real voice-position path.
                    safe_read_memory(peer+8,&peer_id,4);p.peer_id=peer_id;
                    // Voice actor IDs are often -1. The replicated peer data owns the controlled actor ID.
                    if(g_p34_peer_contracts_ready){uintptr_t get_peer{},data{},getter{};
                        if(p34_virtual((uintptr_t)scene+0x40,g_p34_peer_lookup_slot,get_peer)&&p34_body(get_peer,P34_PEER_BY_ID)){
                            ((P34ActorLookupFn)get_peer)(&data,(void*)((uintptr_t)scene+0x40),&peer_id);
                            if(data&&p34_virtual(data+0xb90,g_p34_prop_s32_slot,getter)&&p34_body(getter,P34_PROP_S32_GET)&&safe_read_memory(data+0xb98,&actor_id,4)){
                                p.actor_id=actor_id;actor=0;if(actor_id!=-1)lookup(&actor,(void*)actor_container,&actor_id);
                            }
                        }
                    }
                    if(peer_id==local_peer_id||(actor&&actor==local)){p.valid_fields|=PLAYER_LOCAL;local_seen=true;}
                    p34_actor_state(actor,actor_id,&p);
                    if(result->count<ANYAPI_PLAYERS_CAPACITY)result->players[result->count++]=p;
                    else {if(p.valid_fields&PLAYER_LOCAL)result->players[ANYAPI_PLAYERS_CAPACITY-1]=p;++result->dropped;}
            }
            g_p34_next(&next,(void*)peers,&iter);
            if(next==iter)return false;iter=next;
        }
        if(iter!=end)return false;
        // The local client has its own peer data even when the remote roster is empty.
        if(local&&!local_seen){AnySessionPlayerV1 p{};p.peer_id=local_peer_id;p.valid_fields=PLAYER_LOCAL;
            if(safe_read_memory(local+8,&p.actor_id,4))p34_actor_state(local,p.actor_id,&p);
            if(p34_copy_string((uintptr_t)client+0x70,p.steam_name,sizeof(p.steam_name))&&p.steam_name[0])p.valid_fields|=PLAYER_NAME;
            if(p34_copy_string((uintptr_t)client+0x80,p.steam_id,sizeof(p.steam_id))&&p.steam_id[0])p.valid_fields|=PLAYER_STEAM_ID;
            if(result->count<ANYAPI_PLAYERS_CAPACITY)result->players[result->count++]=p;else {result->players[ANYAPI_PLAYERS_CAPACITY-1]=p;++result->dropped;}}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Forward the original game tick once, then capture players at most every 50 ms; disable capture on failure while retaining original forwarding.
static void p34_player_hook(void* actor,void* client,void* scene,
                           const double* tick,const double* delta,const void* camera) {
    g_p34_original(actor,client,scene,tick,delta,camera);
    if(g_p34_faulted||!client||!scene)return;
    inventory_actions_tick(client,scene,actor);
    const uint64_t now=GetTickCount64();
    if(!TryAcquireSRWLockExclusive(&g_p34_publish_lock))return;
    if(now-g_p34_last_tick<50){ReleaseSRWLockExclusive(&g_p34_publish_lock);return;}
    g_p34_last_tick=now;
    if(g_p34_last_scene!=(uintptr_t)scene){g_p34_last_scene=(uintptr_t)scene;++g_p34_epoch;}
    AnySessionPlayersV1 sample{};sample.sampled_tick=now;sample.world_epoch=g_p34_epoch;
    bool ok=p34_native_sample(client,scene,&sample);
    if(!ok){InterlockedExchange(&g_p34_faulted,1);sample={};
        log_line(ANY_LOG_ERROR,"framework","PHASE34_PLAYERS capture=DISABLED_NATIVE_VALIDATION_FAILURE original_forwarding=CONTINUES");}
    p34_publish(sample);++g_p34_samples;
    if(g_p34_samples==1||sample.count!=g_p34_last_count||g_p34_samples%1200==0){char msg[768]{};
        std::snprintf(msg,sizeof(msg),"PHASE34_PLAYERS copied=%u dropped=%u samples=%llu epoch=%llu outcome=GAMEPLAY_PENDING",sample.count,sample.dropped,(unsigned long long)g_p34_samples,(unsigned long long)g_p34_epoch);
        log_line(ANY_LOG_INFO,"framework",msg);
        for(uint32_t i=0;i<sample.count;++i){const auto& p=sample.players[i];
            std::snprintf(msg,sizeof(msg),"PHASE34_PLAYER peer=%d actor=%d fields=%u local=%d name=%s steam_id=%s position=(%.3f,%.3f,%.3f) yaw_radians=%.6f",
                p.peer_id,p.actor_id,p.valid_fields,(p.valid_fields&PLAYER_LOCAL)?1:0,
                (p.valid_fields&PLAYER_NAME)?p.steam_name:"UNAVAILABLE",(p.valid_fields&PLAYER_STEAM_ID)?p.steam_id:"UNAVAILABLE",
                p.position[0],p.position[1],p.position[2],p.yaw_radians);log_line(ANY_LOG_INFO,"framework",msg);}
        g_p34_last_count=sample.count;}
    ReleaseSRWLockExclusive(&g_p34_publish_lock);
    if (ok) anyapi_client_tasks_tick(sample.world_epoch);
}
// Resolve generic player contracts, create the copied-data channel and install the player observer.
static DWORD WINAPI p34_players_worker(void*) {
    TargetResolution view_owner{"phase34_view_owner",P34_VIEW_OWNER,sizeof(P34_VIEW_OWNER),0,0};
    TargetResolution math_owner{"phase34_camera_math",P34_CAMERA_MATH,sizeof(P34_CAMERA_MATH),0,0};
    TargetResolution parent{"phase34_peer_tick",P34_PEER_TICK,sizeof(P34_PEER_TICK),0,0};
    TargetResolution player{"player_tick_inventory",BODY_PLAYER_TICK,sizeof(BODY_PLAYER_TICK),0,0};
    TargetResolution orientation{"phase34_orientation_owner",P34_ORIENTATION_OWNER,sizeof(P34_ORIENTATION_OWNER),0,0};
    TargetResolution authority{"phase34_peer_authority_owner",P34_AUTHORITY_OWNER,sizeof(P34_AUTHORITY_OWNER),0,0};
    TargetResolution update{"phase34_update_owner",P34_UPDATE_OWNER,sizeof(P34_UPDATE_OWNER),0,0};
    for(;;){
        if(!g_p34_original&&resolve_target(parent)&&resolve_target(player)&&resolve_target(orientation)&&resolve_target(authority)&&resolve_target(update)&&resolve_target(math_owner)&&resolve_target(view_owner)) {
            uintptr_t begin{},end{},value{},next{},rotation_y{},transform_vector{};
            bool ready=safe_read_memory(view_owner.address+0x410,&g_p34_view_slot,8)&&g_p34_view_slot%8==0&&g_p34_view_slot<0x10000&&p34_cell(math_owner.address,0x11a0,rotation_y)&&p34_cell(math_owner.address,0x11d8,transform_vector)&&p34_cell(parent.address,P34_BEGIN_CELL,begin)&&p34_cell(parent.address,P34_END_CELL,end)&&
              p34_cell(parent.address,P34_VALUE_CELL,value)&&p34_cell(parent.address,P34_NEXT_CELL,next)&&
              safe_read_memory(parent.address+P34_ACTOR_SLOT_CELL,&g_p34_actor_slot,8)&&g_p34_actor_slot%8==0&&g_p34_actor_slot<0x10000&&
              safe_read_memory(parent.address+P34_TRANSFORM_SLOT_CELL,&g_p34_transform_slot,8)&&g_p34_transform_slot%8==0&&g_p34_transform_slot<0x10000&&
              safe_read_memory(orientation.address+P34_ORIENTATION_SLOT_CELL,&g_p34_orientation_slot,8)&&g_p34_orientation_slot%8==0&&g_p34_orientation_slot<0x10000&&
              safe_read_memory(authority.address+P34_PEER_LOOKUP_SLOT_CELL,&g_p34_peer_lookup_slot,8)&&g_p34_peer_lookup_slot%8==0&&g_p34_peer_lookup_slot<0x10000&&
              safe_read_memory(authority.address+P34_PROP_S32_SLOT_CELL,&g_p34_prop_s32_slot,8)&&g_p34_prop_s32_slot%8==0&&g_p34_prop_s32_slot<0x10000&&
              safe_read_memory(update.address+P34_FRONTEND_CELL,&g_p34_frontend,8)&&g_p34_frontend;
            if(ready){g_p34_math={(player_heading::RotationFn)rotation_y,(player_heading::TransformFn)transform_vector};log_line(ANY_LOG_INFO,"framework","PHASE34_HEADING native_math=READY source=actor.get_view_transform forward=interaction_ray_positive_z");g_p34_peer_contracts_ready=true;g_p34_begin=(P34MapIterFn)begin;g_p34_end=(P34MapIterFn)end;
                g_p34_value=(P34MapValueFn)value;g_p34_next=(P34MapValueFn)next;g_p34_peer_tick=parent.address;
                wchar_t name[80]{};std::swprintf(name,80,L"Local\\AnyAPI.Players.%lu",GetCurrentProcessId());
                if(!g_p34_channel_handle)g_p34_channel_handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(AnyPlayersChannelV1),name);
                if(g_p34_channel_handle&&!g_p34_channel)g_p34_channel=(AnyPlayersChannelV1*)MapViewOfFile(g_p34_channel_handle,FILE_MAP_WRITE,0,0,sizeof(AnyPlayersChannelV1));
                if(g_p34_channel)*g_p34_channel=AnyPlayersChannelV1{};
                if(g_p34_channel)install_detour("player_tick_inventory",player.address,BODY_PLAYER_TICK,P34_PLAYER_OVERWRITE,(uintptr_t)&p34_player_hook,(void**)&g_p34_original);
            }
        }
        Sleep(5000);
    }
}

// Copy a coherent player snapshot for any in-process plugin; never expose native objects.
static bool p34_copy_snapshot(AnySessionPlayersV1* out,uint32_t* flags){
 if(!out||!flags)return false;AcquireSRWLockShared(&g_p34_publish_lock);
 if(!g_p34_channel){ReleaseSRWLockShared(&g_p34_publish_lock);return false;}
 *out=g_p34_channel->snapshot;*flags=g_p34_channel->reserved;ReleaseSRWLockShared(&g_p34_publish_lock);
 uint64_t now=GetTickCount64();return out->struct_size==sizeof(*out)&&out->version==1&&out->count<=ANYAPI_PLAYERS_CAPACITY&&out->sampled_tick&&now>=out->sampled_tick&&now-out->sampled_tick<=500;
}
