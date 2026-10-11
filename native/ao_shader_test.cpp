#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include "effects/ao_frame.h"
using Microsoft::WRL::ComPtr;
// Compiles the ambient occlusion shaders, checks constant size and sampler
// slots, and optionally writes the bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace ao_bytecode {\n";}
 for(auto entry:{"Vertex","Occlusion","Resolve"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
  if(FAILED(hr)){if(error)std::cerr<<(const char*)error->GetBufferPointer();return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));if(b.Type==D3D_SIT_TEXTURE)assert(b.BindPoint<4);}
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_ao_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 scene_ao_gpu::Frame f;f.metrics={1.f/64,1.f/64,64,64};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,.1f,1,0};f.params={1.5f,1,0,0};assert(scene_ao_gpu::valid(f));
 auto bad=f;bad.params[0]=0;assert(!scene_ao_gpu::valid(bad));bad=f;bad.projection={0,0,0,0};assert(!scene_ao_gpu::valid(bad));bad=f;bad.right[3]=NAN;assert(!scene_ao_gpu::valid(bad));bad=f;bad.metrics[3]=8;assert(!scene_ao_gpu::valid(bad));
 std::cout<<"PASS: ambient occlusion shaders compile; constants and sampler slots validated\n";
}
