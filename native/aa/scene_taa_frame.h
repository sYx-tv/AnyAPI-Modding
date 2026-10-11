#pragma once
#include "../lighting/native_frame.h"
#include <cmath>
// Camera and depth for temporal AA, read at the pre-HUD boundary with the same
// reviewed 0.1.24 offsets as the lighting frame (see lighting/native_frame.h).
namespace scene_taa {
struct Camera {double position[3]{};std::array<float,4> right{},up{},forward{},projection{};};
struct Capture {volumetric::NativeTarget depth;Camera camera;uint32_t failure{};};
template<class Read> bool capture(uintptr_t renderer,uintptr_t scene,Capture& out,Read read){
 out.failure=1;unsigned char special[4]{};if(!read(scene+0x668,special,4)||special[0]||special[1]||special[2]||special[3])return false;
 out.failure=2;uintptr_t depth{};if(!read(renderer+0x218,&depth,8)||!volumetric::target(depth,out.depth,read)||out.depth.srv_format!=41)return false;
 out.failure=3;uintptr_t main=scene+0x20;double v[3]{},offset[3]{},matrix[16]{};auto& c=out.camera;
 auto axis=[&](uintptr_t at,std::array<float,4>& to){if(!read(at,v,sizeof(v)))return false;double n=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];if(!std::isfinite(n)||n<.98||n>1.02)return false;for(size_t i=0;i<3;++i)to[i]=float(v[i]);return true;};
 if(!read(main+0x108,v,sizeof(v))||!read(scene+8,offset,sizeof(offset)))return false;for(size_t i=0;i<3;++i){if(!std::isfinite(v[i])||!std::isfinite(offset[i])||std::abs(v[i])>1e8||std::abs(offset[i])>1e12)return false;c.position[i]=v[i]+offset[i];}
 out.failure=4;if(!axis(main+0xa8,c.right)||!axis(main+0x90,c.up)||!axis(main+0x78,c.forward))return false;
 out.failure=5;if(!read(main+0x1a0,matrix,sizeof(matrix)))return false;for(auto m:matrix)if(!std::isfinite(m))return false;
 if(std::abs(matrix[0])<1e-8||std::abs(matrix[5])<1e-8||std::abs(matrix[11])<.5||std::abs(matrix[15])>.001)return false;
 c.projection={float(matrix[10]),float(matrix[14]),float(matrix[11]),float(matrix[15])};c.right[3]=float(1/std::abs(matrix[0]));c.up[3]=float(1/std::abs(matrix[5]));c.forward[3]=10000;
 if(c.right[3]>=10||c.up[3]>=10||std::abs(c.projection[0]*c.projection[3]-c.projection[1]*c.projection[2])<1e-8f)return false;
 out.failure=0;return true;
}
}
