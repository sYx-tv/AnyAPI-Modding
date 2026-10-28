#pragma once
#include <array>
#include <cmath>
// Root constants of the bounce light pass; layout matches cbuffer Frame in gi.hlsl.
namespace scene_gi_gpu {
// extra: shading revision (0 = V2, 1 = screen-share footprint cap and occlusion test), unused x3.
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},projection{},params{},extra{};};
static_assert(sizeof(Frame)==28*4);
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.projection,&f.params,&f.extra})for(auto v:*a)if(!std::isfinite(v))return false;
 if(f.extra[0]!=0&&f.extra[0]!=1)return false;
 return f.metrics[2]>=16&&f.metrics[3]>=16&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.forward[3]>=1&&f.params[0]>=.25f&&f.params[0]<=8&&f.params[1]>=0&&f.params[1]<=2&&f.params[3]>=1&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f;}
}
