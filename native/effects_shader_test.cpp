#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include "effects/effects_frame.h"
using Microsoft::WRL::ComPtr;
// Compiles the scene effects shaders, checks constant size and sampler slots,
// and optionally writes the bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace effects_bytecode {\n";}
 for(auto entry:{"Vertex","Luma","Adapt","Down","Up","Composite"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)){if(error)std::cerr.write((char*)error->GetBufferPointer(),error->GetBufferSize());return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));if(b.Type==D3D_SIT_TEXTURE)assert(b.BindPoint<4);}
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_effects_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 scene_effects_gpu::Frame f;f.metrics={1.f/64,1.f/64,64,64};f.grading={1,3,1,0};f.exposure={1,.1f,.01f,1.5f};assert(scene_effects_gpu::valid(f));
 auto bad=f;bad.grading[0]=3;assert(!scene_effects_gpu::valid(bad));bad=f;bad.extras[0]=NAN;assert(!scene_effects_gpu::valid(bad));bad=f;bad.metrics[2]=8;assert(!scene_effects_gpu::valid(bad));
 // Sun projection: straight ahead is the screen centre; behind the camera has no glare.
 scene_effects_gpu::Native n;float r[4]{1,0,0,.7f},u[4]{0,1,0,.4f},fw[4]{0,0,1,0};memcpy(n.right,r,sizeof(r));memcpy(n.up,u,sizeof(u));memcpy(n.forward,fw,sizeof(fw));n.sun[0]=0;n.sun[1]=.3f;n.sun[2]=.954f;
 auto s=scene_effects_gpu::sun_screen(n);assert(s[2]==1&&std::abs(s[0]-.5f)<1e-5f&&s[1]<.5f);n.sun[2]=-.954f;assert(scene_effects_gpu::sun_screen(n)[2]==0);
 std::cout<<"PASS: scene effects shaders compile; constants, sampler slots and sun projection validated\n";
}
