#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "effects/sky_frame.h"
#include "scene_celestial.h"
using Microsoft::WRL::ComPtr;
// Compiles the night sky shaders, checks constant size and sampler
// slots, and optionally writes the bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace sky_bytecode {\n";}
 for(auto entry:{"Vertex","Sky"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
  if(FAILED(hr)){if(error)std::cerr<<(const char*)error->GetBufferPointer();return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));if(b.Type==D3D_SIT_TEXTURE)assert(b.BindPoint<4);}
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_sky_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 // Day/night: no night at noon, full night at midnight; the moon sits opposite the sun and is high at midnight.
 assert(scene_sky_gpu::night_factor(.8f)==0&&scene_sky_gpu::night_factor(.05f)==0&&scene_sky_gpu::night_factor(-.5f)==1&&scene_sky_gpu::night_factor(-.03f)>0&&scene_sky_gpu::night_factor(-.03f)<1);
 float midnight[3]={0,-1,0},dusk[3]={0,0,1};auto m=scene_sky_gpu::moon_direction(midnight);assert(m[1]>.99f);m=scene_sky_gpu::moon_direction(dusk);assert(m[2]<-.9f&&m[1]>0);
 float right[4]{1,0,0,.8f},up[4]{0,1,0,.45f},forward[4]{0,0,1,0};auto f=scene_sky_gpu::build(right,up,forward,midnight,64,64,12.5f,1,1,1);assert(scene_sky_gpu::valid(f)&&f.sun[3]==1&&f.moon[3]==1&&std::abs(f.celestial[0]*f.celestial[0]+f.celestial[1]*f.celestial[1]-1)<1e-5f);
 float noon[3]={0,1,0};f=scene_sky_gpu::build(right,up,forward,noon,64,64,0,1,1,1);assert(scene_sky_gpu::valid(f)&&f.sun[3]==0&&f.moon[3]==0);
 auto bad=f;bad.params[1]=3;assert(!scene_sky_gpu::valid(bad));bad=f;bad.sun[3]=NAN;assert(!scene_sky_gpu::valid(bad));bad=f;bad.metrics[2]=4;assert(!scene_sky_gpu::valid(bad));
 assert(scene_sky_gpu::day_factor(.5f)==1&&scene_sky_gpu::day_factor(-.5f)==0);const float sun_colour[3]{2,1.8f,1.5f},sky_colour[3]{.3f,.4f,.6f};f=scene_sky_gpu::build(right,up,forward,noon,64,64,0,0,0,0,sun_colour,sky_colour,1,1.5f);
 assert(scene_sky_gpu::valid(f)&&f.sun_light[0]==2&&f.sun_light[3]==1&&f.sky_light[3]==1.5f&&f.clouds[3]==1);bad=f;bad.sky_light[3]=3;assert(!scene_sky_gpu::valid(bad));bad=f;bad.sun_light[1]=-1;assert(!scene_sky_gpu::valid(bad));
 using namespace scene_sky_gpu;
 // V1/V2 frames carry model 0: with every flag clear the shader is the V2 pass.
 assert(sizeof(Frame)==192&&(f.model==std::array<float,4>{0,0,0,0}));
 bad=f;bad.model[0]=8;assert(!valid(bad));bad=f;bad.model[0]=1.5f;assert(!valid(bad));bad=f;bad.model[1]=3.2f;assert(!valid(bad));bad=f;bad.model[2]=-.1f;assert(!valid(bad));bad=f;bad.model[3]=1.1f;assert(!valid(bad));bad=f;bad.model[0]=MODEL_ALL;assert(valid(bad));
 // SKY-16: phase 0 full (moon opposite the sun), .25 quarter, .5 new; the lit fraction matches Clock.lit_fraction.
 assert(std::abs(elongation(0)-PI)<1e-6&&std::abs(elongation(.25f)-PI/2)<1e-6&&elongation(.5f)==0&&std::abs(elongation(.75f)-PI/2)<1e-6&&std::abs(elongation(1.25f)-PI/2)<1e-6);
 for(int i=0;i<8;++i){float p=i/8.f;assert(std::abs(lit_fraction(elongation(p))-(1+std::cos(2*PI*p))/2)<1e-5);}
 // K16 on the calibrated clock, both sign pairs the game might use.
 const float dark[3]{.002f,.002f,.003f};
 const float dark_stars=1-(.2126f*dark[0]+.7152f*dark[1]+.0722f*dark[2])/.03f;
 for(int sx:{1,-1})for(int sz:{1,-1}){celestial::Calibration cal{};
  // The game's stored sun (scene+0xa78 points away from the sun), parked at noon for t<.2||t>.8.
  auto native_at=[&](double t,double* native,float* toward){double s[3];celestial::path(celestial::parked(t)?.5:t,sx,sz,s);for(int i=0;i<3;++i){native[i]=-s[i];toward[i]=float(s[i]);}};
  // Calibrate on a morning frame (at noon the x sign is ambiguous); noon is the parked vector.
  double native[3],noon_native[3];float morning[3],noon[3];native_at(.4,native,morning);native_at(.5,noon_native,noon);auto day=celestial::evaluate(.4,native,0,cal);assert(cal.done&&cal.sx==sx&&cal.sz==sz&&day.valid);
  auto frame=[&](double t,const float* sky_colour,ClockSky* out=nullptr){double nat[3];float game[3];native_at(t,nat,game);auto r=celestial::evaluate(t,nat,int64_t(t*216000),cal);assert(r.valid);
   ClockSky c{};for(int i=0;i<3;++i){c.sun[i]=r.sun[i];c.moon[i]=r.moon[i];}c.night=r.night;c.source=1;c.parked=r.parked;c.phase=r.phase;if(out)*out=c;
   auto g=build(right,up,forward,game,64,64,0,1,1,1,nullptr,sky_colour,0,0);assert(clock_ok(c)&&apply_clock(g,c,game)&&valid(g));return g;};
  // The parked native sun says noon all night; the clock says night, with the moon up, at t=.9 and t=.1.
  for(double t:{.9,.1}){auto g=frame(t,dark);assert(g.sun[3]==1&&g.moon[3]==1&&g.moon[1]>.6f&&g.sun[1]<-.6f&&(uint32_t(g.model[0])&MODEL_CLOCK)&&g.clouds[3]==0&&std::abs(g.params[0]-dark_stars)<1e-5f&&g.params[3]==g.params[0]);}
  assert(frame(.5,dark).sun[3]==0);
  // The star field turns with the sun: rotating the true sun by -theta gives s(0) at every hour.
  double s0[3];celestial::path(0,sx,sz,s0);
  for(double t:{.05,.25,.4,.6,.9}){auto g=frame(t,dark);float c0=g.celestial[0],s1=g.celestial[1],x=g.sun[0]*c0+g.sun[1]*s1,y=g.sun[1]*c0-g.sun[0]*s1;assert(std::abs(x-s0[0])<1e-5&&std::abs(y-s0[1])<1e-5&&std::abs(g.sun[2]-s0[2])<1e-5);}
  { auto g=frame(.25,dark);assert(std::abs(g.celestial[0])<1e-6&&std::abs(g.celestial[1]-sx)<1e-6); }
  // Source 2 (fallback detector, moon held): the true sun is the held moon turned half a day, so nothing jumps.
  ClockSky held{};auto g1=frame(.9,dark,&held);held.source=2;for(auto& v:held.sun)v=0;auto g2=build(right,up,forward,noon,64,64,0,1,1,1,nullptr,dark,0,0);assert(apply_clock(g2,held,noon));
  for(int i=0;i<4;++i)assert(std::abs(g1.sun[i]-g2.sun[i])<1e-6f&&std::abs(g1.celestial[i]-g2.celestial[i])<1e-6f);
  // SKY-10: no stars in twilight (sun 2 degrees down or less) or against a bright sky.
  const float bright[3]{.3f,.4f,.6f};assert(frame(.9,bright).params[0]==0&&frame(.9,bright).params[3]==0);
  for(double t=.2;t<.3;t+=.001){auto g=frame(t,dark);if(g.sun[1]>-.03f)assert(g.params[0]==0);}
  // By day the disc keeps the game's own sun, even when it differs from the clock's.
  { float game_sun[3]{.1f,.8f,-.59f};double r[3];celestial::path(.45,sx,sz,r);ClockSky c{};double m[3];celestial::path(.95,sx,sz,m);for(int i=0;i<3;++i){c.sun[i]=r[i];c.moon[i]=m[i];}c.source=1;c.parked=false;
    auto g=build(right,up,forward,game_sun,64,64,0,1,1,1);assert(apply_clock(g,c,game_sun)&&g.sun[0]==game_sun[0]&&g.sun[1]==game_sun[1]); }
 }
 // SKY-16: moonlight dims with the phase down to the .35 floor.
 { ClockSky c{};double m[3]{0,.8,-.6},su[3]{0,-.8,-.6};for(int i=0;i<3;++i){c.moon[i]=m[i];c.sun[i]=su[i];}c.source=1;c.parked=true;c.night=1;const float any[3]{0,1,0};
   c.phase=0;auto full=build(right,up,forward,any,64,64,0,1,1,1);assert(apply_clock(full,c,any)&&full.params[2]==1&&full.model[1]>3.14f);
   // The surface moonlight keeps the game's own sun timing: none under the parked noon sun, even with the clock at night.
   assert(full.sun[3]==1&&full.celestial[3]==0);const float down[3]{0,-1,0};auto low=build(right,up,forward,down,64,64,0,1,1,1);assert(low.celestial[3]==1&&low.sun[3]==1);
   c.phase=.5f;auto none=build(right,up,forward,any,64,64,0,1,1,1);assert(apply_clock(none,c,any)&&std::abs(none.params[2]-.35f)<1e-6f&&none.model[1]==0);
   ClockSky broken=c;broken.source=0;assert(!clock_ok(broken));broken=c;broken.moon[1]=0;broken.moon[2]=0;assert(!clock_ok(broken)&&!apply_clock(none,broken,any)); }
 // K49: the disc moves to the game's night light and turns opaque.
 { auto g=build(right,up,forward,midnight,64,64,0,1,1,1);const double up2[3]{0,2,0},zero[3]{};assert(apply_moon_light(g,up2,1)&&g.moon[1]==1&&g.moon[3]==1&&g.model[3]==1&&!apply_moon_light(g,zero,1)); }
 // K18: weather dims what lies behind the clouds and thickens them; extra fog dims, less fog never brightens.
 { const float sky_colour[3]{.3f,.4f,.6f};auto g=build(right,up,forward,midnight,64,64,0,1,1,1,sun_colour,sky_colour,1,1);g.model[3]=1;apply_weather(g,1,1);
   assert(std::abs(g.params[0]-.1f)<1e-6f&&std::abs(g.params[1]-.1f)<1e-6f&&std::abs(g.sun_light[3]-.1f)<1e-6f&&std::abs(g.model[3]-.1f)<1e-6f&&std::abs(g.params[2]-.3f)<1e-6f&&std::abs(g.sky_light[3]-2)<1e-6f&&g.model[2]==1&&valid(g));
   g=build(right,up,forward,midnight,64,64,0,1,1,1,sun_colour,sky_colour,1,1);apply_weather(g,0,4);assert(std::abs(g.params[0]-std::exp(-1.8f))<1e-5f&&g.sky_light[3]==1);
   g=build(right,up,forward,midnight,64,64,0,1,1,1,sun_colour,sky_colour,1,1);apply_weather(g,0,.5f);assert(g.params[0]==1&&g.params[1]==1);
   g=build(right,up,forward,midnight,64,64,0,1,1,1,sun_colour,sky_colour,1,1);apply_weather(g,NAN,NAN);assert(g.params[0]==1&&valid(g)); }
 // K19: the deck moves with the wind (sampled at camera+offset, so the offset falls), clamped, in double, wrapped.
 { const double span=double(cloud_period)*cloud_cell,east[2]{10,0},storm[2]{200,-200};double o[2]{};drift(o,east,.1);assert(std::abs(span-o[0]-1.5)<1e-6&&o[1]==0);
   double a[2]{};drift(a,storm,.1);assert(std::abs(span-a[0]-6)<1e-6&&std::abs(a[1]-6)<1e-6);
   double b[2]{};drift(b,nullptr,1);assert(std::abs(span-b[0]-14.1)<1e-6&&std::abs(span-b[1]-5.1)<1e-6);
   double c[2]{5,5};drift(c,east,-1);drift(c,east,NAN);assert(c[0]==5&&c[1]==5);
   // An hour at 60 Hz in a 20 m/s wind stays inside the period and moves a feature by v*dt every frame.
   double d[2]{};for(int i=0;i<216000;++i){double before=d[0];drift(d,storm,1./60);double moved=std::fmod(before-d[0]+span,span);assert(d[0]>=0&&d[0]<span&&std::abs(moved-1)<1e-6);}
   const double camera[3]{1e7+700,0,-3e6};auto c0=cloud_field(camera,o);auto c1=cloud_position(camera,0);assert(c0[0]>=0&&c0[0]<=cloud_period&&c0[2]>=0&&c0[2]<=cloud_period&&c0[1]==1500&&c1[1]==1500); }
 std::cout<<"PASS: sky shaders compile; constants, sampler slots, night factor, moon placement, day sky constants, clock sky, phases, star rotation, moon disc, weather and cloud drift validated\n";
}
