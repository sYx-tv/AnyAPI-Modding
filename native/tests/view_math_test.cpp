#include "../effects/view_math.h"
#include <cassert>
#include <cstdio>
using namespace scene_view_math;
// A synthetic game camera: view from a pose, reversed-Z infinite perspective,
// written in either float layout, checked through detection and rebuild.
static M4 view_of(const Pose& c,double handed){M4 v;const double* rows[3]={c.right,c.up,c.forward};for(int i=0;i<3;++i){double s=i==2?handed:1;for(int k=0;k<3;++k)v.m[i][k]=rows[i][k]*s;v.m[i][3]=-dot3(v.m[i],c.position);}v.m[3][3]=1;return v;}
static M4 projection(double handed){M4 p;double t=std::tan(.5);p.m[0][0]=1/(t*16/9.);p.m[1][1]=1/t;p.m[2][3]=.1;p.m[3][2]=handed;return p;}
static void make(float* cam,const Pose& c,double handed,int layout){M4 v=view_of(c,handed),p=projection(handed),vp=mul(p,v),inv;assert(inverse(vp,inv));store(vp,cam+VIEW_PROJECTION,layout);store(inv,cam+INVERSE_VIEW_PROJECTION,layout);store(v,cam+VIEW,layout);store(p,cam+PROJECTION,layout);for(int i=0;i<3;++i)cam[POSITION+i]=float(c.position[i]);}
int main(){for(double handed:{1.,-1.})for(int layout=0;layout<2;++layout){
 Pose c{{12,3,-40},{1,0,0},{0,1,0},{0,0,handed>0?1.:-1.}};
 float cam[CAMERA_FLOATS]{},copy[CAMERA_FLOATS];make(cam,c,handed,layout);memcpy(copy,cam,sizeof(cam));
 assert(detect_layout(cam,c.position,c.forward)==layout);
 // Same pose: unchanged.
 int got=-1;assert(rebuild(copy,c,c,1,nullptr,&got)==OK&&got==layout);for(size_t i=0;i<64;++i)assert(std::abs(copy[i]-cam[i])<1e-3f*std::max(1.f,std::abs(cam[i])));
 // Chase: 8 m behind, 3 m up. The main camera's position must now be 8 m ahead in view depth.
 Pose chase;double off[3]={0,3,-8};assert(relative(c,off,0,0,chase));memcpy(copy,cam,sizeof(cam));M4 inv;assert(rebuild(copy,c,chase,1,&inv)==OK);
 auto vp=load(copy+VIEW_PROJECTION,layout);assert(std::abs(std::abs(clip_w(vp,c.position))-8)<1e-3);assert(std::abs(copy[POSITION+1]-6)<1e-5);
 M4 check=mul(vp,inv);for(int i=0;i<4;++i)for(int j=0;j<4;++j)assert(std::abs(check.m[i][j]-(i==j))<1e-3);
 // A point 10 m along the new forward lands at the screen centre.
 double ahead[4];for(int i=0;i<3;++i)ahead[i]=chase.position[i]+chase.forward[i]*10;double x=vp.m[0][0]*ahead[0]+vp.m[0][1]*ahead[1]+vp.m[0][2]*ahead[2]+vp.m[0][3],w=clip_w(vp,ahead);assert(std::abs(x/w)<1e-6);
 // Turned 90 degrees right: the old right axis becomes forward.
 Pose turned;double none[3]{};assert(relative(c,none,1.5707963267948966,0,turned));for(int i=0;i<3;++i)assert(std::abs(turned.forward[i]-c.right[i])<1e-9);
 // Absolute: yaw 0 looks along +Z, pi/2 along +X, right follows handedness.
 Pose a;assert(orient(c,0,0,a)&&std::abs(a.forward[2]-1)<1e-9&&std::abs(a.up[1]-1)<1e-9);double cr[3];cross3(a.up,a.forward,cr);assert((dot3(cr,a.right)>0)==(handed>0));
 assert(orient(c,1.5707963267948966,0,a)&&std::abs(a.forward[0]-1)<1e-9);
 // Zoom doubles the projection scale.
 memcpy(copy,cam,sizeof(cam));assert(rebuild(copy,c,c,2)==OK);assert(std::abs(load(copy+PROJECTION,layout).m[1][1]-2*load(cam+PROJECTION,layout).m[1][1])<1e-4);
 // Field of view: the test projection is 1 radian vertical.
 assert(std::abs(fov_scale_for(cam,layout,1.0)-1)<1e-5&&std::abs(fov_scale_for(cam,layout,0)-1)<1e-12&&fov_scale_for(cam,layout,.5)>1.9);
 // Broken constants are refused.
 memcpy(copy,cam,sizeof(cam));copy[VIEW+3]+=5;copy[VIEW+12]+=5;assert(rebuild(copy,c,chase,1)!=OK);
 Pose wrong=c;wrong.position[2]+=3;memcpy(copy,cam,sizeof(cam));assert(rebuild(copy,wrong,chase,1)==LAYOUT);
}
 std::puts("PASS: scene view camera maths (layout detection, chase, turn, absolute, zoom, refusal)");}
