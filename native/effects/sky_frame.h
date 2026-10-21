#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
// Root constants of the night sky pass; layout matches cbuffer Frame in sky.hlsl.
// The pass adds stars, a milky way and a moon to sky pixels and soft moonlight
// to surfaces. Everything follows the game's own sun, so it rides the day/night
// cycle: the star field turns with the sun, and the moon sits opposite it.
namespace scene_sky_gpu {
// sun_light: the game's sun colour, w sun disc strength. sky_light: the game's
// sky (ambient) colour, w cloud amount. clouds: x/z where the camera sits in the
// cloud field (noise cells, wrapped every cloud_period), y the cloud base height
// above the camera in metres, w daylight factor.
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},sun{},moon{},celestial{},params{},sun_light{},sky_light{},clouds{};};
static_assert(sizeof(Frame)==44*4);
inline float smoothstep(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
// 0 in daylight, 1 once the sun is well below the horizon.
inline float night_factor(float sun_y){return smoothstep(.05f,-.12f,sun_y);}
// The moon rises as the sun sets: opposite the sun, lifted so it stands high at midnight.
inline std::array<float,3> moon_direction(const float* sun){float m[3]={-sun[0],-sun[1]+.35f,-sun[2]};float l=std::sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);if(l<1e-4f)return {0,1,0};return {m[0]/l,m[1]/l,m[2]/l};}
// The cloud field: one noise cell is cloud_cell metres, the field repeats every
// cloud_period cells (the shader's hash wraps there too, so wrapping is seamless),
// and the cloud base sits cloud_base metres above the world origin.
constexpr float cloud_period=256,cloud_cell=1400,cloud_base=1500;
// Wind: about 15 m/s from the west-south-west.
constexpr double cloud_wind[2]={14.1,5.1};
// Camera world position and wall time to the cloud constants (computed in double so
// they stay precise however far the player travels).
inline std::array<float,3> cloud_position(const double* camera,double seconds){std::array<float,3> c{};for(int i=0;i<2;++i){double x=(camera[i*2]+cloud_wind[i]*seconds)/cloud_cell;x=std::fmod(x,double(cloud_period));if(x<0)x+=cloud_period;c[i*2]=std::min(float(x),cloud_period);}
 c[1]=float(std::clamp(double(cloud_base)-camera[1],-1e6,1e6));return c;}
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.sun,&f.moon,&f.celestial,&f.params,&f.sun_light,&f.sky_light,&f.clouds})for(auto v:*a)if(!std::isfinite(v))return false;
 return f.metrics[2]>=16&&f.metrics[3]>=16&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.sun[3]>=0&&f.sun[3]<=1&&f.moon[3]>=0&&f.moon[3]<=1&&f.params[0]>=0&&f.params[0]<=2&&f.params[1]>=0&&f.params[1]<=2&&f.params[2]>=0&&f.params[2]<=2&&f.params[3]>=0&&f.params[3]<=2
  &&f.sun_light[3]>=0&&f.sun_light[3]<=2&&f.sky_light[3]>=0&&f.sky_light[3]<=2&&f.clouds[3]>=0&&f.clouds[3]<=1&&f.clouds[0]>=0&&f.clouds[0]<=cloud_period&&f.clouds[2]>=0&&f.clouds[2]<=cloud_period&&std::abs(f.clouds[1])<=1e6f
  &&f.sun_light[0]>=0&&f.sun_light[1]>=0&&f.sun_light[2]>=0&&f.sun_light[0]<=100&&f.sun_light[1]<=100&&f.sun_light[2]<=100&&f.sky_light[0]>=0&&f.sky_light[1]>=0&&f.sky_light[2]>=0&&f.sky_light[0]<=100&&f.sky_light[1]<=100&&f.sky_light[2]<=100;}
// 1 in daylight, 0 once the sun has set.
inline float day_factor(float sun_y){return smoothstep(-.1f,.05f,sun_y);}
// sun: unit vector toward the sun. seconds: wall time for star twinkle and
// cloud drift. sun_colour/sky_colour: the game's light colours (or null).
inline Frame build(const float* right,const float* up,const float* forward,const float* sun,uint32_t width,uint32_t height,float seconds,float stars,float moon,float moonlight,const float* sun_colour=nullptr,const float* sky_colour=nullptr,float sun_disc=0,float clouds=0,const double* camera=nullptr){
 Frame f;f.metrics={1.f/width,1.f/height,float(width),float(height)};
 for(int i=0;i<4;++i){f.right[i]=right[i];f.up[i]=up[i];f.forward[i]=forward[i];}
 auto m=moon_direction(sun);f.sun={sun[0],sun[1],sun[2],night_factor(sun[1])};f.moon={m[0],m[1],m[2],smoothstep(-.02f,.12f,m[1])};
 // The star field turns about the same axis as the sun (the game rotates it about x).
 float angle=std::atan2(sun[1],sun[2]);f.celestial={std::cos(angle),std::sin(angle),std::fmod(seconds,3600.f),0};
 f.params={stars,moon,moonlight,stars};
 auto colour=[](const float* c,float scale){return c?std::array<float,3>{std::clamp(c[0]*scale,0.f,100.f),std::clamp(c[1]*scale,0.f,100.f),std::clamp(c[2]*scale,0.f,100.f)}:std::array<float,3>{0,0,0};};
 auto s=colour(sun_colour,1),k=colour(sky_colour,1);f.sun_light={s[0],s[1],s[2],sun_disc};f.sky_light={k[0],k[1],k[2],clouds};
 const double origin[3]{};auto c=cloud_position(camera?camera:origin,seconds);f.clouds={c[0],c[1],c[2],day_factor(sun[1])};return f;
}
}
