#pragma once
#include <cmath>
// Native math callable cells are resolved from the exact current camera body.
// Never assume the engine's matrix storage or rotation sine convention.
namespace player_heading {
using RotationFn=void(*)(double*,const double*);
using TransformFn=void(*)(double*,const double*,const double*);
struct NativeMath {RotationFn rotation_y{};TransformFn transform{};};
using ViewFn=void(*)(double*,void*);
// Use the same +Z view ray as native actor.get_interaction_ray.
inline bool view_yaw(void* actor,ViewFn getter,double& out,NativeMath math){
 if(!actor||!getter||!math.transform)return false;
 double view[12]{},world[3]{};const double forward[3]={0,0,1};getter(view,actor);
 for(int i=0;i<9;++i)if(!std::isfinite(view[i]))return false;
 math.transform(world,view,forward);
 if(!std::isfinite(world[0])||!std::isfinite(world[2])||std::hypot(world[0],world[2])<1e-8)return false;
 out=std::atan2(world[0],world[2]);return true;
}
// Body/local-yaw utility is retained for diagnostic fixtures; published heading uses view_yaw.
inline bool world_yaw(const double* matrix,double local_yaw,double& out,NativeMath math){
 if(!matrix||!math.rotation_y||!math.transform||!std::isfinite(local_yaw))return false;
 for(int i=0;i<9;++i)if(!std::isfinite(matrix[i]))return false;
 double rotation[9]{},local[3]{},world[3]{};const double forward[3]={0,0,1};
 math.rotation_y(rotation,&local_yaw);math.transform(local,rotation,forward);math.transform(world,matrix,local);
 if(!std::isfinite(world[0])||!std::isfinite(world[2])||std::hypot(world[0],world[2])<1e-8)return false;
 out=std::atan2(world[0],world[2]);return true;
}
}
