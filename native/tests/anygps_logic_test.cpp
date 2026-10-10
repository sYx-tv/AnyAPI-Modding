#include "../anygps_logic.h"
#include "../anygps_mesh.h"
#include <cstring>
#include <vector>
#include <cassert>
#include <cmath>
using namespace anygps;
static bool near(double a,double b,double e=1e-6){return std::abs(a-b)<e;}
static Mat34 at(Vec3 p,double yaw_deg=0){double r=yaw_deg*kPi/180;Mat34 m;
 // columns: right, up, forward; yaw about +Y keeps forward = (sin, 0, cos)
 m.m[0]=std::cos(r);m.m[2]=-std::sin(r);m.m[4]=1;m.m[6]=std::sin(r);m.m[8]=std::cos(r);m.m[9]=p.x;m.m[10]=p.y;m.m[11]=p.z;return m;}
int main(){
 // Channel table: names are unique, inputs come last, lookups work by length.
 for(int i=0;i<kChannelCount;++i){assert(channel_index(kChannels[i].name,std::char_traits<char>::length(kChannels[i].name))==i);assert(kChannels[i].input==(i>=kFirstInput));}
 assert(channel_index("north_angle",11)<0&&channel_index("x",1)==kX&&channel_index("xy",1)==kX&&channel_index("speed_kmh",9)==kSpeedKmh);
 // The definition clones the compass and lists every channel once, after north_angle.
 std::string json=definition_json();
 assert(json.find("\"class\": \"compass_sensor\"")!=std::string::npos&&json.find("compass_sensor_a.mesh")!=std::string::npos);
 assert(json.find("\"tech_tier\": 4")!=std::string::npos&&json.find("\"category\": \"sensor\"")!=std::string::npos);
 assert(json.find("north_angle")<json.find("\"name\": \"x\"")&&json.find("\"type_f64_input\", \"name\": \"waypoint_z\"")!=std::string::npos);
 // Composition: vehicle * component.
 Mat34 c=compose(at({10,2,3},90),at({1,0,0}));Vec3 p=column(c,3);assert(near(p.x,10)&&near(p.y,2)&&near(p.z,2));
 // Position is the raw world position; altitude is world y.
 Sensor s;tick(s,at({100,50,-20}),0,0);assert(s.slots[kX]==100&&s.slots[kY]==50&&s.slots[kZ]==-20&&s.slots[kAltitude]==50);
 assert(s.slots[kSpeedMs]==0&&near(s.slots[kHeading],0)&&near(s.slots[kPitch],0)&&near(s.slots[kRoll],0));
 // Heading wraps into 0-360.
 tick(s,at({100,50,-20}),-90,0.01);assert(near(s.slots[kHeading],270));
 // Constant 10 m/s along +Z settles to 10 m/s = 36 km/h; vertical 0; acceleration settles to 0.
 Sensor m;double t=0;for(int i=0;i<200;++i){t+=0.02;tick(m,at({0,5,10*t}),0,t);}
 assert(near(m.slots[kSpeedMs],10,1e-3)&&near(m.slots[kSpeedKmh],36,1e-2)&&near(m.slots[kGroundSpeedMs],10,1e-3)&&near(m.slots[kVerticalSpeedMs],0));
 assert(std::abs(m.slots[kAccelMs2])<0.05&&near(m.slots[kAccelKmhS],m.slots[kAccelMs2]*3.6));
 // Constant 2 m/s² acceleration reads about 2 m/s² and 7.2 km/h per second; climbing shows vertical speed.
 Sensor a;t=0;for(int i=0;i<300;++i){t+=0.02;tick(a,at({0,3*t,t*t}),0,t);}
 assert(near(a.slots[kAccelMs2],2,0.15)&&near(a.slots[kAccelKmhS],7.2,0.6)&&near(a.slots[kVerticalSpeedMs],3,0.05)&&near(a.slots[kVerticalSpeedKmh],10.8,0.2));
 // A long pause or teleport does not produce a speed spike.
 tick(a,at({5000,0,0}),0,t+5);assert(a.slots[kSpeedMs]<a.slots[kSpeedMs]+1&&a.slots[kSpeedMs]<200);
 // Pitch and roll in degrees.
 Mat34 nose=at({0,0,0});nose.m[7]=std::sin(kPi/6);nose.m[8]=std::cos(kPi/6);Sensor pr;tick(pr,nose,0,0);assert(near(pr.slots[kPitch],30,1e-6));
 Mat34 bank=at({0,0,0});bank.m[1]=std::sin(kPi/4);bank.m[0]=std::cos(kPi/4);tick(pr,bank,0,1);assert(near(pr.slots[kRoll],45,1e-6));
 // Waypoint: distance, height difference and bearings in the compass convention.
 Sensor w;w.slots[kWaypointX]=30;w.slots[kWaypointY]=4;w.slots[kWaypointZ]=40;tick(w,at({0,0,0}),0,0);
 assert(near(w.slots[kWaypointHorizontalDistance],50)&&near(w.slots[kWaypointHeightDifference],4)&&near(w.slots[kWaypointDistance],std::sqrt(2516.0)));
 double rel=deg(std::atan2(30.0,40.0));assert(near(std::abs(w.slots[kWaypointRelativeBearing]),rel,1e-6));
 // Waypoint straight ahead: relative bearing 0, bearing equals heading.
 Sensor f;f.slots[kWaypointZ]=100;tick(f,at({0,0,0}),123,0);assert(near(f.slots[kWaypointRelativeBearing],0)&&near(f.slots[kWaypointBearing],123));
 // The heading direction calibrates from turning: when the game's heading falls as yaw rises, bearings flip.
 Sensor k;k.slots[kWaypointX]=100;tick(k,at({0,0,0},0),0,0);tick(k,at({0,0,0},10),-10,0.02);assert(k.sign_known&&k.sign==-1);
 Sensor k2;k2.slots[kWaypointX]=100;tick(k2,at({0,0,0},0),0,0);tick(k2,at({0,0,0},10),10,0.02);assert(k2.sign_known&&k2.sign==1);
 assert(near(k.slots[kWaypointRelativeBearing],-k2.slots[kWaypointRelativeBearing]));
 // Mesh: a synthetic file in the compass layout gains the arrows and still parses; junk is rejected.
 std::vector<uint8_t> mf;auto put=[&](const void*p,size_t n){auto*c=(const uint8_t*)p;mf.insert(mf.end(),c,c+n);};
 put("mesh",4);uint32_t u=5;put(&u,4);u=1;put(&u,4);const char*nm="compass_sensor_a";u=16;put(&u,4);put(nm,16);
 uint32_t hdr[8]={3,1,5,2,2,3,3,4};put(hdr,32);uint8_t zero[56]={};put(zero,56);double bd[6]={-.04,-.04,-.04,.04,.12,.04};put(bd,48);
 MeshVertex vs[3]={};float P[3][3]={{.04f,-.04f,.04f},{.04f,.04f,.04f},{-.04f,.12f,-.04f}};for(int i=0;i<3;++i){std::memcpy(vs[i].pos,P[i],12);vs[i].rgba[0]=153;vs[i].rgba[3]=255;}
 u=sizeof vs;put(&u,4);put(vs,sizeof vs);uint32_t I[3]={0,1,2};u=12;put(&u,4);put(I,12);uint8_t tr[8]={};put(tr,8);
 MeshLayout L;assert(parse_mesh(mf,L)&&L.vertices==3&&L.indices==3);
 auto g=build_gps_mesh(mf,"anygps_gps_sensor_a");MeshLayout G;assert(!g.empty()&&parse_mesh(g,G)&&G.vertices==3+4*6&&G.indices==3+4*6);
 assert(std::memcmp(g.data()+16,"anygps_gps_sensor_a",19)==0);
 double gb[6];std::memcpy(gb,g.data()+G.bounds,48);assert(gb[4]>.12&&gb[5]>.04&&gb[0]==-.04);
 MeshVertex last;std::memcpy(&last,g.data()+G.vertex_bytes+4+(G.vertices-1)*36,36);assert(last.rgba[0]==255&&last.rgba[1]==196&&last.normal[2]==-1);
 auto bad=mf;bad[0]='x';assert(build_gps_mesh(bad,"x").empty());bad=mf;bad.pop_back();assert(build_gps_mesh(bad,"x").empty());
 // The definition points at whichever mesh was built.
 assert(definition_json(kGpsMeshPath).find(kGpsMeshPath)!=std::string::npos&&definition_json().find(kStockMeshPath)!=std::string::npos);
 return 0;}
