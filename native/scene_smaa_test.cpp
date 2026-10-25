#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include "third_party/smaa/Textures/AreaTex.h"
#include "third_party/smaa/Textures/SearchTex.h"
#include "aa/scene_jitter.h"
#include "aa/scene_smaa_bindings.h"
using Microsoft::WRL::ComPtr;
// CPU contracts of the AA runtime that need no GPU (run before the shaders compile, ctest mode only).
static void cpu_contracts(){
 // K03: bit i of the mask refuses byte i of scene+0x668; >1 always refuses; 0xff is the 0.40.1 rule.
 unsigned char none[4]{},underground[4]{1,0,0,0},night[4]{0,0,1,0},bad[4]{0,2,0,0};const uint32_t special=4|8;
 assert(!scene_jitter::view_refused(none,0xff)&&scene_jitter::view_refused(underground,0xff)&&!scene_jitter::view_refused(underground,special)&&scene_jitter::view_refused(night,special)&&scene_jitter::view_refused(bad,0));
 // Jitter and K32: a perspective camera (VP = P, clip w = -z = forward), jittered; the copy read after AA is un-jittered exactly once.
 static unsigned char scene[0x2bf8]{};double p[16]{1.5,0,0,0, 0,2,0,0, 0,0,0,-1, 0,0,.1,0},forward[3]{0,0,-1};
 memcpy(scene+0x20+0x1a0,p,sizeof(p));memcpy(scene+0x20+0x220,p,sizeof(p));memcpy(scene+0x20+0x78,forward,sizeof(forward));
 scene_jitter::State s;scene_jitter::current=nullptr;scene[0x66a]=1;assert(!scene_jitter::apply(scene,64,64,0,s,special)&&!s.active);scene[0x66a]=0;scene[0x668]=1;
 assert(scene_jitter::apply(scene,64,64,0,s,special)&&s.active);scene[0x668]=0;double vp[16];memcpy(vp,scene+0x20+0x220,sizeof(vp));assert(memcmp(vp,p,sizeof(p)));
 assert(!scene_jitter::unjitter_view_projection(vp));scene_jitter::current=&s;assert(scene_jitter::unjitter_view_projection(vp));for(int i=0;i<16;++i)assert(std::abs(vp[i]-p[i])<1e-12);
 double rebuilt[16];memcpy(rebuilt,p,sizeof(p));rebuilt[0]=1.4;assert(!scene_jitter::unjitter_view_projection(rebuilt)&&rebuilt[0]==1.4);
 // A stale copy of last frame's jitter is detected; jitter never accumulates.
 assert(scene_jitter::apply(scene,64,64,1,s,special));double x,y;scene_jitter::offset(1,x,y);memcpy(vp,scene+0x20+0x220,sizeof(vp));assert(std::abs(vp[8]-(p[8]+x*2/64*p[11]))<1e-12);
 s.active=false;assert(!scene_jitter::unjitter_view_projection(vp));scene_jitter::current=nullptr;
 // AA-1: overlay counts at +0xc of 7 vectors, stride 0x28; hysteresis of 30 frames.
 static_assert(scene_jitter::overlay_block==0x100);unsigned char block[scene_jitter::overlay_block]{};int32_t counts[scene_jitter::overlay_renderers];
 assert(scene_jitter::overlay_counts(block,counts));for(auto n:counts)assert(!n);int32_t five=5,negative=-1;memcpy(block+6*0x28+0xc,&five,4);assert(scene_jitter::overlay_counts(block,counts)&&counts[6]==5);
 memcpy(block+0xc,&negative,4);assert(!scene_jitter::overlay_counts(block,counts));
 scene_jitter::OverlayGate gate;assert(!gate.blocks());gate.observe(true);for(int i=0;i<29;++i){assert(gate.blocks());gate.observe(false);}assert(gate.blocks());gate.observe(false);assert(!gate.blocks()&&gate.seen==1);
 // K23: arguments below the high-water mark reset on a new root signature; root constants keep a 64-bit written mask.
 scene_smaa::Bindings b;b.reset();auto root=(ID3D12RootSignature*)uintptr_t(0x10);b.signature(root);uint32_t v=7;b.constants(2,1,&v,5);assert(b.used==3&&b.arguments[2].kind==5&&b.arguments[2].written==1ull<<5&&b.arguments[2].constant_count==6);
 uint32_t many[64]{};b.constants(2,64,many,0);assert(b.arguments[2].written==~0ull&&b.arguments[2].constant_count==64);b.constants(2,1,&v,64);assert(!b.valid);b.valid=true;
 b.descriptor(4,1,0x1234);assert(b.used==5&&b.arguments[4].kind==1);b.descriptor(2,1,0x99);b.constants(2,1,&v,1);assert(b.arguments[2].kind==5&&b.arguments[2].written==2);
 b.signature(root);assert(b.used==5);b.signature((ID3D12RootSignature*)uintptr_t(0x20));assert(b.used==0);for(auto& a:b.arguments)assert(!a.kind);b.descriptor(40,1,1);assert(!b.valid);
 // K23: 4 tracked lists, the oldest replaced by a fifth, last hit cached, clear() forgets all.
 scene_smaa::ListStates states;ID3D12GraphicsCommandList* l[6];for(int i=0;i<6;++i)l[i]=(ID3D12GraphicsCommandList*)uintptr_t(0x100*(i+1));
 for(int i=0;i<4;++i){assert(states.find(l[i])==states.end());states.insert(l[i])->second.pipeline=(ID3D12PipelineState*)uintptr_t(i+1);}
 assert(states.size()==4&&states.find(l[2])->second.pipeline==(ID3D12PipelineState*)uintptr_t(3));states.insert(l[4]);assert(!states.find(l[0])&&states.find(l[1])&&states.find(l[4])&&states.peak==4);
 assert(!states.find(l[4])->second.pipeline);states.clear();assert(!states.size()&&!states.find(l[4])&&states.end()==nullptr);
 std::cout<<"PASS: AA CPU contracts (view mask, jitter and un-jitter, overlay counts and hysteresis, bindings masks, list cache)\n";
}
// Texture slots (scene_smaa_gpu.h heap tables) and the Frame block (44 root constants).
static void bindings_contract(const char* entry,ID3D11ShaderReflection* reflected){D3D11_SHADER_DESC desc{};reflected->GetDesc(&desc);
 const char* textures[]={"source_image","area_or_weights","search_image","ocean_image","albedo_image"};
 for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};reflected->GetResourceBindingDesc(i,&b);
  if(b.Type==D3D_SIT_SAMPLER){assert(b.BindCount==1);assert(b.BindPoint==(!strcmp(b.Name,"PointSampler")?1:0));}
  if(b.Type==D3D_SIT_TEXTURE){bool known=false;for(UINT t=0;t<5;++t)if(!strcmp(b.Name,textures[t])){assert(b.BindPoint==t&&b.BindCount==1);known=true;}assert(known);}
  if(b.Type==D3D_SIT_CBUFFER){assert(b.BindPoint==0);D3D11_SHADER_BUFFER_DESC cb{};reflected->GetConstantBufferByName(b.Name)->GetDesc(&cb);assert(cb.Size==44*4);}}
 if(!strcmp(entry,"TemporalMRT"))assert(desc.OutputParameters==2);
}
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);if(argc==2)cpu_contracts();unsigned kernels{};std::ofstream output;if(argc==3){output.open(argv[2],std::ios::binary);assert(output);output<<"#pragma once\nnamespace scene_smaa_bytecode {\n";}unsigned quality=0;
 for(auto preset:{"SMAA_PRESET_LOW","SMAA_PRESET_MEDIUM","SMAA_PRESET_HIGH","SMAA_PRESET_ULTRA"}){
  D3D_SHADER_MACRO macros[]={{preset,"1"},{nullptr,nullptr}};
  for(auto entry:{"Vertex","Edges","Weights","Resolve","Temporal","TemporalMRT","Output","UnditherProbe"}){
   ComPtr<ID3DBlob> code,errors;auto hr=D3DCompileFromFile(argv[1],macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
   if(FAILED(hr)){if(errors)std::cerr.write((char*)errors->GetBufferPointer(),errors->GetBufferSize());return 1;}
   assert(code&&code->GetBufferSize()>32);++kernels;
   ComPtr<ID3D11ShaderReflection> reflected;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflected))));bindings_contract(entry,reflected.Get());
   if(output){output<<"inline constexpr unsigned char "<<entry<<quality<<"[]={";auto data=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)output<<unsigned(data[i])<<',';output<<"};\n";}
  }
  ++quality;
 }
 if(output)output<<"}\n";
 assert(sizeof(areaTexBytes)==AREATEX_SIZE&&sizeof(searchTexBytes)==SEARCHTEX_SIZE);
 std::cout<<"PASS: official SMAA 1x vertex/edge/weight/resolve plus temporal (single and MRT), output and undither-probe kernels compile for Low, Medium, High and Ultra with the t0-t4/s0-s1/b0 contract; complete lookup tables. Kernels="<<kernels<<"\n";
}
