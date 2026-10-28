#include "effects/ao_gpu.h"
// Ambient occlusion: recorded inside the game's composite pass, before the lighting composite reads the SSAO
// target. K12: controls' ssao_hook calls replace_ssao (below) first; when it renders, the game's SSAO is skipped.
// Otherwise the game's SSAO runs and record_ao follows it, as before.
namespace scene_effects {
static scene_ao_gpu::Gpu ao_gpu;static uint32_t ao_frame_index{};
static thread_local bool ao_skip_next{};   // replace_ssao failed this frame for a reason a retry cannot fix
static void ao_bypass(int code,const char* why,const volumetric::NativeTarget* t=nullptr){ao_ready=false;std::string detail;auto n=ao_pass.rejected+1;if(t&&(n==1||n%600==0))for(int i=0;i<4;++i)detail+=" format"+std::to_string(i)+"="+std::to_string(t[i].srv_format)+" width"+std::to_string(i)+"="+std::to_string(t[i].width);pass_reject(ao_pass,code,why,detail);}
// Native inputs: 0 SSAO target, 1 depth, 2 normals (gbuffer_1), 3 albedo (gbuffer_0).
// K03: no view check; bunkers and underwater have ambient light to occlude too. frame_index 0..15 (0 without TAA, K13).
template<class Read> bool ao_capture(uintptr_t renderer,uintptr_t scene,volumetric::NativeTarget (&t)[4],scene_ao_gpu::Frame& f,const AnySceneEffectsParametersV1& p,Read read,float frame_index=0,uint32_t revision=0){
 static constexpr uintptr_t offsets[4]={0x248,0x218,0x1f8,0x1e8};for(int i=0;i<4;++i){uintptr_t object{};if(!read(renderer+offsets[i],&object,8)||!volumetric::target(object,t[i],read))return false;}
 for(auto& x:t)if(x.width!=t[0].width||x.height!=t[0].height)return false;
 uintptr_t main=scene+0x20;double v[3]{},m[16]{};
 auto vector=[&](uintptr_t address,std::array<float,4>& d){if(!read(address,v,sizeof(v)))return false;for(int i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;d[i]=float(v[i]);}return true;};
 if(!vector(main+0xa8,f.right)||!vector(main+0x90,f.up)||!vector(main+0x78,f.forward)||!read(main+0x1a0,m,sizeof(m)))return false;
 if(!std::isfinite(m[0])||!std::isfinite(m[5])||std::abs(m[0])<1e-8||std::abs(m[5])<1e-8)return false;
 f.right[3]=float(1/std::abs(m[0]));f.up[3]=float(1/std::abs(m[5]));f.forward[3]=5000;f.projection={float(m[10]),float(m[14]),float(m[11]),float(m[15])};
 f.metrics={1.f/t[0].width,1.f/t[0].height,float(t[0].width),float(t[0].height)};f.params={p.ao_radius,p.ao_strength,frame_index,float(revision)};
 return scene_ao_gpu::valid(f);
}
// True only when AnyAPI AO was written into the game's SSAO target this call.
static bool record_ao(void* renderer,void* scene){if(std::exchange(ao_skip_next,false))return false;
 auto p=copy();if(!p.enabled||!p.ambient_occlusion||!renderer||!scene){ao_ready=false;pass_off(ao_pass);return false;}want(ao_pass);auto x=copy_extras();
 volumetric::NativeTarget t[4];scene_ao_gpu::Frame f;if(!ao_capture(uintptr_t(renderer),uintptr_t(scene),t,f,p,scene_frame::reader,taa_resolving()?float(ao_frame_index&15):0.f,x.shading_revision)){ao_bypass(200,"native_frame_contract",t);return false;}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()){ao_bypass(201,"command_bindings");return false;}
 for(auto& x:t)if(!interface_valid(x.resource)){ao_bypass(202,"resources");return false;}
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)t[0].resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device)))){ao_bypass(203,"device");return false;}
 for(auto& x:t)if(FAILED(((ID3D12Resource*)x.resource)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get()){ao_bypass(203,"device");return false;}
 if(!ensure_gpu(ao_pass,ao_gpu,device.Get(),platform::queue.Get(),target->GetDesc())){ao_bypass(204,"gpu_resources",t);return false;}
 auto input=[&](int i){return scene_ao_gpu::Gpu::Input{(ID3D12Resource*)t[i].resource,D3D12_RESOURCE_STATES(t[i].state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=ao_gpu.record(list,input(0),input(1),input(2),input(3),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered){ao_bypass(205,"gpu_record",t);return false;}
 ++ao_frame_index;ao_ready=true;ao_ms=float(ao_gpu.gpu_milliseconds());pass_done(ao_pass,ao_ms);auto count=++ao_frames;
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_AO frames="+std::to_string(count)+" radius="+std::to_string(p.ao_radius)+" strength="+std::to_string(p.ao_strength)+" gpu_ms="+std::to_string(ao_ms.load())+" width="+std::to_string(t[0].width)+" revision="+std::to_string(x.shading_revision)+" replaces_native_ssao="+std::to_string(int(native_ssao_skipped.load()))+" location=COMPOSITE_SSAO").c_str());
 return true;
}
// K12 seam (scene_frame::seams.replace_ssao, called by controls' ssao_hook before the game's SSAO). True: AnyAPI AO
// is in the SSAO target and the game's SSAO, fully computed and then overwritten until now, is skipped.
// ANY_FX_KEEP_NATIVE_SSAO keeps today's order. A failure other than 201 (command bindings, which the game's SSAO
// sets up) would fail again right after the native SSAO, so that retry is skipped.
static bool replace_ssao(void* renderer,void* scene){ao_skip_next=false;
 if(copy_extras().flags&ANY_FX_KEEP_NATIVE_SSAO){native_ssao_skipped=false;return false;}
 bool done=record_ao(renderer,scene);native_ssao_skipped=done;
 if(!done){int code=scene_frame::board.pass_error[ANY_PASS_AO].load(std::memory_order_relaxed);ao_skip_next=code>=200&&code!=201;}
 return done;}
}
static const bool anyapi_ao_seam=(scene_frame::seams.replace_ssao=&scene_effects::replace_ssao,true);
