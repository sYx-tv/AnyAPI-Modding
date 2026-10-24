#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
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
 auto f=build(right,up,forward,projection,1920,1080,camera,wind,3600.*24*30,sun,sun_colour,sky,1,1,2,130);
 assert(valid(f)&&f.params[0]==32&&f.camera[0]==12&&f.camera[3]==2&&f.view_up[3]==1&&f.sun_light[3]==1&&f.light_down[3]==glint_roughness&&f.view_up[0]==.3f&&f.light_down[2]==.1f);
 // Positions and ripple drift wrap into the tile; a breeze of 10 m/s gives the full ripple.
 assert(f.camera[1]>=0&&f.camera[1]<ripple_tile&&f.camera[2]>=0&&f.camera[2]<ripple_tile&&std::abs(std::fmod(1e7+3.5,512.)-f.camera[1])<1e-3f);for(auto v:f.ripple)assert(v>=0&&v<=ripple_tile);
 assert(f.light_up[3]==1&&ripple_strength(nullptr)==.35f&&steps_for(0)==16&&steps_for(1)==24&&steps_for(2)==32);
 // Infinite reversed-Z far plane: fog is never full; a finite far plane is the fog distance.
 assert(f.view_down[3]==1e6f);const float finite[4]{-.1f/1999.9f,2000*.1f/1999.9f,1,0};assert(std::abs(fog_distance(finite)-2000)<1);
 // Below sea level the height is clamped just above the water rather than going negative.
 const double under[3]{0,-3,0};assert(build(right,up,forward,projection,64,64,under,wind,0,sun,sun_colour,sky,1,0,0,0).camera[0]==.05f);
 auto low=build(right,up,forward,projection,64,64,camera,nullptr,0,sun,sun_colour,sky,.5f,0,0,0);assert(valid(low)&&low.params[0]==16&&low.light_up[3]==.35f&&low.sun_light[3]==0);
 auto bad=f;bad.view_up[3]=1.5f;assert(!valid(bad));bad=f;bad.sun_light[3]=3;assert(!valid(bad));bad=f;bad.params[0]=4;assert(!valid(bad));bad=f;bad.camera[0]=0;assert(!valid(bad));
 bad=f;bad.camera[1]=600;assert(!valid(bad));bad=f;bad.ripple[2]=-1;assert(!valid(bad));bad=f;bad.metrics[2]=4;assert(!valid(bad));bad=f;bad.projection={0,0,0,0};assert(!valid(bad));
 bad=f;bad.light_down[3]=0;assert(!valid(bad));bad=f;bad.view_down[1]=-1;assert(!valid(bad));bad=f;bad.sun[3]=NAN;assert(!valid(bad));bad=f;bad.view_down[3]=10;assert(!valid(bad));bad=f;bad.sun_light[0]=20000;assert(!valid(bad));
 std::cout<<"PASS: water shaders compile; constants, sampler and texture slots, frame bounds, ripple wrap and fog distance validated\n";
}
