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
namespace platform {static thread_local int current_plugin=0;}
static bool owner_alive=true;static bool anyapi_owner_active(int owner){return owner_alive&&owner==0;}
#include "anyapi_scene_effects_state.h"
using Microsoft::WRL::ComPtr;
// Compiles the scene effects shaders, checks constant size and sampler slots,
// and optionally writes the bytecode header used by the runtime.
int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);std::ofstream out;if(argc==3){out.open(argv[2],std::ios::binary);assert(out);out<<"#pragma once\nnamespace effects_bytecode {\n";}
 for(auto entry:{"Vertex","Luma","Adapt","Down","Up","Composite"}){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)){if(error)std::cerr.write((char*)error->GetBufferPointer(),error->GetBufferSize());return 1;}
  ComPtr<ID3D11ShaderReflection> r;assert(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&r))));D3D11_SHADER_DESC sd{};r->GetDesc(&sd);
  for(UINT i=0;i<sd.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};r->GetResourceBindingDesc(i,&b);if(b.Type==D3D_SIT_SAMPLER)assert(b.BindPoint==(!strcmp(b.Name,"point_sampler")?1u:0u));if(b.Type==D3D_SIT_TEXTURE)assert(b.BindPoint<6);}
  if(entry[0]!='V'){D3D11_SHADER_BUFFER_DESC b{};assert(SUCCEEDED(r->GetConstantBufferByName("Frame")->GetDesc(&b))&&b.Size==sizeof(scene_effects_gpu::Frame));}
  if(out){out<<"inline constexpr unsigned char "<<entry<<"[]={";auto bytes=(const unsigned char*)code->GetBufferPointer();for(size_t i=0;i<code->GetBufferSize();++i)out<<unsigned(bytes[i])<<',';out<<"};\n";}}
 if(out)out<<"}\n";
 scene_effects_gpu::Frame f;f.metrics={1.f/64,1.f/64,64,64};f.grading={1,3,1,0};f.exposure={1,.1f,.01f,1.5f};assert(scene_effects_gpu::valid(f));
 auto bad=f;bad.grading[0]=4;assert(!scene_effects_gpu::valid(bad));bad=f;bad.grading[1]=7;assert(!scene_effects_gpu::valid(bad));bad=f;bad.grading[0]=3;bad.grading[1]=6;assert(scene_effects_gpu::valid(bad));bad=f;bad.extras[0]=NAN;assert(!scene_effects_gpu::valid(bad));bad=f;bad.metrics[2]=8;assert(!scene_effects_gpu::valid(bad));
 // Sun projection: straight ahead is the screen centre; behind the camera has no glare.
 scene_effects_gpu::Native n;float r[4]{1,0,0,.7f},u[4]{0,1,0,.4f},fw[4]{0,0,1,0};memcpy(n.right,r,sizeof(r));memcpy(n.up,u,sizeof(u));memcpy(n.forward,fw,sizeof(fw));n.sun[0]=0;n.sun[1]=.3f;n.sun[2]=.954f;
 auto s=scene_effects_gpu::sun_screen(n);assert(s[2]==1&&std::abs(s[0]-.5f)<1e-5f&&s[1]<.5f);n.sun[2]=-.954f;assert(scene_effects_gpu::sun_screen(n)[2]==0);
 // Version 3 tuning: the defaults are V2's output; bounds.
 {scene_effects_gpu::Frame d;d.metrics=f.metrics;assert(valid(d)&&d.tuning[0]==1&&d.tuning[1]==0&&d.tuning[2]==0&&d.tuning[3]==.35f);
  for(int c=0;c<7;++c){auto t=f;if(c==0)t.tuning[0]=.4f;if(c==1)t.tuning[0]=1.01f;if(c==2)t.tuning[1]=.5f;if(c==3)t.tuning[2]=2;if(c==4)t.tuning[3]=-.1f;if(c==5)t.tuning[3]=1.1f;if(c==6)t.tuning[0]=NAN;assert(!scene_effects_gpu::valid(t));}
  auto t=f;t.tuning={.5f,1,1,1};assert(scene_effects_gpu::valid(t));}
 // K03 capture_view on a fake scene and renderer in this process (scene_frame::reader reads memory directly).
 {static unsigned char scene[0x2c00]{},renderer[0x400]{},colour_object[0x60]{},depth_object[0x60]{};
  auto put=[](unsigned char* b,size_t o,auto v){memcpy(b+o,&v,sizeof(v));};
  auto target=[&](unsigned char* o,uint32_t format){put(o,0x10,uintptr_t(0x1000));put(o,0x28,uint32_t(4));put(o,0x34,uint32_t(64));put(o,0x38,uint32_t(36));put(o,0x50,format);put(o,0x54,uint32_t(4));};
  target(colour_object,10);target(depth_object,41);put(renderer,0x3c0,uintptr_t(colour_object));put(renderer,0x218,uintptr_t(depth_object));
  auto vec=[&](size_t o,double x,double y,double z){double v[3]{x,y,z};memcpy(scene+o,v,sizeof(v));};
  vec(0x20+0xa8,1,0,0);vec(0x20+0x90,0,1,0);vec(0x20+0x78,0,0,1);vec(0xa78,0,-1,0);vec(0xaf0,1,1,1);double m[16]{};m[0]=1.2;m[5]=1.6;memcpy(scene+0x20+0x1a0,m,sizeof(m));
  scene_effects_gpu::Native n;uint32_t view=0;
  auto flags=[&](unsigned char a,unsigned char b,unsigned char c,unsigned char d){scene[0x668]=a;scene[0x669]=b;scene[0x66a]=c;scene[0x66b]=d;};
  flags(0,0,0,0);assert(scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)&&view==0&&n.failure==0&&n.color.width==64&&n.sun[1]==1);
  flags(0,1,0,0);assert(scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)&&view==scene_frame::VIEW_UNDERWATER&&n.failure==0);
  assert(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader)&&n.failure==1);   // legacy contract unchanged
  flags(1,0,0,1);assert(scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)&&view==(scene_frame::VIEW_UNDERGROUND|scene_frame::VIEW_THERMAL));
  flags(0,0,2,0);assert(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)&&view==scene_frame::VIEW_UNREADABLE&&n.failure==1);
  flags(0,0,0,0);put(depth_object,0x50,uint32_t(40));assert(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)&&view==0&&n.failure==2);
  assert(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(0),n,scene_frame::reader,view)&&view==scene_frame::VIEW_UNREADABLE);}
 // anyapi.scene_effects v3: setter bounds, V1/V2 reset the extras and the sky policy, ownership, status_v3, registry.
 {using namespace scene_effects;scene_effects::available=true;auto& b=scene_frame::board;scene_frame::SkyPolicy policy{};
  AnySceneEffectsParametersV3 p;p.water.effects.enabled=1;assert(set_v3(&p));auto x=copy_extras();
  assert(x.shading_revision==1&&x.tone_revision==0&&x.bloom_falloff==1&&x.dither==1&&x.flags==0);
  assert(b.sky_policy.load(policy)&&policy.sky_model==1&&policy.cloud_model==1&&policy.weather==0&&policy.moon_disc_follows_light==0);
  for(int c=0;c<13;++c){auto q=p;switch(c){case 0:q.version=2;break;case 1:q.struct_size-=4;break;case 2:q.sky_model=2;break;case 3:q.tone_revision=2;break;
   case 4:q.bloom_falloff=.49f;break;case 5:q.bloom_falloff=NAN;break;case 6:q.dither=2;break;case 7:q.flags=2;break;case 8:q.reserved[3]=1;break;
   case 9:q.water.version=1;break;case 10:q.water.effects.version=2;break;case 11:q.water.sun_glint=3;break;case 12:q.shading_revision=7;break;}
   assert(!set_v3(&q));}
  assert(!set_v3(nullptr));
  p.tone_revision=1;p.bloom_falloff=.7f;p.dither=0;p.flags=ANY_FX_KEEP_NATIVE_SSAO;p.weather=1;assert(set_v3(&p));x=copy_extras();
  assert(x.tone_revision==1&&x.bloom_falloff==.7f&&x.dither==0&&x.flags==ANY_FX_KEEP_NATIVE_SSAO&&b.sky_policy.load(policy)&&policy.weather==1);
  platform::current_plugin=1;assert(!set_v3(&p));platform::current_plugin=0;           // owner 0 is active and enabled
  AnySceneEffectsParametersV2 v2;v2.effects.enabled=1;assert(set_v2(&v2));x=copy_extras();
  assert(x.shading_revision==0&&x.tone_revision==0&&x.bloom_falloff==1&&x.dither==1&&x.flags==0&&b.sky_policy.load(policy)&&!policy.sky_model&&!policy.cloud_model&&!policy.weather);
  assert(set_v3(&p));AnySceneEffectsParametersV1 v1;v1.enabled=1;assert(set(&v1));x=copy_extras();assert(x.dither==1&&x.flags==0&&b.sky_policy.load(policy)&&!policy.weather);
  owner_alive=false;assert(!copy_owned().enabled&&!copy().enabled);owner_alive=true;assert(copy_owned().enabled);
  scene_effects::available=false;assert(copy_owned().enabled&&!copy().enabled);scene_effects::available=true;  // SC-16: photo/distance need only the owner
  AnySceneEffectsStatusV3 s;s.version=2;assert(!status_v3(&s));s.version=3;
  b.frame=100;b.pass_error[ANY_PASS_FINISH]=205;b.pass_error[ANY_PASS_AO]=-1;assert(status_v3(&s)&&s.pass_error[ANY_PASS_FINISH]==205&&s.pass_error[ANY_PASS_AO]==-1&&!s.clock_valid&&!s.sky_model_active&&s.base.version==2&&s.base.effects.version==1);
  scene_frame::Clock clock{};clock.frame=99;clock.source=1;b.clock.store(clock);assert(set_v3(&p)&&status_v3(&s)&&s.clock_valid&&!s.sky_model_active);   // sky_ready false
  clock.frame=97;b.clock.store(clock);assert(status_v3(&s)&&!s.clock_valid);
  assert(scene_frame::find_service("anyapi.scene_effects",3)==&api_v3);b.pass_error[ANY_PASS_FINISH]=0;b.pass_error[ANY_PASS_AO]=0;b.frame=0;}
 std::cout<<"PASS: scene effects shaders compile; constants, sampler slots, sun projection, v3 tuning bounds, view flags and the v3 API validated\n";
}
