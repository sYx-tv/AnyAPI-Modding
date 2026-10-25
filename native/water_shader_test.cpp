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
#include "effects/water_frame.h"
using Microsoft::WRL::ComPtr;
// Compiles the ocean reflection shaders, checks constant size, sampler and
// texture slots, validates the frame constants, and optionally writes the
// bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace water_bytecode {\n";}
 for(auto entry:{"Vertex","March","Composite"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
  if(FAILED(hr)){if(error)std::cerr<<(const char*)error->GetBufferPointer();return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  bool reads_reflection=false;
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));
   if(b.Type==D3D_SIT_TEXTURE){assert(b.BindPoint<4);static const char* names[]={"colour","depth","ocean","reflection"};assert(!strcmp(b.Name,names[b.BindPoint]));reads_reflection=reads_reflection||b.BindPoint==3;}}
  // The march renders into the half resolution texture, so it must never read it.
  if(!strcmp(entry,"March"))assert(!reads_reflection);if(!strcmp(entry,"Composite"))assert(reads_reflection);
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_water_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 using namespace scene_water_gpu;
 const float right[4]{1,0,0,.8f},up[4]{0,.97f,.24f,.45f},forward[4]{0,-.24f,.97f,0},projection[4]{0,.1f,1,0},sun[3]{0,.6f,.8f},sun_colour[3]{3,2.7f,2.4f},sky[4][3]{{.3f,.45f,.8f},{.6f,.7f,.8f},{.2f,.25f,.3f},{.1f,.1f,.1f}};
 const double camera[3]{1e7+3.5,12,-1e7-7},wind[2]{6,-8};
 double phase[4]{};advance_ripples(phase,wind,3600.*24*30);
 auto f=build(right,up,forward,projection,1920,1080,camera,wind,phase,sun,sun_colour,sky,1,1,2,130);
 assert(valid(f)&&f.params[0]==32&&f.camera[0]==12&&f.camera[3]==2&&f.view_up[3]==1&&f.sun_light[3]==1&&f.light_down[3]==glint_alpha(wind)&&f.view_up[0]==.3f&&f.light_down[2]==.1f&&f.params[2]==0&&f.params[3]==0);
 // Positions and ripple drift wrap into the tile; a breeze of 10 m/s gives the full ripple.
 assert(f.camera[1]>=0&&f.camera[1]<ripple_tile&&f.camera[2]>=0&&f.camera[2]<ripple_tile&&std::abs(std::fmod(1e7+3.5,512.)-f.camera[1])<1e-3f);for(auto v:f.ripple)assert(v>=0&&v<=ripple_tile);
 assert(f.light_up[3]==1&&ripple_strength(nullptr)==.35f&&steps_for(0)==16&&steps_for(1)==24&&steps_for(2)==32);
 // Infinite reversed-Z far plane: fog is never full; a finite far plane is the fog distance.
 assert(f.view_down[3]==1e6f);const float finite[4]{-.1f/1999.9f,2000*.1f/1999.9f,1,0};assert(std::abs(fog_distance(finite)-2000)<1);
 // WATER-1: rays reach the fog distance, capped at 100 km.
 assert(f.forward[3]==longest_ray&&longest_ray==1e5f&&std::abs(build(right,up,forward,finite,64,64,camera,wind,phase,sun,sun_colour,sky,1,1,1,0).forward[3]-2000)<1);
 // Below sea level the height is clamped just above the water rather than going negative.
 const double under[3]{0,-3,0};assert(build(right,up,forward,projection,64,64,under,wind,nullptr,sun,sun_colour,sky,1,0,0,0).camera[0]==.05f);
 auto low=build(right,up,forward,projection,64,64,camera,nullptr,nullptr,sun,sun_colour,sky,.5f,0,0,0);assert(valid(low)&&low.params[0]==16&&low.light_up[3]==.35f&&low.sun_light[3]==0);
 auto bad=f;bad.view_up[3]=1.5f;assert(!valid(bad));bad=f;bad.sun_light[3]=3;assert(!valid(bad));bad=f;bad.params[0]=4;assert(!valid(bad));bad=f;bad.camera[0]=0;assert(!valid(bad));
 bad=f;bad.camera[1]=600;assert(!valid(bad));bad=f;bad.ripple[2]=-1;assert(!valid(bad));bad=f;bad.metrics[2]=4;assert(!valid(bad));bad=f;bad.projection={0,0,0,0};assert(!valid(bad));
 bad=f;bad.light_down[3]=0;assert(!valid(bad));bad=f;bad.view_down[1]=-1;assert(!valid(bad));bad=f;bad.sun[3]=NAN;assert(!valid(bad));bad=f;bad.view_down[3]=10;assert(!valid(bad));bad=f;bad.sun_light[0]=20000;assert(!valid(bad));
 bad=f;bad.params[2]=.06f;assert(!valid(bad));bad=f;bad.forward[3]=2e5f;assert(!valid(bad));
 // K19: the ripple phase integrates the wind. An hour at 60 Hz with a wind that wanders up to 10 m/s moves each
 // offset by at most |v| dt per frame (no jumps), and stays in the tile.
 { double ph[4]{};for(int i=0;i<216000;++i){double t=i/60.,w[2]{7*std::sin(t*.01),7*std::cos(t*.013)},before[4];for(int k=0;k<4;++k)before[k]=ph[k];advance_ripples(ph,w,1./60);
    auto o=ripple_offsets(ph);for(int k=0;k<4;++k){double d=std::abs(ph[k]-before[k]);d=std::min(d,double(ripple_tile)-d);assert(d<=.06&&ph[k]>=0&&ph[k]<ripple_tile&&o[k]>=0&&o[k]<=ripple_tile);}}
   // After 30 days of uptime a 0.1 m/s change of wind moves the ripples by under 0.05 m in the next frame (V2: 35 km).
   double a[4]{},b[4];const double w0[2]{5,0},w1[2]{5.1,0};advance_ripples(a,w0,3600.*24*30);for(int k=0;k<4;++k)b[k]=a[k];advance_ripples(a,w0,.016);advance_ripples(b,w1,.016);
   for(int k=0;k<4;++k)assert(std::abs(a[k]-b[k])<.05);
   double c[4]{1,2,3,4};advance_ripples(c,w0,-1);advance_ripples(c,w0,NAN);assert(c[0]==1&&c[3]==4); }
 // K48: GGX alpha widens with wind and rain; calm water keeps the old roughness plus the calm slope variance.
 { const double calm[2]{},breeze[2]{10,0},gale[2]{30,0};assert(std::abs(glint_alpha(calm)-.1140f)<1e-3f&&std::abs(glint_alpha(breeze)-.2534f)<1e-3f&&glint_alpha(breeze)>glint_alpha(calm));
   assert(std::abs(glint_alpha(calm,1)-glint_alpha(calm)-.08f)<1e-5f&&glint_alpha(gale,1)==.4f&&glint_alpha(nullptr)==glint_alpha(calm)&&ripple_strength(calm,1)==.65f&&ripple_strength(breeze,1)==1); }
 // WATER-15: this frame's TAA jitter (uv), clamped; none without TAA.
 { const float jitter[2]{.2f,-.0004f};auto j=build(right,up,forward,projection,64,64,camera,wind,phase,sun,sun_colour,sky,1,1,1,5,jitter,.5f);
   assert(valid(j)&&j.params[2]==.05f&&j.params[3]==-.0004f&&j.camera[3]==5&&j.light_down[3]==glint_alpha(wind,.5f)&&j.light_up[3]==ripple_strength(wind,.5f)); }
 std::cout<<"PASS: water shaders compile; constants, sampler and texture slots, frame bounds, ripple wrap, fog distance, ray reach, ripple phase, glint roughness and jitter validated\n";
}
