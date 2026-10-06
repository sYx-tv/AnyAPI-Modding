#pragma once
#include <vector>
#include <array>
#include <queue>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <cstdint>
namespace roads {
struct Point {double x{},y{},z{};};
// Measure 3D distance between two road points, including elevation.
inline double distance(Point a,Point b){return std::hypot(std::hypot(a.x-b.x,a.z-b.z),a.y-b.y);}
// Measure horizontal X/Z distance, ignoring elevation.
inline double flat(Point a,Point b){return std::hypot(a.x-b.x,a.z-b.z);}
// Signed route turn in world X/Z: positive is right, negative is left.
// Example: traveling +Z then +X turns right; -X turns left.
inline double turn_angle(Point a,Point b,Point c){
 return std::atan2((b.z-a.z)*(c.x-b.x)-(b.x-a.x)*(c.z-b.z),
                  (b.x-a.x)*(c.x-b.x)+(b.z-a.z)*(c.z-b.z));
}
struct Edge {uint32_t a,b,first,count;};
struct Snap {int edge{-1};uint32_t segment{};Point point{};double along{},gap{1e100};};
struct Route {bool connected{};std::vector<Point> points;double length{},start_gap{},end_gap{};};
class Graph {
public:
 std::vector<Point> nodes,points;std::vector<Edge> edges;
 std::vector<double> lengths;std::vector<std::vector<int>> adjacent;
 // Validate and load the bounded road cache into a temporary graph; replace the current graph only on success.
 bool load(const std::filesystem::path& path){
  std::ifstream f(path,std::ios::binary);return load(f);
 }
 // Read the same validated road-cache format from a file or an embedded DLL resource.
 bool load(std::istream& f){char magic[8];uint32_t v,n,e,p;double calibration[3];
  if(!f.read(magic,8)||std::memcmp(magic,"ANYROAD2",8)||!f.read((char*)&v,4)||v!=2||
   !f.read((char*)&n,4)||!f.read((char*)&e,4)||!f.read((char*)&p,4)||!f.read((char*)calibration,24)||
   n>100000||e>200000||p>3000000||!n||!e||calibration[0]!=81000||calibration[1]!=52000||calibration[2]!=6508)return false;
  Graph candidate;candidate.nodes.resize(n);candidate.edges.resize(e);candidate.points.resize(p);
  if(!f.read((char*)candidate.nodes.data(),n*sizeof(Point))||!f.read((char*)candidate.edges.data(),e*sizeof(Edge))||
   !f.read((char*)candidate.points.data(),p*sizeof(Point))||f.peek()!=std::char_traits<char>::eof())return false;
  auto valid=[](Point q){return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::abs(q.x)<1e7&&std::abs(q.z)<1e7&&std::abs(q.y)<1e5;};
  for(auto q:candidate.nodes)if(!valid(q))return false;for(auto q:candidate.points)if(!valid(q))return false;
  for(auto q:candidate.edges)if(q.a>=n||q.b>=n||q.count<2||q.first>p||q.count>p-q.first||
   distance(candidate.nodes[q.a],candidate.points[q.first])>1||distance(candidate.nodes[q.b],candidate.points[q.first+q.count-1])>1)return false;
  candidate.prepare();*this=std::move(candidate);return true;
 }
 // Build edge lengths and node adjacency lists used by route search.
 void prepare(){adjacent.assign(nodes.size(),{});lengths.assign(edges.size(),0);
  for(size_t i=0;i<edges.size();++i){auto e=edges[i];adjacent[e.a].push_back(int(i));adjacent[e.b].push_back(int(i));
   for(uint32_t k=1;k<e.count;++k)lengths[i]+=distance(points[e.first+k-1],points[e.first+k]);}}
 // Find the nearest point on sampled road segments; optionally penalize height differences.
 Snap snap(Point p,bool height)const {Snap best;double best_score=1e100;
  for(size_t i=0;i<edges.size();++i){auto e=edges[i];double along=0;
   for(uint32_t j=0;j+1<e.count;++j){auto a=points[e.first+j],b=points[e.first+j+1];double dx=b.x-a.x,dz=b.z-a.z,den=dx*dx+dz*dz;
    double t=den>1e-9?std::clamp(((p.x-a.x)*dx+(p.z-a.z)*dz)/den,0.,1.):0.;
    Point q{a.x+t*dx,a.y+t*(b.y-a.y),a.z+t*dz};double gap=flat(p,q),score=gap*gap+(height?4*(p.y-q.y)*(p.y-q.y):0);
    if(score<best_score){best_score=score;best={int(i),j,q,along+t*distance(a,b),gap};}along+=distance(a,b);
   }}return best;
 }
 // Build the sampled road polyline from a snapped point to either endpoint of its edge.
 std::vector<Point> to_node(const Snap& s,uint32_t node)const {auto e=edges[s.edge];std::vector<Point> out{s.point};
  if(node==e.a){for(int k=int(s.segment);k>=0;--k)out.push_back(points[e.first+k]);}
  else {for(uint32_t k=s.segment+1;k<e.count;++k)out.push_back(points[e.first+k]);}return out;
 }
 // Snap both ends horizontally, search connected roads with Dijkstra, then assemble the shortest sampled path plus off-road approach distances. No distance cap.
 Route route(Point from,Point to)const {Route result;if(edges.empty())return result;auto s=snap(from,false),t=snap(to,false);
  result.start_gap=s.gap;result.end_gap=t.gap;if(s.edge<0||t.edge<0)return result;
  auto se=edges[s.edge],te=edges[t.edge];double best=1e100;bool same=false;
  if(s.edge==t.edge){best=std::abs(s.along-t.along);same=true;}
  std::vector<double> cost(nodes.size(),1e100);std::vector<int> parent(nodes.size(),-1),via(nodes.size(),-1);
  using Item=std::pair<double,uint32_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
  cost[se.a]=s.along;cost[se.b]=lengths[s.edge]-s.along;queue.push({cost[se.a],se.a});queue.push({cost[se.b],se.b});
  while(!queue.empty()){auto [c,u]=queue.top();queue.pop();if(c!=cost[u])continue;
   for(int i:adjacent[u]){auto e=edges[i];uint32_t v=u==e.a?e.b:e.a;double next=c+lengths[i];
    if(next+1e-9<cost[v]){cost[v]=next;parent[v]=int(u);via[v]=i;queue.push({next,v});}}}
  uint32_t end=te.a;double a=cost[te.a]+t.along,b=cost[te.b]+lengths[t.edge]-t.along;
  if(b<a){a=b;end=te.b;}if(a<best){best=a;same=false;}if(best>=1e99)return result;
  if(same){result.points.push_back(s.point);auto e=se;
   if(s.along<=t.along){for(uint32_t k=s.segment+1;k<=t.segment;++k)result.points.push_back(points[e.first+k]);}
   else {for(int k=int(s.segment);k>int(t.segment);--k)result.points.push_back(points[e.first+k]);}result.points.push_back(t.point);
  }else {std::vector<uint32_t> path{end};while(parent[path.back()]>=0)path.push_back(uint32_t(parent[path.back()]));
   std::reverse(path.begin(),path.end());result.points=to_node(s,path[0]);
   for(size_t k=1;k<path.size();++k){auto e=edges[via[path[k]]];if(path[k-1]==e.a){for(uint32_t j=1;j<e.count;++j)result.points.push_back(points[e.first+j]);}
    else {for(int j=int(e.count)-2;j>=0;--j)result.points.push_back(points[e.first+j]);}}
   auto tail=to_node(t,end);std::reverse(tail.begin(),tail.end());result.points.insert(result.points.end(),tail.begin(),tail.end());
  }result.connected=true;result.length=best+s.gap+t.gap;return result;
 }
};
}
