#pragma once
#include <array>
#include <cmath>
// Root constants of the scene view display pass; layout matches cbuffer Frame
// in view.hlsl. The pass lights the view's own gbuffer (albedo, normal, depth)
// with the game's sun, sky and fog colours and draws it into a screen rectangle.
namespace scene_view_gpu {
// inverse: canonical inverse view-projection, row-major, world = M * ndc.
// camera: view position (same space), w exposure. sun: toward the sun.
// The destination rectangle is the viewport. options: x far plane for the base fog (0 none), y 1 to gamma-encode.
struct Frame {std::array<float,16> inverse{};std::array<float,4> camera{},sun{},sun_light{},sky_light{},back_light{},view_up{},view_down{},options{};};
static_assert(sizeof(Frame)==48*4);
inline bool valid(const Frame& f){for(float v:f.inverse)if(!std::isfinite(v))return false;for(auto* a:{&f.camera,&f.sun,&f.sun_light,&f.sky_light,&f.back_light,&f.view_up,&f.view_down,&f.options})for(float v:*a)if(!std::isfinite(v)||std::abs(v)>1e9f)return false;
 return f.camera[3]>0&&f.camera[3]<=16&&f.options[0]>=0;}
}
