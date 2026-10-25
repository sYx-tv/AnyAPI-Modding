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
// model: x MODEL_* flags (a whole number), y the moon's angle from the sun in radians (clock sky; 0 otherwise),
// z weather cloud cover 0..1, w moon opacity 0..1 (K49; 0 = the V2 additive moon). All 0 is exactly the V2 pass.
// celestial: x/y cos and sin of the sky rotation, z seconds, w the night factor of the moonlight on surfaces. That light
// has no shadows, so it keeps the 0.40.1 timing on the game's own (parked) sun even when the sky follows the clock:
// on the clock it would light interiors through their roofs all night.
struct Frame {std::array<float,4> metrics{},right{},up{},forward{},sun{},moon{},celestial{},params{},sun_light{},sky_light{},clouds{},model{};};
static_assert(sizeof(Frame)==48*4);
enum : uint32_t {MODEL_CLOCK=1,MODEL_CLOUDS_V2=2,MODEL_STAR_SEARCH=4,MODEL_ALL=7};
constexpr double PI=3.14159265358979323846;
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
inline bool valid(const Frame& f){for(auto* a:{&f.metrics,&f.right,&f.up,&f.forward,&f.sun,&f.moon,&f.celestial,&f.params,&f.sun_light,&f.sky_light,&f.clouds,&f.model})for(auto v:*a)if(!std::isfinite(v))return false;
 if(f.model[0]<0||f.model[0]>MODEL_ALL||f.model[0]!=std::floor(f.model[0])||f.model[1]<0||f.model[1]>3.1416f||f.model[2]<0||f.model[2]>1||f.model[3]<0||f.model[3]>1)return false;
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
 float angle=std::atan2(sun[1],sun[2]);f.celestial={std::cos(angle),std::sin(angle),std::fmod(seconds,3600.f),night_factor(sun[1])};
 f.params={stars,moon,moonlight,stars};
 auto colour=[](const float* c,float scale){return c?std::array<float,3>{std::clamp(c[0]*scale,0.f,100.f),std::clamp(c[1]*scale,0.f,100.f),std::clamp(c[2]*scale,0.f,100.f)}:std::array<float,3>{0,0,0};};
 auto s=colour(sun_colour,1),k=colour(sky_colour,1);f.sun_light={s[0],s[1],s[2],sun_disc};f.sky_light={k[0],k[1],k[2],clouds};
 const double origin[3]{};auto c=cloud_position(camera?camera:origin,seconds);f.clouds={c[0],c[1],c[2],day_factor(sun[1])};return f;
}
// ---- sky_model 1: the night sky on the game clock (K16). Inputs are scene_frame::Clock fields, passed as plain
// values so this header stays free of the board.
struct ClockSky {double sun[3]{},moon[3]{};double night{};uint32_t source{};bool parked{};float phase{};};
// Clock.phase (0 full, .5 new) to the moon's angle from the sun seen from the ground, and the lit fraction.
inline float elongation(float phase){float p=phase-std::floor(phase);return float(PI*(1-2*std::min(p,1-p)));}
inline float lit_fraction(float e){return .5f-.5f*std::cos(e);}
inline bool unit(const double* v,float* out){if(!v)return false;double l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(!std::isfinite(l)||l<.5||l>2)return false;for(int i=0;i<3;++i)out[i]=float(v[i]/l);return true;}
// True when apply_clock() would accept c: a unit-length moon, and on the game clock (source 1) a unit-length sun.
inline bool clock_ok(const ClockSky& c){float v[3];return unit(c.moon,v)&&(c.source==2||(c.source==1&&unit(c.sun,v)));}
// Rewrites a frame from build() for the clock sky. The true sun is the clock's; on the fallback detector (source 2)
// it is the held moon turned half a day about world z (s(t) = Rz(pi) s(t+.5) = (-m.x,-m.y,m.z)), never the parked
// vector. By day the disc and the cloud light keep the game's own sun (native_sun). The star field turns about z
// by theta = atan2(s.x,-s.y), which is +-2*pi*t with the calibrated sign (Clock.star_angle carries no sign).
inline bool apply_clock(Frame& f,const ClockSky& c,const float* native_sun){
 float moon[3],truth[3];if(!clock_ok(c)||!native_sun||!unit(c.moon,moon))return false;
 if(c.source==2){const double s[3]{-c.moon[0],-c.moon[1],c.moon[2]};unit(s,truth);}else unit(c.sun,truth);
 const float* sun=c.parked||c.source==2?truth:native_sun;float night=float(std::clamp(std::isfinite(c.night)?c.night:0.,0.,1.));
 float theta=std::atan2(truth[0],-truth[1]),e=elongation(std::isfinite(c.phase)?c.phase:0.f);
 f.sun={sun[0],sun[1],sun[2],night};f.moon={moon[0],moon[1],moon[2],smoothstep(-.02f,.12f,moon[1])};
 f.celestial[0]=std::cos(theta);f.celestial[1]=std::sin(theta);f.clouds[3]=day_factor(sun[1]);
 // SKY-10: stars and milky way only once the sun is 2 degrees down and against a dark sky; SKY-16 dims moonlight with the phase.
 float sky=.2126f*f.sky_light[0]+.7152f*f.sky_light[1]+.0722f*f.sky_light[2],fade=smoothstep(-.03f,-.2f,truth[1])*std::clamp(1-sky/.03f,0.f,1.f);
 f.params[0]*=fade;f.params[3]*=fade;f.params[2]*=std::max(lit_fraction(e),.35f);
 f.model[0]=float(uint32_t(f.model[0])|MODEL_CLOCK);f.model[1]=e;return true;}
// K49: the moon disc at the direction the game lights the night from (scene_controls v4 moon_mode 1), opaque.
inline bool apply_moon_light(Frame& f,const double* direction,float opacity){float d[3];if(!unit(direction,d))return false;
 f.moon={d[0],d[1],d[2],smoothstep(-.02f,.12f,d[1])};f.model[3]=std::clamp(opacity,0.f,1.f);return true;}
// K18 (weather on): cover 0..1 (Weather.cover; 0 without a source) and fog_ratio .5..4 (Weather.fog_ratio).
inline void apply_weather(Frame& f,float cover,float fog_ratio){cover=std::isfinite(cover)?std::clamp(cover,0.f,1.f):0.f;fog_ratio=std::isfinite(fog_ratio)?fog_ratio:1.f;
 float seen=(1-.9f*cover)*std::exp(-.6f*std::clamp(fog_ratio-1,0.f,3.f));
 f.params[0]*=seen;f.params[1]*=seen;f.params[3]*=seen;f.sun_light[3]*=seen;f.model[3]*=seen;f.params[2]*=1-.7f*cover;
 f.sky_light[3]=std::min(2.f,f.sky_light[3]*(1+1.2f*cover));f.clouds[3]*=1-.6f*cover;f.model[2]=cover;}
// K19 (clouds v2): the deck drifts with the game's wind (scene+0xb78, m/s), 1.5x faster at 1.5 km, integrated in
// double. offset (m) is kept in [0, cloud_period*cloud_cell); the field is sampled at camera+offset, so a feature
// moves by -offset: subtracting v*dt moves it with the wind.
constexpr double cloud_fallback_wind[2]={9.4,3.4};   // 15 m/s at the deck from the WSW, as V2's fixed wind
// wind: validated by the caller (finite, |v| <= 200), or null for the fallback.
inline void drift(double* offset,const double* wind,double dt){const double span=double(cloud_period)*cloud_cell,*w=wind?wind:cloud_fallback_wind;if(!std::isfinite(dt)||dt<0)dt=0;
 for(int i=0;i<2;++i){double x=std::fmod(offset[i]-std::clamp(w[i]*1.5,-60.,60.)*dt,span);if(!std::isfinite(x))x=0;offset[i]=x<0?x+span:x;}}
inline std::array<float,3> cloud_field(const double* camera,const double* offset){std::array<float,3> c{};for(int i=0;i<2;++i){double x=std::fmod((camera[i*2]+offset[i])/cloud_cell,double(cloud_period));if(x<0)x+=cloud_period;c[i*2]=std::min(float(x),cloud_period);}
 c[1]=float(std::clamp(double(cloud_base)-camera[1],-1e6,1e6));return c;}
}
