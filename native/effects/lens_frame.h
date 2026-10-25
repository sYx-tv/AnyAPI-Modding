#pragma once
#include <array>
#include <cmath>
// Root constants of the lens pass (depth of field and camera motion blur);
// layout matches cbuffer Frame in lens.hlsl.
namespace scene_lens_gpu {
// sea: camera height above sea level (m), 1 = ocean mask bound (else 0), unused, unused.
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},projection{},previous_right{},previous_up{},previous_forward{},camera_move{},params{},sea{};};
static_assert(sizeof(Frame)==44*4);
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.projection,&f.previous_right,&f.previous_up,&f.previous_forward,&f.camera_move,&f.params,&f.sea})for(auto v:*a)if(!std::isfinite(v))return false;
 if(std::abs(f.sea[0])>1e6||(f.sea[1]!=0&&f.sea[1]!=1))return false;
 return f.metrics[2]>=16&&f.metrics[3]>=16&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.previous_right[3]>=0&&f.previous_right[3]<10&&f.previous_up[3]>=0&&f.previous_up[3]<10&&f.forward[3]>=1
  &&f.params[0]>=0&&f.params[0]<=4&&f.params[1]>=0&&f.params[1]<=2&&f.params[2]>=1&&f.params[2]<=64&&std::abs(f.camera_move[0])<1000&&std::abs(f.camera_move[1])<1000&&std::abs(f.camera_move[2])<1000&&f.camera_move[3]>0&&f.camera_move[3]<=1&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f;}
}
