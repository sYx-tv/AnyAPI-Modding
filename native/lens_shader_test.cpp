#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include "effects/lens_frame.h"
using Microsoft::WRL::ComPtr;
// Compiles the lens shaders, checks constant size and sampler
// slots, and optionally writes the bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace lens_bytecode {\n";}
 for(auto entry:{"Vertex","Lens","Focus"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
  if(FAILED(hr)){if(error)std::cerr<<(const char*)error->GetBufferPointer();return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));if(b.Type==D3D_SIT_TEXTURE)assert(b.BindPoint<6);}
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_lens_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 scene_lens_gpu::Frame f;f.metrics={1.f/64,1.f/64,64,64};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,.1f,1,0};f.camera_move={0,0,0,1};f.params={1,1,12,0};assert(scene_lens_gpu::valid(f));
 auto bad=f;bad.params[0]=5;assert(!scene_lens_gpu::valid(bad));bad=f;bad.params[2]=0;assert(!scene_lens_gpu::valid(bad));bad=f;bad.projection={0,0,0,0};assert(!scene_lens_gpu::valid(bad));bad=f;bad.previous_up[3]=NAN;assert(!scene_lens_gpu::valid(bad));bad=f;bad.camera_move[0]=5000;assert(!scene_lens_gpu::valid(bad));
 std::cout<<"PASS: lens shaders compile; constants and sampler slots validated\n";
}
