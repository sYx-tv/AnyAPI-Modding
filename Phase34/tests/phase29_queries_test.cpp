#include "../anymaker_mod_api.h"
#include "../phase29_contracts.h"
#include <cassert>
#include <vector>
#include <iostream>
using LONG=int;using LONG64=int64_t;using SRWLOCK=int;
static LONG64 InterlockedIncrement64(volatile LONG64* p){auto n=*p+1;*p=n;return n;}
static bool locked=false;
static void AcquireSRWLockShared(SRWLOCK*){assert(!locked);locked=true;}
static void ReleaseSRWLockShared(SRWLOCK*){assert(locked);locked=false;}
struct Entry {uintptr_t actor{};bool live{},have_snapshot{};P29Identity identity{};AnymakerClientActorStateV1 last_state{};};
static Entry g_client_actor_registry[3];static LONG g_client_actor_registry_highwater=3;
static constexpr LONG CLIENT_ACTOR_REGISTRY_CAPACITY=3;
static SRWLOCK g_client_actor_registry_lock{};static volatile LONG64 g_client_actor_state_queries{};
static bool seh_client_actor_enumerate_callback(AnymakerClientActorEnumerateCallback callback,const AnymakerClientActorStateV1* state,void* user){assert(!locked);return callback(state,user);}
#include "../phase29_actor_queries.inc"
static bool stop_after_one(const AnymakerClientActorStateV1* state,void*) {
    assert(!locked && state->actor==11);
    // Consumer reentry is safe: no native pointer read and no registry lock held.
    AnymakerClientActorStateV1 nested{};assert(get_client_actor_state(11,&nested));return false;
}
int main() {
    auto& live=g_client_actor_registry[0];live.actor=11;live.live=live.have_snapshot=true;
    live.identity.begin(11,100,1);live.last_state.actor=11;live.last_state.position[0]=42;
    auto& dead=g_client_actor_registry[1];dead.actor=22;dead.have_snapshot=true;dead.last_state.actor=22;
    auto& missing=g_client_actor_registry[2];missing.actor=33;missing.live=true;missing.identity.begin(33,100,2);
    AnymakerClientActorStateV1 output{};
    assert(get_client_actor_state(11,&output) && output.position[0]==42);
    assert(!get_client_actor_state(22,&output) && output.actor==0);
    assert(!get_client_actor_state(33,&output) && output.actor==0);
    assert(!get_client_actor_state(0,&output) && !get_client_actor_state(11,nullptr));
    assert(enumerate_client_actors(stop_after_one,nullptr)==1);
    live.identity.retire();assert(!get_client_actor_state(11,&output));
    assert(enumerate_client_actors(stop_after_one,nullptr)==0);
    std::cout<<"PASS: production snapshot-only actor queries reject dead/missing entries, clear failures and release locks before reentrant consumer callbacks\n";
}
