#include "map_roads.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
 const double pi=std::acos(-1.);
 for(double yaw:{0.,pi*.25,pi*.5,pi,-pi*.5}){
  roads::Point a{},b{std::sin(yaw)*100,0,std::cos(yaw)*100};
  roads::Point right{b.x+std::cos(yaw)*100,0,b.z-std::sin(yaw)*100};
  roads::Point left{b.x-std::cos(yaw)*100,0,b.z+std::sin(yaw)*100};
  assert(std::abs(roads::turn_angle(a,b,right)-pi*.5)<1e-10);
  assert(std::abs(roads::turn_angle(a,b,left)+pi*.5)<1e-10);
 }

 roads::Graph g;g.nodes={{0,0,0},{100,0,0},{100,0,100},{500,0,500},{600,0,500}};
 g.points={{0,0,0},{50,0,20},{100,0,0},{100,0,0},{100,0,50},{100,0,100},{500,0,500},{600,0,500}};
 g.edges={{0,1,0,3},{1,2,3,3},{3,4,6,2}};g.prepare();
 auto same=g.route({40,0,20},{60,0,20});assert(same.connected&&same.length<30&&same.points.size()>=3);
 auto reverse=g.route({60,0,20},{40,0,20});assert(reverse.connected&&std::abs(reverse.length-same.length)<1e-8);
 auto across=g.route({0,0,0},{100,0,100});assert(across.connected&&across.length>200&&across.points.front().x==0&&across.points.back().z==100);
 assert(!g.route({0,0,0},{550,0,500}).connected);
 auto offroad=g.route({0,0,-50},{100,0,150});assert(offroad.connected&&offroad.start_gap==50&&offroad.end_gap==50);
 auto flying=g.route({0,10000,0},{100,0,100});assert(flying.connected&&std::abs(flying.length-across.length)<1e-8);
 if(argc>1){roads::Graph actual;assert(actual.load(std::filesystem::path(argv[1])));auto spawn=roads::Point{83955.394,5.472,55845.817};
  auto s=actual.snap(spawn,true);assert(s.edge>=0&&s.gap<500);auto local=actual.route(spawn,actual.points[actual.edges[s.edge].first]);assert(local.connected);
  unsigned reachable=0;double longest=0;roads::Point destination{};
  // Exercise routes from the reported Sandbox spawn to every authored road node.
  for(auto node:actual.nodes){auto r=actual.route(spawn,node);if(r.connected){++reachable;if(r.length>longest){longest=r.length;destination=node;}}}
  assert(reachable>100&&longest>1000);auto long_route=actual.route(spawn,destination);assert(long_route.connected&&long_route.points.size()>20);
  std::cout<<"Sandbox routes: "<<reachable<<" nodes; furthest "<<longest<<" m; destination "<<destination.x<<","<<destination.z<<"; snap "<<s.gap<<" m\n";
 }
}
