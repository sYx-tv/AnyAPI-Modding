#include "anymaker_mod_api.h"
#include "../legacy_contracts.h"
#include "../runtime_object_core.h"
static void p29_note_hook(size_t){}
static std::atomic<uint64_t> g_p29_world_revision{1},g_p29_teardowns{0};
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
using LONG=int32_t;using LONG64=int64_t;using SRWLOCK=int;
static LONG64 InterlockedIncrement64(volatile LONG64* p){auto v=*p+1;*p=v;return v;}
static LONG64 InterlockedExchange64(volatile LONG64* p,LONG64 v){auto old=*p;*p=v;return old;}
static LONG64 InterlockedCompareExchange64(volatile LONG64* p,LONG64 v,LONG64 expected){auto old=*p;if(old==expected)*p=v;return old;}
static void AcquireSRWLockExclusive(SRWLOCK*){}static void ReleaseSRWLockExclusive(SRWLOCK*){}
struct Entry{uintptr_t actor=0,container=0;int32_t actor_id=0;bool live=false,have_snapshot=false;AnymakerClientActorStateV1 last_state{};P29Identity identity{};uint64_t p33_epoch{};};
static constexpr LONG CLIENT_ACTOR_REGISTRY_CAPACITY=4;
static Entry g_client_actor_registry[4];static LONG g_client_actor_registry_highwater=3;
static SRWLOCK g_client_actor_registry_lock=0,g_latest_player_lock=0;
static volatile LONG64 g_p27_hook_calls[24]{},g_local_actor_hint=100,g_local_scene_hint=99,g_next_sequence=0,g_client_actor_destroyed_captured=0;
static bool g_have_latest_player_state=true;static AnymakerLocalPlayerStateV2 g_latest_player_state{};
static int original_calls=0;static std::vector<AnymakerClientActorLifecycleEventV1> events;
static void enqueue_client_actor_event(const AnymakerClientActorLifecycleEventV1& event){assert(original_calls>0);events.push_back(event);}
static void p33_invalidate(AnyObjectTokenV1,uint32_t=ANY_OBJECT_RETIRED){}
static void p33_retire_world(){p33_advance_epoch(g_p29_world_revision);}
#include "../legacy_actor_teardown.inc"
static void original(void* container,void* physics){assert(container==(void*)11 && physics==(void*)22);assert(!g_client_actor_registry[0].live && !g_client_actor_registry[1].live);assert(g_client_actor_registry[2].live);assert(!g_have_latest_player_state);++original_calls;}
int main(){
    assert(g_p271_bulk_candidate==0);
    g_client_actor_registry[0]={100,11,1,true,false,{}};
    g_client_actor_registry[1]={200,11,2,true,true,{}};
    auto& snapshot=g_client_actor_registry[1].last_state;snapshot.struct_size=sizeof(snapshot);snapshot.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;snapshot.actor=(AnymakerActorHandle)200;snapshot.actor_id=2;snapshot.valid_fields=ANY_CLIENT_ACTOR_VALID_ID;
    g_client_actor_registry[2]={300,33,3,true,false,{}};g_p271_bulk_original=original;
    for(auto& entry:g_client_actor_registry) if(entry.live){entry.identity.begin(entry.actor,entry.container,1);entry.p33_epoch=1;}
    p271_bulk_destroy_hook((void*)11,(void*)22);
    assert(original_calls==1 && events.size()==2 && g_p271_bulk_invalidated==2 && g_client_actor_destroyed_captured==2);
    assert(g_local_actor_hint==0 && g_local_scene_hint==0);
    assert(!g_client_actor_registry[0].identity.live && !g_client_actor_registry[1].identity.live && g_client_actor_registry[2].identity.live);
    assert(g_p29_world_revision==2 && g_p29_teardowns==1);
    for(auto& event:events){assert(event.kind==ANY_CLIENT_ACTOR_DESTROYED && event.state.struct_size==sizeof(event.state));assert(event.state.actor_id==1 || event.state.actor_id==2);}
    assert(events[0].sequence<events[1].sequence);
    p271_bulk_destroy_hook((void*)11,(void*)22);assert(original_calls==2 && events.size()==2 && g_p271_bulk_invalidated==2);
    std::cout<<"PASS: bulk teardown invalidates only owning container, forwards once, clears local cache, and emits no duplicate events\n";
}
