#pragma once
#include "anyapi_creation_balance_v1.h"
#include <cmath>
#include <algorithm>
#include <mutex>
namespace balance {
inline bool finite(AnyBalancePointV1 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<1e9&&std::abs(p.y)<1e9&&std::abs(p.z)<1e9;}
inline AnyBalancePointV1 transform(const double* m,AnyBalancePointV1 p){return {m[0]*p.x+m[3]*p.y+m[6]*p.z+m[9],m[1]*p.x+m[4]*p.y+m[7]*p.z+m[10],m[2]*p.x+m[5]*p.y+m[8]*p.z+m[11]};}
// Native mat34 basis columns are orthonormal; inverse uses their transpose.
inline AnyBalancePointV1 inverse_transform(const double* m,AnyBalancePointV1 p){p.x-=m[9];p.y-=m[10];p.z-=m[11];return {m[0]*p.x+m[1]*p.y+m[2]*p.z,m[3]*p.x+m[4]*p.y+m[5]*p.z,m[6]*p.x+m[7]*p.y+m[8]*p.z};}
struct Camera {double transform[12]{},projection[16]{};};
inline AnyBalanceScreenV1 project(const Camera& camera,AnyBalancePointV1 world){
 auto p=inverse_transform(camera.transform,world);auto m=camera.projection;
 double x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
 double w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
 if(!std::isfinite(w)||w<=1e-8||p.z<=0)return {0,0,-1};return {x/w,y/w,p.z};
}
// The renderer consumes graphics-relative world coordinates through its
// final view-projection matrix, including camera effects and viewport aspect.
struct RenderCamera {double matrix[16]{};AnyBalancePointV1 origin{};};
inline AnyBalanceScreenV1 project(const RenderCamera& camera,AnyBalancePointV1 world){
 auto m=camera.matrix;world.x-=camera.origin.x;world.y-=camera.origin.y;world.z-=camera.origin.z;
 double x=m[0]*world.x+m[4]*world.y+m[8]*world.z+m[12];
 double y=m[1]*world.x+m[5]*world.y+m[9]*world.z+m[13];
 double w=m[3]*world.x+m[7]*world.y+m[11]*world.z+m[15];
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||w<=1e-8)return {0,0,-1};
 return {x/w,y/w,w};
}
inline bool derive(AnyCreationBalanceSnapshotV1& s){
 if(!finite(s.centre_local)||!finite(s.bounds_min)||!finite(s.bounds_max))return false;
 double lo[]={s.bounds_min.x,s.bounds_min.y,s.bounds_min.z},hi[]={s.bounds_max.x,s.bounds_max.y,s.bounds_max.z};
 for(int i=0;i<3;++i)if(hi[i]<lo[i]||hi[i]-lo[i]>10000)return false;
 s.height_m=s.centre_local.y-s.bounds_min.y;
 s.offset_x_m=s.centre_local.x-(s.bounds_min.x+s.bounds_max.x)*.5;
 s.offset_z_m=s.centre_local.z-(s.bounds_min.z+s.bounds_max.z)*.5;
 return true;
}
inline bool pixel(AnyBalanceScreenV1 p,uint32_t w,uint32_t h,float& x,float& y){
 if(!w||!h||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.depth)||p.depth<=1e-7||std::abs(p.x)>8||std::abs(p.y)>8)return false;
 x=float((p.x+1)*.5*w);y=float((1-p.y)*.5*h);return std::isfinite(x)&&std::isfinite(y);
}
class Store {
 mutable std::mutex mutex_;AnyCreationBalanceSnapshotV1 data_;bool valid_{};
public:
 void publish(const AnyCreationBalanceSnapshotV1& v){std::lock_guard lock(mutex_);data_=v;valid_=true;}
 void clear(){std::lock_guard lock(mutex_);valid_=false;}
 bool copy(AnyCreationBalanceSnapshotV1* out,uint64_t now)const{if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;std::lock_guard lock(mutex_);if(!valid_||now<data_.sampled_tick||now-data_.sampled_tick>150)return false;*out=data_;return true;}
};
}
