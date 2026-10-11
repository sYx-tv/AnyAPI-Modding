#pragma once
#include <array>
#include <cmath>
// Root constants of the ambient occlusion pass; layout matches cbuffer Frame in ao.hlsl.
namespace scene_ao_gpu {
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},projection{},params{};};
static_assert(sizeof(Frame)==24*4);
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.projection,&f.params})for(auto v:*a)if(!std::isfinite(v))return false;
 return f.metrics[2]>=16&&f.metrics[3]>=16&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.forward[3]>=1&&f.params[0]>=.1f&&f.params[0]<=8&&f.params[1]>=0&&f.params[1]<=3&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f;}
}
