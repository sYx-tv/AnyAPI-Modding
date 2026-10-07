#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <iostream>
#include <fstream>
#include "third_party/smaa/Textures/AreaTex.h"
#include "third_party/smaa/Textures/SearchTex.h"
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);unsigned kernels{};std::ofstream output;if(argc==3){output.open(argv[2],std::ios::binary);assert(output);output<<"#pragma once\nnamespace scene_smaa_bytecode {\n";}unsigned quality=0;
 for(auto preset:{"SMAA_PRESET_LOW","SMAA_PRESET_MEDIUM","SMAA_PRESET_HIGH","SMAA_PRESET_ULTRA"}){
  D3D_SHADER_MACRO macros[]={{preset,"1"},{nullptr,nullptr}};
  for(auto entry:{"Vertex","Edges","Weights","Resolve"}){
   ComPtr<ID3DBlob> code,errors;auto hr=D3DCompileFromFile(argv[1],macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
   if(FAILED(hr)){if(errors)std::cerr.write((char*)errors->GetBufferPointer(),errors->GetBufferSize());return 1;}
   assert(code&&code->GetBufferSize()>32);++kernels;
   ComPtr<ID3D11ShaderReflection> reflected;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflected))));D3D11_SHADER_DESC desc{};reflected->GetDesc(&desc);for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};reflected->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER){assert(b.BindCount==1);assert(b.BindPoint==(!strcmp(b.Name,"PointSampler")?1:0));}}

   if(output){output<<"inline constexpr unsigned char "<<entry<<quality<<"[]={";auto data=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)output<<unsigned(data[i])<<',';output<<"};\n";}
  }
  ++quality;
 }
 if(output)output<<"}\n";
 assert(sizeof(areaTexBytes)==AREATEX_SIZE&&sizeof(searchTexBytes)==SEARCHTEX_SIZE);
 std::cout<<"PASS: official SMAA 1x vertex/edge/weight/resolve kernels compile for Low, Medium, High and Ultra; complete lookup tables. Kernels="<<kernels<<"\n";
}
