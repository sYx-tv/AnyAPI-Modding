#include "runtime_call_cell.h"
namespace scene_controls {
static anyapi_runtime::CallCellHook renderer_cell,scene_cell,pre_hud_cell,lighting_cell,ssao_cell;
// The frame's scene_renderer, valid from scene_hook until renderer_hook returns.
static void* frame_scene{};
static bool writable(void* pointer,size_t bytes){MEMORY_BASIC_INFORMATION m{};return pointer&&VirtualQuery(pointer,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(m.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE))&&uintptr_t(pointer)+bytes>=uintptr_t(pointer)&&uintptr_t(pointer)+bytes<=uintptr_t(m.BaseAddress)+m.RegionSize;}
struct RendererSaved {unsigned char flags[9];double threshold,intensity;};
static bool renderer_apply(void* object,const AnySceneParametersV1* p,RendererSaved* old,const uint32_t* details=nullptr){
 if(!writable(object,0x680))return false;auto b=(unsigned char*)object;
 memcpy(old->flags,b+0x638,9);memcpy(&old->threshold,b+0x648,8);memcpy(&old->intensity,b+0x650,8);
 for(auto v:old->flags)if(v>1)return false;
 if(!std::isfinite(old->threshold)||!std::isfinite(old->intensity))return false;
 auto choice=[&](size_t off,uint32_t v){if(v)b[off]=v==2;};
 if(details)for(size_t i=0;i<3;++i)choice(0x63e+i,details[i]);
 choice(0x638,p->aa);choice(0x639,p->ssao);choice(0x63a,p->bloom);choice(0x63b,p->shadows);choice(0x63c,p->fog_blur);
 if(p->bloom==2){double threshold=p->bloom_threshold,intensity=p->bloom_intensity;memcpy(b+0x648,&threshold,8);memcpy(b+0x650,&intensity,8);}return true;
}
static void renderer_restore(void* object,const RendererSaved* old,const AnySceneParametersV1* p,const uint32_t* details=nullptr){auto b=(unsigned char*)object;const uint32_t choices[]={p->aa,p->ssao,p->bloom,p->shadows,p->fog_blur};for(size_t i=0;i<5;++i)if(choices[i])b[0x638+i]=old->flags[i];if(details)for(size_t i=0;i<3;++i)if(details[i])b[0x63e+i]=old->flags[6+i];if(p->bloom==2){memcpy(b+0x648,&old->threshold,8);memcpy(b+0x650,&old->intensity,8);}}
using RenderFn=void(*)(void*);
static void scoped_render(void* self,const AnySceneParametersV1* p,const uint32_t* details=nullptr){RendererSaved old{};bool changed=false;
 __try{changed=renderer_apply(self,p,&old,details);if(!changed)++rejected_frames;((RenderFn)renderer_cell.original())(self);}
 __finally{if(changed)renderer_restore(self,&old,p,details);}
}
static void renderer_hook(void* self){++renderer_calls;auto policy=snapshot();auto p=policy.scene;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 if(scene_antialiasing::begin()){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.aa=1;}
 // Scene effects replace the game's bloom; their own bloom runs before tone mapping.
 auto fx=scene_effects::copy();
 if(fx.enabled&&fx.finish){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.bloom=1;}
 // The replacement ambient occlusion writes into the game's SSAO target, so the game's SSAO must run.
 if(fx.enabled&&fx.ambient_occlusion&&scene_effects::ao_available){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.ssao=2;}
#endif
 if(p.enabled)scoped_render(self,&p,policy.details.data());else ((RenderFn)renderer_cell.original())(self);
 frame_scene=nullptr;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_antialiasing::finish();
#endif
}
// scene_renderer is transient frame data. Mutate its copied lighting before
// _build_scene creates constants; subsequent native passes consume the same data.
static bool scene_apply(void* object,const AnySceneParametersV1& p){if(!writable(object,0x2bf8))return false;auto b=(unsigned char*)object;double colors[7][3]{},fog{};memcpy(colors,b+0xa90,sizeof(colors));memcpy(&fog,b+0xb60,8);
 for(auto& c:colors)for(auto value:c)if(!std::isfinite(value)||value<0||value>10000)return false;
 if(!std::isfinite(fog)||fog<0||fog>10000)return false;
 double exposure=std::exp2(p.light_exposure);for(size_t i=0;i<7;++i){double scale=i==4?p.sun:i==5?p.sky:p.ambient;for(auto& value:colors[i])value=std::min(10000.,value*scale*exposure);}fog=std::min(10000.,fog*p.fog);
 memcpy(b+0xa90,colors,sizeof(colors));memcpy(b+0xb60,&fog,8);return true;
}
using SceneFn=void(*)(void*,void*);
static void scene_hook(void* renderer,void* scene){++scene_calls;frame_scene=scene;auto p=copy();if(p.enabled&&!scene_apply(scene,p))++rejected_frames;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 if(writable(scene,0x2bf8))scene_antialiasing::apply_jitter(renderer,scene);
#endif
((SceneFn)scene_cell.original())(renderer,scene);}
// Native scene/UI boundary: the AA replacement records on the native command
// list after scene composition, before either native UI renderer.
static std::atomic<uint64_t> before_hud_calls{};
static bool pre_hud_ready{};
static void pre_hud_hook(void* renderer,void* scene){
 ((SceneFn)pre_hud_cell.original())(renderer,scene);auto calls=++before_hud_calls;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 anyapi_creation_balance_render_camera(scene);
 scene_antialiasing::record(renderer,scene);
#endif
 if(calls==1||calls%600==0){uintptr_t target{};int32_t dimensions[2]{};
  bool valid=safe_read_memory(uintptr_t(renderer)+0x3c0,&target,8)&&target&&safe_read_memory(target+0x34,dimensions,8)&&dimensions[0]>0&&dimensions[0]<=16384&&dimensions[1]>0&&dimensions[1]<=16384;
  log_line(ANY_LOG_INFO,"framework",("SCENE_AA_STAGE before_hud_calls="+std::to_string(calls)+" source_target_valid="+std::to_string(valid)+" width="+std::to_string(valid?dimensions[0]:0)+" height="+std::to_string(valid?dimensions[1]:0)+" diagnostic_scene_target=1").c_str());
 }
}
static void lighting_hook(void* renderer,void* scene){((SceneFn)lighting_cell.original())(renderer,scene);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_lighting::record(renderer,scene);
 scene_effects::record(renderer,scene);
#endif
}
using SsaoFn=void(*)(void*,void*);
static void ssao_hook(void* renderer,void* camera){((SsaoFn)ssao_cell.original())(renderer,camera);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_effects::record_ao(renderer,frame_scene);
#endif
}
static const unsigned char COMPOSITE[]={0x53,0x55,0x56,0x48,0x81,0xec,0x20,0x02,0,0,0x48,0x89,0x8c,0x24,0xc0,0x01,0,0,0x48,0x89,0x94,0x24,0xc8,0x01,0,0,0x48,0x8b,0x05,0xaf,0x0c,0};
static const unsigned char SSAO[]={0x53,0x55,0x48,0x81,0xec,0xc8,0,0,0,0x48,0x89,0x4c,0x24,0x60,0x48,0x89,0x54,0x24,0x68,0x48,0x8d,0x44,0x24,0x70,0x48,0x8d,0x19,0x48,0x8b,0x2d,0xde,0x01};
static const unsigned char ADDITIVE[]={0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x81,0xec,0x50,0x01,0,0,0x48,0x89,0x8c,0x24,0xe8,0,0,0,0x48,0x89,0x94,0x24,0xf0,0,0,0,0x48,0x8b,0x05};
static const unsigned char RECORD_COMMANDS[]={0x53,0x55,0x56,0x48,0x81,0xec,0x10,0x02,0,0,0x48,0x89,0x8c,0x24,0xb0,0x01,0,0,0x48,0x89,0x94,0x24,0xb8,0x01,0,0,0x48,0x8b,0x05,0x07,0x0a,0};
static const unsigned char POST_COMPOSITE[]={0x53,0x55,0x56,0x48,0x81,0xec,0x80,0x04,0,0,0x48,0x89,0x8c,0x24,0x28,0x04,0,0,0x48,0x89,0x94,0x24,0x30,0x04,0,0,0x48,0x8b,0x05,0x17,0x17,0};
static const unsigned char ON_RENDER[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x15,0,0,0,0x48,0x8d,0x08,0x48,0x8b,0x05,0x13,0,0,0,0xff,0x10,0x48,0x83,0xc4,0x28,0xc3};
static const unsigned char BUILD_RENDERABLES[]={0x53,0x55,0x48,0x83,0xec,0x58,0x48,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x28,0x48,0x8b,0x05,0x51,0x03,0,0,0x48,0x83,0x78,0x08,0};
static const unsigned char RENDER_TARGET[]={0x53,0x48,0x81,0xec,0x30,0x2c,0,0,0x48,0x89,0x4c,0x24,0x20,0x8a,0x81,0x90,0,0,0,0x3c,0,0x0f,0x84,0x30,0,0,0,0x8a,0x05,0x17,0x01,0};
static const unsigned char SCENE_TARGET[]={0x53,0x55,0x56,0x57,0x48,0x81,0xec,0x68,0x05,0,0,0x48,0x89,0x8c,0x24,0x10,0x04,0,0,0x48,0x89,0x94,0x24,0x18,0x04,0,0,0x4c,0x8d,0x8c,0x24,0x20};
static void** resolve_cell(uintptr_t caller,size_t end,size_t slot,const unsigned char* target,size_t bytes){uintptr_t cell{},body{};unsigned char prefix[32]{};
 if(!safe_read_memory(caller+end+slot*8,&cell,8)||!cell||!safe_read_memory(cell,&body,8)||!body||!safe_read_memory(body,prefix,bytes)||memcmp(prefix,target,bytes))return nullptr;
 return (void**)cell;
}
// on_render's short body is shared by hundreds of unrelated wrappers. Select
// by the reviewed callee and require one distinct call cell, not one wrapper.
static void** resolve_unique_cell(const std::vector<uintptr_t>& callers,size_t end,size_t slot,const unsigned char* target,size_t bytes){void** selected=nullptr;
 for(auto caller:callers){auto cell=resolve_cell(caller,end,slot,target,bytes);if(!cell)continue;if(selected&&selected!=cell)return nullptr;selected=cell;}return selected;
}
static DWORD WINAPI worker(void*){for(unsigned attempt=0;attempt<120&&!ready;++attempt){auto render=scan_exact(ON_RENDER,sizeof(ON_RENDER)),scene=scan_exact(BUILD_RENDERABLES,sizeof(BUILD_RENDERABLES));
 {auto a=resolve_unique_cell(render,32,1,RENDER_TARGET,sizeof(RENDER_TARGET)),b=resolve_unique_cell(scene,872,8,SCENE_TARGET,sizeof(SCENE_TARGET));
  if(a&&b&&renderer_cell.install(a,(void*)renderer_hook)){if(scene_cell.install(b,(void*)scene_hook)){auto record=scan_exact(RECORD_COMMANDS,sizeof(RECORD_COMMANDS));auto boundary=resolve_unique_cell(record,2600,37,POST_COMPOSITE,sizeof(POST_COMPOSITE));
 if(boundary&&pre_hud_cell.install(boundary,(void*)pre_hud_hook)){pre_hud_ready=true;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_antialiasing::available=true;
 auto light=resolve_unique_cell(record,2600,33,ADDITIVE,sizeof(ADDITIVE));if(light&&lighting_cell.install(light,(void*)lighting_hook)){scene_lighting::available=true;scene_effects::available=true;log_line(ANY_LOG_INFO,"framework","SCENE_LIGHTING resolved=1 location=HDR_AFTER_ADDITIVE_BEFORE_BLOOM_TONEMAP_HUD experimental=1");}
 // _record_commands_composite runs the game's SSAO from its own call cell.
 auto composite=resolve_unique_cell(record,2600,28,COMPOSITE,sizeof(COMPOSITE));uintptr_t composite_body{};
 if(composite&&safe_read_memory(uintptr_t(composite),&composite_body,8)&&composite_body){auto ssao=resolve_cell(composite_body,3280,6,SSAO,sizeof(SSAO));if(ssao&&ssao_cell.install(ssao,(void*)ssao_hook)){scene_effects::ao_available=true;log_line(ANY_LOG_INFO,"framework","SCENE_AO resolved=1 location=COMPOSITE_AFTER_NATIVE_SSAO");}}
 if(!scene_effects::ao_available)log_line(ANY_LOG_WARN,"framework","SCENE_AO unavailable=1 native_ssao_kept=1");
#endif
 log_line(ANY_LOG_INFO,"framework","SCENE_AA_STAGE resolved=1 location=AFTER_SCENE_COMPOSITION_BEFORE_HUD scene_replacement_available=1");}
 else log_line(ANY_LOG_WARN,"framework","SCENE_AA_STAGE unavailable=1 replacement_AA_not_exposed=1");
 ready=true;log_line(ANY_LOG_INFO,"framework","SCENE_CONTROLS ready=1 phase=BEFORE_NATIVE_SCENE_BUILD native_AA_bloom_lighting_fog=1 shared_call_cells=1");break;}renderer_cell.remove();}
 }Sleep(1000);}if(!ready)log_line(ANY_LOG_WARN,"framework","SCENE_CONTROLS unavailable native_settings_preserved=1");return 0;}
}
