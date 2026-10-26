#include "effects/contact_gpu.h"
// Contact shadows: recorded at the additive stage before volumetric lighting,
// so fog and beams are added on top of the shadowed scene. The ocean is already
// drawn and writes no depth, so gbuffer_ocean masks it.
namespace scene_effects {
static scene_contact_gpu::Gpu contact_gpu;static uint32_t contact_frame_index{};
static void contact_bypass(int code,const char* why){contact_ready=false;pass_reject(contact_pass,code,why);}
static bool colour(uintptr_t address,std::array<float,4>& out){double v[3]{};if(!scene_frame::read(address,v,sizeof(v)))return false;for(int i=0;i<3;++i){if(!std::isfinite(v[i])||v[i]<0||v[i]>10000)return false;out[i]=float(v[i]);}return true;}
static void record_contact(void* renderer,void* scene){auto p=copy();if(!p.enabled||!p.contact_shadows){contact_ready=false;pass_off(contact_pass);return;}want(contact_pass);
 scene_effects_gpu::Native n;uint32_t view=0;if(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view))return contact_bypass(400+int(n.failure),"native_frame_contract");
 note_size(n);
 // K03: underground and underwater the sun lights nothing on screen; inactive by design.
 if(view&(scene_frame::VIEW_UNDERGROUND|scene_frame::VIEW_UNDERWATER)){contact_ready=false;contact_ms=0;pass_quiet(contact_pass);return;}
 // Below the horizon the sun lights nothing, so there is nothing to shadow.
 float strength=std::clamp((n.sun[1]+.02f)/.1f,0.f,1.f);if(strength<=0){contact_ready=true;contact_ms=0;pass_quiet(contact_pass);return;}
 volumetric::NativeTarget g[3];static constexpr uintptr_t offsets[3]={0x1f8,0x1e8,0x208};
 for(int i=0;i<3;++i){uintptr_t object{};if(!scene_frame::read_value(uintptr_t(renderer)+offsets[i],object)||!volumetric::target(object,g[i],scene_frame::reader)||g[i].width!=n.color.width||g[i].height!=n.color.height)return contact_bypass(410+i,"gbuffer_contract");}
 scene_contact_gpu::Frame f;double m[16]{};
 if(!scene_frame::read(uintptr_t(scene)+0x20+0x1a0,m,sizeof(m))||!colour(uintptr_t(scene)+0xaf0,f.sun_colour)||!colour(uintptr_t(scene)+0xb08,f.sky_colour)||!colour(uintptr_t(scene)+0xb20,f.back_colour))return contact_bypass(413,"camera_or_colours");
 f.metrics={1.f/n.color.width,1.f/n.color.height,float(n.color.width),float(n.color.height)};
 for(int i=0;i<4;++i){f.right[i]=n.right[i];f.up[i]=n.up[i];f.forward[i]=n.forward[i];}f.forward[3]=5000;
 f.projection={float(m[10]),float(m[14]),float(m[11]),float(m[15])};f.sun={n.sun[0],n.sun[1],n.sun[2],strength};
 // Thick enough for blocks, thin enough that parts well in front of a ray do not
 // cast offset shadows. The noise only moves per frame when TAA resolves it.
 f.params={p.contact_length,std::clamp(p.contact_length*.5f,.15f,.5f),taa_resolving()?float(contact_frame_index%64):0.f,60};
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return contact_bypass(420,"command_bindings_or_target_state");
 if(!interface_valid(n.color.resource)||!interface_valid(n.depth.resource)||!interface_valid(g[0].resource)||!interface_valid(g[1].resource)||!interface_valid(g[2].resource))return contact_bypass(421,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return contact_bypass(422,"device");
 for(auto r:{n.color.resource,n.depth.resource,g[0].resource,g[1].resource,g[2].resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return contact_bypass(422,"device");
 if(!ensure_gpu(contact_pass,contact_gpu,device.Get(),platform::queue.Get(),target->GetDesc()))return contact_bypass(423,"gpu_resources");
 auto input=[](const volumetric::NativeTarget& t){return scene_contact_gpu::Gpu::Input{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=contact_gpu.record(list,target,input(n.depth),input(g[0]),input(g[1]),input(g[2]),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return contact_bypass(424,"gpu_record");
 ++contact_frame_index;contact_ready=true;contact_ms=float(contact_gpu.gpu_milliseconds());pass_done(contact_pass,contact_ms);static uint64_t count{};
 if(++count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_CONTACT frames="+std::to_string(count)+" length="+std::to_string(p.contact_length)+" sun_y="+std::to_string(n.sun[1])+" gpu_ms="+std::to_string(contact_ms.load())+" location=HDR_ADDITIVE_BEFORE_VOLUMETRICS").c_str());
}
}
