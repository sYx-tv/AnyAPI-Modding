#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "../lighting/native_frame.h"
// Root constants of the scene effects pass; layout matches cbuffer Frame in
// effects.hlsl. extras.zw are rewritten per draw with that pass's texel size.
namespace scene_effects_gpu {
struct Frame {std::array<float,4> metrics{},sun_screen{},sun_colour{},grading{},exposure{},extras{};};
static_assert(sizeof(Frame)==24*4);
inline bool finite(const std::array<float,4>& a){for(auto v:a)if(!std::isfinite(v))return false;return true;}
inline bool valid(const Frame& f){
 return finite(f.metrics)&&finite(f.sun_screen)&&finite(f.sun_colour)&&finite(f.grading)&&finite(f.exposure)&&finite(f.extras)&&f.metrics[2]>=16&&f.metrics[3]>=16
  &&f.grading[0]>=0&&f.grading[0]<=2&&f.grading[1]>=0&&f.grading[1]<=5&&f.grading[2]>=0&&f.grading[2]<=1&&std::abs(f.grading[3])<=3
  &&f.exposure[1]>=0&&f.exposure[1]<=1&&f.exposure[2]>=0&&f.exposure[2]<=1&&f.exposure[3]>=0&&f.exposure[3]<=4&&f.extras[0]>=0&&f.extras[0]<=1&&f.extras[1]>=0&&f.extras[1]<=1
  &&f.sun_colour[0]>=0&&f.sun_colour[1]>=0&&f.sun_colour[2]>=0&&f.sun_colour[0]<=10000&&f.sun_colour[1]<=10000&&f.sun_colour[2]<=10000&&f.sun_colour[3]>=0&&f.sun_colour[3]<=2;
}
// Native inputs: HDR target, depth, main camera, sun direction and colour.
struct Native {volumetric::NativeTarget color,depth;float right[4]{},up[4]{},forward[4]{},sun[3]{},sun_colour[3]{};uint32_t failure{};};
template<class Read> bool capture(uintptr_t renderer,uintptr_t scene,Native& out,Read read){
 out.failure=1;unsigned char special[4]{};if(!read(scene+0x668,special,4)||special[0]||special[1]||special[2]||special[3])return false;
 out.failure=2;uintptr_t color{},depth{};if(!read(renderer+0x3c0,&color,8)||!read(renderer+0x218,&depth,8)||!volumetric::target(color,out.color,read)||!volumetric::target(depth,out.depth,read)||out.color.srv_format!=10||out.depth.srv_format!=41||out.color.width!=out.depth.width||out.color.height!=out.depth.height)return false;
 out.failure=3;uintptr_t main=scene+0x20;double v[3]{},matrix[16]{};
 auto vector=[&](uintptr_t address,float* destination){if(!read(address,v,sizeof(v)))return false;for(int i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;destination[i]=float(v[i]);}return true;};
 if(!vector(main+0xa8,out.right)||!vector(main+0x90,out.up)||!vector(main+0x78,out.forward)||!vector(scene+0xa78,out.sun)||!vector(scene+0xaf0,out.sun_colour)||!read(main+0x1a0,matrix,sizeof(matrix)))return false;
 out.failure=4;if(std::abs(matrix[0])<1e-8||std::abs(matrix[5])<1e-8||!std::isfinite(matrix[0])||!std::isfinite(matrix[5]))return false;out.right[3]=float(1/std::abs(matrix[0]));out.up[3]=float(1/std::abs(matrix[5]));
 out.failure=5;float m=0;for(auto s:out.sun)m+=s*s;if(m<.01f)return false;for(auto& s:out.sun)s=-s/std::sqrt(m);
 for(auto c:out.sun_colour)if(c<0||c>10000)return false;
 out.failure=0;return true;
}
// Sun position on screen: uv, whether glare can show, and the sun's height.
inline std::array<float,4> sun_screen(const Native& n){
 float z=n.sun[0]*n.forward[0]+n.sun[1]*n.forward[1]+n.sun[2]*n.forward[2];std::array<float,4> s{0,0,0,n.sun[1]};if(z<.05f)return s;
 float x=(n.sun[0]*n.right[0]+n.sun[1]*n.right[1]+n.sun[2]*n.right[2])/(z*n.right[3]),y=(n.sun[0]*n.up[0]+n.sun[1]*n.up[1]+n.sun[2]*n.up[2])/(z*n.up[3]);
 s[0]=x*.5f+.5f;s[1]=-y*.5f+.5f;s[2]=std::abs(x)<1.6f&&std::abs(y)<1.6f&&n.sun[1]>-.02f?1.f:0.f;return s;
}
}
