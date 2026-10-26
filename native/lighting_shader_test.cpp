#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include "lighting/volumetric_frame.h"
using Microsoft::WRL::ComPtr;
// AnyAPI 0.40.2: IntegrateConstants is also built with LAMP_SLOTS 0/2/4 (the 8 build keeps its old name) and
// LAMP_BRANCH 1, through D3D_SHADER_MACRO. Every build stays FXC ps_5_0.
struct Build{const char* name;const char* entry;const char* slots;const char* branch;};
static const Build builds[]={{"Vertex","Vertex"},{"Matrices","Matrices"},{"Probe","Probe"},{"Integrate","Integrate"},{"IntegrateConstants","IntegrateConstants"},{"IntegrateConstants0","IntegrateConstants","0"},{"IntegrateConstants2","IntegrateConstants","2"},{"IntegrateConstants4","IntegrateConstants","4"},{"IntegrateConstantsBranch","IntegrateConstants",nullptr,"1"},{"Accumulate","Accumulate"},{"Composite","Composite"}};
static bool bound(ID3D11ShaderReflection* r,const char* name,UINT& slot){D3D11_SHADER_INPUT_BIND_DESC b{};if(FAILED(r->GetResourceBindingDescByName(name,&b)))return false;slot=b.BindPoint;return true;}
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace volumetric_bytecode {\n";}
 UINT flow8{},flow_branch{};
 for(auto& build:builds){auto entry=build.entry;D3D_SHADER_MACRO macros[3]{};UINT m=0;if(build.slots)macros[m++]={"LAMP_SLOTS",build.slots};if(build.branch)macros[m++]={"LAMP_BRANCH",build.branch};
  ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)){std::cerr<<build.name<<": ";if(error)std::cerr.write((char*)error->GetBufferPointer(),error->GetBufferSize());return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1:0));}if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==offsetof(volumetric::Frame,additional_shadows));}
  // Lamp data: 8 rows of 4 float4 in b1 for every constants build except LAMP_SLOTS 0, which reads none.
  if(!strncmp(build.name,"IntegrateConstants",18)){D3D11_SHADER_BUFFER_DESC b{};bool lamps=SUCCEEDED(r->GetConstantBufferByName("Lamps")->GetDesc(&b));assert(strcmp(build.name,"IntegrateConstants0")?lamps&&b.Size==512:!lamps);
   if(!strcmp(build.name,"IntegrateConstants"))flow8=sd.DynamicFlowControlCount;else if(build.branch)flow_branch=sd.DynamicFlowControlCount;else assert(sd.DynamicFlowControlCount<=flow8);}
  // K07: the composite blends onto the HDR target, so it must not read scene_colour (t0). The ocean mask is t18.
  UINT slot{};if(!strcmp(entry,"Composite"))assert(!bound(r.Get(),"scene_colour",slot));if(!strcmp(entry,"Integrate")||!strcmp(build.name,"IntegrateConstants"))assert(bound(r.Get(),"ocean",slot)&&slot==18);
  if(out){out<<"inline constexpr unsigned char "<<build.name<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 assert(flow_branch>=flow8);
 if(out)out<<"}\n";volumetric::Frame frame;assert(volumetric::valid(frame));frame.medium[0]=NAN;assert(!volumetric::valid(frame));
 // Row 21 extras (0.40.2): lamp scale 0 or 1..4, sea mode 0..2, sea level, phase cap 0/1, flash glow 0..100.
 frame=volumetric::Frame{};auto& t=frame.temporal[1];t[6]=2.5f;t[8]=2;t[9]=0;t[10]=1;t[12]=t[13]=t[14]=3;assert(volumetric::valid(frame));
 for(auto [at,bad]:std::initializer_list<std::pair<int,float>>{{6,.5f},{6,4.5f},{7,1},{8,3},{8,1.5f},{9,-200},{10,.5f},{11,1},{12,-1},{12,101},{15,1}}){auto copy=frame;copy.temporal[1][at]=bad;assert(!volumetric::valid(copy));}
 std::cout<<"PASS: HDR volumetric integration and bilateral composite compile; exact constants and sampler slots validated; LAMP_SLOTS 0/2/4/8 and branch builds; blend composite without t0; row 21 extras\n";
}
