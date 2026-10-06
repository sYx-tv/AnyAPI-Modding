#include "../legacy_pattern_match.h"
#include "../legacy_payload_checks.h"
#include <cassert>
#include <iostream>
#include <limits>
int main(){
    const unsigned char a[]={0x53,1,2,3,4,5,6,7,8,9};
    const unsigned char b[]={0x53,1,2,3,4,5,6,7,8,10};
    P272PatternIndex index;index.add({a,sizeof(a),1});index.add({b,sizeof(b),2});
    unsigned char data[40]{};std::memcpy(data+13,a,sizeof(a));std::memcpy(data+26,b,sizeof(b));
    size_t counts[3]{};
    index.scan(data,25,16,[&](size_t id,size_t offset){assert(offset==13);++counts[id];});
    index.scan(data+16,24,24,[&](size_t id,size_t offset){assert(offset==10);++counts[id];});
    assert(counts[1]==1 && counts[2]==1); // overlap neither loses nor duplicates a match
    index.scan(data+13,9,9,[&](size_t,size_t){assert(false);}); // partial pattern never accepted
    AnymakerServerItemUseEventV2 use{};use.kind=ANY_SERVER_ITEM_USE_VEHICLE;use.actor_id=1;use.item_id=2;use.target_id=3;use.component_id=4;use.server=1;use.peer_data=2;
    assert(p272_valid_use(use));use.item_resolved=1;assert(!p272_valid_use(use));use.server_actor=3;use.server_item=4;assert(p272_valid_use(use));use.component_id=-1;assert(!p272_valid_use(use));
    AnymakerLocalPlayerStateV2 player{};player.actor=1;player.client_scene=2;player.valid_fields=ANY_PLAYER_VALID_STAMINA;assert(p272_valid_player(player));player.stamina=std::numeric_limits<double>::quiet_NaN();assert(!p272_valid_player(player));player.valid_fields=0;assert(p272_valid_player(player));
    AnymakerClientActorLifecycleEventV1 actor{};actor.kind=ANY_CLIENT_ACTOR_DESTROYED;actor.state.actor=1;actor.state.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;assert(p272_valid_actor(actor));actor.state.valid_fields=ANY_CLIENT_ACTOR_VALID_POSITION;actor.state.position[0]=std::numeric_limits<double>::infinity();assert(!p272_valid_actor(actor));
    std::cout<<"PASS: broad matcher handles overlap, shared prefixes and partial reads; semantic validators reject invalid resolved handles, targets and finite fields\n";
}
