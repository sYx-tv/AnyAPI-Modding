#pragma once
#include "anymaker_mod_api.h"
#include <cmath>
#include <cstddef>
static bool p272_finite(const double* v,size_t n){for(size_t i=0;i<n;++i)if(!std::isfinite(v[i]))return false;return true;}
static bool p272_valid_player(const AnymakerLocalPlayerStateV2& e){
    if(!e.actor || !e.client_scene)return false;
    if((e.valid_fields&ANY_PLAYER_VALID_POSITION) && !p272_finite(e.position,3))return false;
    if((e.valid_fields&ANY_PLAYER_VALID_ORIENTATION) && !p272_finite(e.orientation,2))return false;
    if((e.valid_fields&ANY_PLAYER_VALID_LINEAR_VELOCITY) && !p272_finite(e.linear_velocity,3))return false;
    const uint64_t bits[]={ANY_PLAYER_VALID_STAMINA,ANY_PLAYER_VALID_HUNGER,ANY_PLAYER_VALID_THIRST,ANY_PLAYER_VALID_OXYGEN,ANY_PLAYER_VALID_WETNESS,ANY_PLAYER_VALID_INFECTION,ANY_PLAYER_VALID_TEMPERATURE_COLD,ANY_PLAYER_VALID_TEMPERATURE_HOT};
    const double values[]={e.stamina,e.hunger,e.thirst,e.oxygen,e.wetness,e.infection,e.temperature_cold,e.temperature_hot};
    for(size_t i=0;i<8;++i)if((e.valid_fields&bits[i]) && !std::isfinite(values[i]))return false;
    return true;
}
static bool p272_valid_use(const AnymakerServerItemUseEventV2& e){
    if(e.kind<ANY_SERVER_ITEM_USE_SELF || e.kind>ANY_SERVER_ITEM_USE_TRANSFORM || e.actor_id<0 || e.item_id<0 || !e.server || !e.peer_data)return false;
    if(e.kind==ANY_SERVER_ITEM_USE_ACTOR && e.target_id<0)return false;
    if(e.kind==ANY_SERVER_ITEM_USE_VEHICLE && (e.target_id<0 || e.component_id<0))return false;
    return !e.item_resolved || (e.server_actor && e.server_item);
}
static bool p272_valid_actor(const AnymakerClientActorLifecycleEventV1& e){
    if(e.kind!=ANY_CLIENT_ACTOR_CREATED && e.kind!=ANY_CLIENT_ACTOR_DESTROYED)return false;
    if(!e.state.actor || e.state.state_version!=ANYMAKER_CLIENT_ACTOR_STATE_VERSION)return false;
    if((e.state.valid_fields&ANY_CLIENT_ACTOR_VALID_POSITION) && !p272_finite(e.state.position,3))return false;
    if((e.state.valid_fields&ANY_CLIENT_ACTOR_VALID_ORIENTATION) && !p272_finite(e.state.orientation,2))return false;
    return true;
}
