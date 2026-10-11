#include "effects/ao_gpu.h"
// Ambient occlusion: recorded inside the game's composite pass, right after the
// game's SSAO has filled its target and before the lighting composite reads it.
namespace scene_effects {
static scene_ao_gpu::Gpu ao_gpu;static uint32_t ao_frame_index{};
static void ao_bypass(int code,const char* why,const volumetric::NativeTarget* t=nullptr){ao_ready=false;error=code;auto n=++rejected;if(n==1||n%600==0){std::string line=std::string("SCENE_AO bypass=")+why;if(t)for(int i=0;i<4;++i)line+=" format"+std::to_string(i)+"="+std::to_string(t[i].srv_format)+" width"+std::to_string(i)+"="+std::to_string(t[i].width);log_line(ANY_LOG_WARN,"framework",line.c_str());}}
// Native inputs: 0 SSAO target, 1 depth, 2 normals (gbuffer_1), 3 albedo (gbuffer_0).
template<class Read> bool ao_capture(uintptr_t renderer,uintptr_t scene,volumetric::NativeTarget (&t)[4],scene_ao_gpu::Frame& f,const AnySceneEffectsParametersV1& p,Read read){
 unsigned char special[4]{};if(!read(scene+0x668,special,4)||special[0]||special[1]||special[2]||special[3])return false;
 static constexpr uintptr_t offsets[4]={0x248,0x218,0x1f8,0x1e8};for(int i=0;i<4;++i){uintptr_t object{};if(!read(renderer+offsets[i],&object,8)||!volumetric::target(object,t[i],read))return false;}
 for(auto& x:t)if(x.width!=t[0].width||x.height!=t[0].height)return false;
 uintptr_t main=scene+0x20;double v[3]{},m[16]{};
 auto vector=[&](uintptr_t address,std::array<float,4>& d){if(!read(address,v,sizeof(v)))return false;for(int i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;d[i]=float(v[i]);}return true;};
 if(!vector(main+0xa8,f.right)||!vector(main+0x90,f.up)||!vector(main+0x78,f.forward)||!read(main+0x1a0,m,sizeof(m)))return false;
 if(!std::isfinite(m[0])||!std::isfinite(m[5])||std::abs(m[0])<1e-8||std::abs(m[5])<1e-8)return false;
 f.right[3]=float(1/std::abs(m[0]));f.up[3]=float(1/std::abs(m[5]));f.forward[3]=5000;f.projection={float(m[10]),float(m[14]),float(m[11]),float(m[15])};
 f.metrics={1.f/t[0].width,1.f/t[0].height,float(t[0].width),float(t[0].height)};f.params={p.ao_radius,p.ao_strength,float(ao_frame_index%64),0};
 return scene_ao_gpu::valid(f);
}
static void record_ao(void* renderer,void* scene){auto p=copy();if(!p.enabled||!p.ambient_occlusion||!renderer||!scene){ao_ready=false;return;}
 volumetric::NativeTarget t[4];scene_ao_gpu::Frame f;if(!ao_capture(uintptr_t(renderer),uintptr_t(scene),t,f,p,safe_read_memory)){ao_bypass(200,"native_frame_contract",t);return;}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()){ao_bypass(201,"command_bindings");return;}
 for(auto& x:t)if(!interface_valid(x.resource)){ao_bypass(202,"resources");return;}
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)t[0].resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return ao_bypass(203,"device");
 for(auto& x:t)if(FAILED(((ID3D12Resource*)x.resource)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return ao_bypass(203,"device");
 if(!ao_gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc()))return ao_bypass(204,"gpu_resources",t);
 auto input=[&](int i){return scene_ao_gpu::Gpu::Input{(ID3D12Resource*)t[i].resource,D3D12_RESOURCE_STATES(t[i].state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=ao_gpu.record(list,input(0),input(1),input(2),input(3),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return ao_bypass(205,"gpu_record",t);
 ++ao_frame_index;ao_ready=true;ao_ms=float(ao_gpu.gpu_milliseconds());auto count=++ao_frames;
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_AO frames="+std::to_string(count)+" radius="+std::to_string(p.ao_radius)+" strength="+std::to_string(p.ao_strength)+" gpu_ms="+std::to_string(ao_ms.load())+" width="+std::to_string(t[0].width)+" location=COMPOSITE_AFTER_NATIVE_SSAO").c_str());
}
}
