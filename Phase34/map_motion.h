#pragma once
#include "anyapi_players_v1.h"
#include <cmath>
#include <algorithm>
#include <map>
namespace atlas {
// Short, frame-rate independent presentation smoothing. Never used for gameplay
// actions/routing. Snap after teleports, scene changes or a stale sampling gap.
class PoseSmoother {
 struct Pose {double position[3]{},yaw{};int actor{};};std::map<int,Pose> poses;uint64_t epoch{},tick{};
public:
 void reset(){poses.clear();epoch=tick=0;}
 void update(AnySessionPlayersV1& players,uint64_t now){bool snap=epoch!=players.world_epoch||!tick||now<tick||now-tick>250;if(snap)poses.clear();double alpha=1-std::exp(-double(now>=tick?now-tick:0)/30.);epoch=players.world_epoch;tick=now;std::map<int,Pose> next;
 for(uint32_t i=0;i<players.count&&i<ANYAPI_PLAYERS_CAPACITY;++i){auto& p=players.players[i];if(!(p.valid_fields&PLAYER_POSITION))continue;Pose at;std::copy(p.position,p.position+3,at.position);at.yaw=p.yaw_radians;at.actor=p.actor_id;auto prior=poses.find(p.peer_id);
  if(!snap&&prior!=poses.end()&&prior->second.actor==p.actor_id&&std::hypot(p.position[0]-prior->second.position[0],p.position[2]-prior->second.position[2])<100){auto& old=prior->second;for(int j=0;j<3;++j)at.position[j]=old.position[j]+(at.position[j]-old.position[j])*alpha;at.yaw=old.yaw+std::remainder(at.yaw-old.yaw,2*std::acos(-1.))*alpha;}
  next[p.peer_id]=at;std::copy(at.position,at.position+3,p.position);p.yaw_radians=at.yaw;
 }poses=std::move(next);}
};
}
