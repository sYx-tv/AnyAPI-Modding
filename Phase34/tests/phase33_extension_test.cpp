#include "../anymaker_mod_api.h"
#include "../phase29_contracts.h"
#include "../phase33_object_core.h"
#include <cassert>
#include <cstring>
#include <shared_mutex>
#include <iostream>
#define P33_EXTENSION_TEST
using SRWLOCK=std::shared_mutex;
void AcquireSRWLockShared(SRWLOCK* p){p->lock_shared();}
void ReleaseSRWLockShared(SRWLOCK* p){p->unlock_shared();}
struct Actor {
    bool live{},have_snapshot{};uint64_t p33_epoch{},last_seen_ms{};
    int32_t actor_id{};P29Identity identity{};AnymakerClientActorStateV1 last_state{};
};
struct Component {
    bool live{};uint64_t p33_epoch{};uint32_t side{};uintptr_t metadata_definition{};
    char class_name[128]{};P29Identity identity{};
};
constexpr size_t CLIENT_ACTOR_REGISTRY_CAPACITY=2,COMPONENT_REGISTRY_CAPACITY=2;
static Actor g_client_actor_registry[2];static Component g_component_registry[2];
static int g_client_actor_registry_highwater=2,g_component_registry_count=2;
static SRWLOCK g_client_actor_registry_lock,g_component_registry_lock,g_p33_handheld_lock,g_p33_journal_lock;
static std::atomic<bool> g_p33_ready{true};
static std::atomic<uint64_t> g_p29_world_revision{4};
static AnyHandheldSnapshotV1 g_p33_handheld[2];
static P33Invalidations<3> g_p33_journal;
#include "../phase33_extension.inc"
int main(){
    auto& e=g_client_actor_registry[0];e.live=e.have_snapshot=true;e.p33_epoch=4;e.actor_id=123;e.last_seen_ms=50;
    e.identity.begin(0xfeed,0xbeef,9);e.last_state.actor=0xfeed;e.last_state.valid_fields=ANY_CLIENT_ACTOR_VALID_POSITION;
    e.last_state.position[0]=42;
    AnyObjectTokenV1 token=p33_token(4,9,0,ANY_OBJECT_ACTOR),rows[2]{};size_t count=77,total=77;
    assert(p33_enumerate(ANY_OBJECT_ACTOR,nullptr,0,&count,&total)==ANY_EXT_BUFFER_TOO_SMALL && count==0 && total==1);
    assert(p33_enumerate(ANY_OBJECT_ACTOR,rows,2,&count,&total)==ANY_EXT_OK && count==1 && rows[0].object_id==1);
    assert(rows[0].object_id!=e.identity.ticket.address && p33_validate(token)==ANY_EXT_OK);
    AnyActorSnapshotV1 actor{};
    assert(p33_actor(token,&actor,sizeof(actor))==ANY_EXT_OK && actor.position[0]==42 && actor.actor_id==123);
    assert(p33_actor(token,&actor,sizeof(actor)-1)==ANY_EXT_BAD_ARGUMENT);
    auto corrupt=token;corrupt.object_id=UINT64_MAX;assert(p33_validate(corrupt)==ANY_EXT_BAD_ARGUMENT);
    corrupt=token;corrupt.reserved=1;assert(p33_validate(corrupt)==ANY_EXT_BAD_ARGUMENT);
    e.identity.retire();e.live=false;assert(p33_validate(token)==ANY_EXT_STALE_TOKEN);
    assert(p33_actor(token,&actor,sizeof(actor))==ANY_EXT_STALE_TOKEN && actor.position[0]==0);
    e.identity.begin(0xfeed,0xbeef,10);e.live=true;
    assert(p33_validate(token)==ANY_EXT_STALE_TOKEN);token.object_generation=10;
    assert(p33_validate(token)==ANY_EXT_OK);
    g_p29_world_revision=5;assert(p33_validate(token)==ANY_EXT_STALE_TOKEN);
    assert(p33_enumerate(ANY_OBJECT_ACTOR,rows,2,&count,&total)==ANY_EXT_OK && total==0);
    token.world_epoch=5;assert(p33_validate(token)==ANY_EXT_STALE_TOKEN); // Old entry cannot be retokenized into new world.
    auto& c=g_component_registry[0];c.live=true;c.p33_epoch=5;c.side=1;c.metadata_definition=0xdead;
    c.identity.begin(0xabcd,0x9876,11);std::strcpy(c.class_name,"building");
    auto ct=p33_token(5,11,0,ANY_OBJECT_COMPONENT);AnyComponentSnapshotV1 component{};
    assert(p33_validate(ct)==ANY_EXT_OK && p33_component(ct,&component,sizeof(component))==ANY_EXT_OK);
    assert(std::strcmp(component.class_name,"building")==0 && component.class_name_valid==1);
    c.identity.retire();assert(p33_component(ct,&component,sizeof(component))==ANY_EXT_STALE_TOKEN && component.class_name[0]==0);
    AnyHandheldSnapshotV1 hand{};assert(p33_handheld(0,&hand,sizeof(hand))==ANY_EXT_UNAVAILABLE);
    auto& h=g_p33_handheld[0];h.struct_size=sizeof(h);h.version=1;h.world_epoch=5;h.readable=1;
    assert(p33_handheld(0,&hand,sizeof(hand))==ANY_EXT_OK && hand.quantity_valid==0 && hand.item_identity_valid==0);
    assert(p33_handheld(2,&hand,sizeof(hand))==ANY_EXT_BAD_ARGUMENT);
    g_p29_world_revision=6;assert(p33_handheld(0,&hand,sizeof(hand))==ANY_EXT_UNAVAILABLE && hand.quantity==0);
    AnyInvalidationV1 notices[3];uint64_t next=0;
    g_p33_journal.push(token,ANY_OBJECT_RETIRED);g_p33_journal.push(ct,ANY_OBJECT_RETIRED);
    assert(p33_changes(0,notices,1,&count,&next)==ANY_EXT_BUFFER_TOO_SMALL && count==1 && next==1);
    assert(p33_changes(next,notices,3,&count,&next)==ANY_EXT_OK && count==1 && next==2);
    g_p33_journal.push({},ANY_WORLD_RETIRED);g_p33_journal.push({},ANY_WORLD_RETIRED);
    assert(p33_changes(0,notices,3,&count,&next)==ANY_EXT_RESYNC_REQUIRED && count==0 && next==4);
    assert(p33_changes(5,notices,3,&count,&next)==ANY_EXT_BAD_ARGUMENT);
    g_p33_ready=false;assert(p33_validate(ct)==ANY_EXT_UNAVAILABLE);
    assert(p33_enumerate(ANY_OBJECT_ACTOR,rows,2,&count,&total)==ANY_EXT_UNAVAILABLE && count==0 && total==0);
    AnyExtensionInfoV1 info{};assert(p33_info(&info,sizeof(info))==ANY_EXT_OK && !info.native_observers_ready && !info.session_role);
    std::atomic<uint64_t> epoch{UINT64_MAX};assert(p33_advance_epoch(epoch)==0 && epoch==0);
    std::cout<<"PASS: production extension rejects delete/reuse/prior-world tokens, bounds output, copies registry state, keeps quantities unknown and reports journal overflow\n";
}
