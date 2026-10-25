#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
// Root constants of the ocean reflection pass; layout matches cbuffer Frame in water.hlsl.
// The pass finds the sea surface itself (the plane y = 0 below the camera), so
// gbuffer_ocean is only used as the ocean mask (y > 0), never for its distance.
namespace scene_water_gpu {
// camera: x height above sea level (m), y/z the camera's world x/z wrapped into the ripple tile, w frame index (step jitter).
// ripple: xy, zw offsets (m) of the two ripple layers drifting with the wind, wrapped into the tile.
// sun: unit vector toward the sun, w above-horizon fade. sun_light: the game's sun colour, w glint strength 0..2.
// view_up, view_down, light_up, light_down: the game's sky colours, to estimate what its water already reflects;
// w: reflection strength 0..1, fog distance (m), ripple strength 0..1, glint roughness.
// params: x linear ray steps (16, 24 or 32), y hit thickness (m), zw the TAA jitter of this frame in uv (0 without TAA).
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},projection{},camera{},ripple{},sun{},sun_light{},view_up{},view_down{},light_up{},light_down{},params{};};
static_assert(sizeof(Frame)==56*4);
// The ripple noise repeats every ripple_tile metres (its hash wraps there), so positions wrap without a jump.
// glint_roughness is the calm-water GGX alpha (K48); wind and rain widen it in glint_alpha().
constexpr float ripple_tile=512,glint_roughness=.1f,longest_ray=100000,hit_thickness=.6f;
constexpr uint32_t steps_for(uint32_t quality){return quality>=2?32:quality==1?24:16;}
inline float smoothstep(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
inline float wrap(double x){x=std::fmod(x,double(ripple_tile));if(x<0)x+=ripple_tile;return std::min(float(x),ripple_tile);}
// Two ripple layers: one drifts with the wind, the other slower and across it, so the pattern never just slides.
// K19: the offsets are a phase integrated from this frame's wind (m/s) over dt in double and wrapped into the
// tile, so a change of wind changes the speed, never the position.
inline std::array<double,4> ripple_velocity(const double* wind){double w[2]{wind?wind[0]:0,wind?wind[1]:0};return {-w[0]*.35,-w[1]*.35,w[1]*.2-.6,-w[0]*.2-.4};}
inline void advance_ripples(double* phase,const double* wind,double dt){if(!std::isfinite(dt)||dt<0)dt=0;auto v=ripple_velocity(wind);
 for(int i=0;i<4;++i){double x=std::fmod(phase[i]+v[i]*dt,double(ripple_tile));if(!std::isfinite(x))x=0;phase[i]=x<0?x+ripple_tile:x;}}
inline std::array<float,4> ripple_offsets(const double* phase){return {wrap(phase?phase[0]:0),wrap(phase?phase[1]:0),wrap(phase?phase[2]:0),wrap(phase?phase[3]:0)};}
// Calm water still ripples a little; a stiff breeze (10 m/s and up) gives the full chop.
inline float ripple_strength(const double* wind,float rain=0){double s=wind?std::sqrt(wind[0]*wind[0]+wind[1]*wind[1]):0;return float(std::min(1.,.35+.65*std::clamp(s/10,0.,1.)+.3*std::clamp(double(rain),0.,1.)));}
// K48: the glint's GGX alpha. Calm water is glint_roughness; wind adds the Cox-Munk slope variance .003+.00512*U
// (U m/s, half of it, since the ripple normals carry the rest near the camera), rain adds up to .08 (weather on).
inline float glint_alpha(const double* wind,float rain=0){double u=wind?std::min(20.,std::sqrt(wind[0]*wind[0]+wind[1]*wind[1])):0;
 return float(std::clamp(std::sqrt(double(glint_roughness)*glint_roughness+(.003+.00512*u))+.08*std::clamp(double(rain),0.,1.),.05,.4));}
// Distance at which the game's fog has fully covered the scene: its far plane (reversed depth reaches 0 there).
inline float fog_distance(const float* projection){float a=projection[0],b=projection[1];float limit=std::abs(a)>1e-12f?std::abs(b/a):1e6f;return std::isfinite(limit)?std::clamp(limit,100.f,1e6f):1e6f;}
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.projection,&f.camera,&f.ripple,&f.sun,&f.sun_light,&f.view_up,&f.view_down,&f.light_up,&f.light_down,&f.params})for(auto v:*a)if(!std::isfinite(v))return false;
 for(auto* a:{&f.sun_light,&f.view_up,&f.view_down,&f.light_up,&f.light_down})for(int i=0;i<3;++i)if((*a)[i]<0||(*a)[i]>10000)return false;
 for(auto v:f.ripple)if(v<0||v>ripple_tile)return false;
 return f.metrics[2]>=16&&f.metrics[3]>=16&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.forward[3]>=1&&f.forward[3]<=100000&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f
  &&f.camera[0]>0&&f.camera[0]<=100000&&f.camera[1]>=0&&f.camera[1]<=ripple_tile&&f.camera[2]>=0&&f.camera[2]<=ripple_tile&&f.camera[3]>=0&&f.camera[3]<64
  &&f.sun[3]>=0&&f.sun[3]<=1&&f.sun_light[3]>=0&&f.sun_light[3]<=2&&f.view_up[3]>=0&&f.view_up[3]<=1&&f.view_down[3]>=100&&f.view_down[3]<=1e6f&&f.light_up[3]>=0&&f.light_up[3]<=1&&f.light_down[3]>=.02f&&f.light_down[3]<=.5f
  &&f.params[0]>=8&&f.params[0]<=64&&f.params[1]>0&&f.params[1]<=10&&std::abs(f.params[2])<=.05f&&std::abs(f.params[3])<=.05f;}
// right/up/forward: camera axes (w: tangents of the half fields of view). projection: A B C D of
// depth = (A*z + B) / (C*z + D). camera: world position (doubles). wind: the game's wind (m/s, x and z).
// sky: view up, view down, light up and light down colours. sun: unit vector toward the sun. phase: the four
// integrated ripple offsets (advance_ripples). frame: the step-jitter index (0 without TAA: a fixed pattern).
// jitter: this frame's TAA jitter in uv (x right, y down), or null. rain: Weather.rain_fx with weather on, else 0.
inline Frame build(const float* right,const float* up,const float* forward,const float* projection,uint32_t width,uint32_t height,const double* camera,const double* wind,const double* phase,const float* sun,const float* sun_colour,const float (*sky)[3],float reflections,float glint,uint32_t quality,uint32_t frame,const float* jitter=nullptr,float rain=0){
 Frame f;f.metrics={1.f/width,1.f/height,float(width),float(height)};
 for(int i=0;i<4;++i){f.right[i]=right[i];f.up[i]=up[i];f.forward[i]=forward[i];f.projection[i]=projection[i];}
 // WATER-1: rays reach as far as the game's fog (screen steps are linear, so reach costs no precision), capped at 100 km.
 f.forward[3]=std::min(fog_distance(projection),longest_ray);
 f.camera={float(std::clamp(camera[1],.05,1e5)),wrap(camera[0]),wrap(camera[2]),float(frame%64)};f.ripple=ripple_offsets(phase);
 f.sun={sun[0],sun[1],sun[2],smoothstep(-.02f,.05f,sun[1])};f.sun_light={sun_colour[0],sun_colour[1],sun_colour[2],glint};
 std::array<float,4>* colours[4]={&f.view_up,&f.view_down,&f.light_up,&f.light_down};for(int c=0;c<4;++c)for(int i=0;i<3;++i)(*colours[c])[i]=sky[c][i];
 f.view_up[3]=reflections;f.view_down[3]=fog_distance(projection);f.light_up[3]=ripple_strength(wind,rain);f.light_down[3]=glint_alpha(wind,rain);
 f.params={float(steps_for(quality)),hit_thickness,jitter?std::clamp(jitter[0],-.05f,.05f):0.f,jitter?std::clamp(jitter[1],-.05f,.05f):0.f};return f;
}
}
