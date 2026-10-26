#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include "volumetric_frame.h"
#include "volumetric_bytecode.h"
#include "../gpu_timer.h"
namespace volumetric {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
// gbuffer_ocean (AnyAPI 0.40.2), optional: null binds a 1x1 zero texture, which marks no pixel as ocean.
struct Ocean {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};};
// Lamp paths. 0: marched lamps read from the matrix texture (Integrate). 1 (AMD): lamps in a constant buffer,
// IntegrateConstants built for 0/2/4/8 slots, the smallest that holds the frame's lamps. 2 (AMD, experiment):
// the 8-slot build with a uniform branch per slot. Paths 1 and 2 never use lamp shadows. All paths share one root
// signature size (56 constants + 1 table = 57 DWORDs): the lamp CBV (b1) is the table's last entry, not a root CBV.
enum : uint32_t {PATH_MARCHED=0,PATH_CONSTANTS=1,PATH_BRANCH=2};
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
 ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> integrate,accumulate,composite,matrix_pipeline,probe_pipeline;std::array<ComPtr<ID3D12PipelineState>,3> integrate_small;
 // Inputs hold a reference, so a freed game resource cannot come back at the same address unnoticed.
 ComPtr<ID3D12Resource> volume,matrix_texture,ocean_dummy,depth_input,ocean_input;std::array<ComPtr<ID3D12Resource>,2> history;std::array<D3D12_RESOURCE_STATES,2> history_state{};gpu_timer::Timer timer;std::array<ComPtr<ID3D12Resource>,12> shadow_inputs;
 ComPtr<ID3D12DescriptorHeap> srv,staging,rtv;UINT width{},height{},srv_stride{},rtv_stride{};DXGI_FORMAT format{};bool read{},history_cleared{},ocean_cleared{};
 // Descriptor ring (K08): the shader-visible heap holds TABLES copies of the TABLE-entry table. Views are written
 // to the CPU-only staging heap; a change copies staging into the next table once the GPU has finished every
 // command list that bound it (fence value table_use). Nothing in a table a queued frame reads is ever rewritten.
 static constexpr UINT TABLE=19,STRIDE=TABLE+1,TABLES=4;UINT table{TABLES-1};std::array<UINT64,TABLES> table_use{};
 // K09: each table holds a reference to the resources its views name (depth, 12 shadows, ocean) until it is reused.
 std::array<std::array<ComPtr<ID3D12Resource>,14>,TABLES> table_refs;
 ComPtr<ID3D12Fence> ring_fence;UINT64 serial{},signalled{},pending{};ID3D12CommandList* ring_list{};
 // Paths 1-2: lights travel in a constant buffer, one 512-byte upload slot per table. Entry TABLE of table t is a
 // CBV of slot t, written once; every lamp frame takes the next table, so a slot is rewritten only after the GPU
 // has finished the frames that bound it (table_use). A root CBV here made 59 of the 64 root DWORDs (vkd3d could
 // not create it with 8 descriptor sets, and validation layers add their own root parameters).
 UINT path{};ComPtr<ID3D12Resource> lamp_buffer;unsigned char* lamp_mapped{};static constexpr UINT lamp_stride=512;HRESULT last_error{};
 uint32_t idle_calls{},ring_waits{},ring_unconfirmed{};bool permutations{true};
 ComPtr<ID3D12Resource> probe_target,probe_readback;ComPtr<ID3D12Fence> probe_fence;ComPtr<ID3D12GraphicsCommandList> probe_list;
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT probe_footprint{};UINT64 probe_bytes{};bool probe_recorded{},probe_submitted{},probe_consumed{},probe_written{};UINT64 probe_sequence{1};
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=staging->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE ring_cpu(UINT t){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(t)*STRIDE*srv_stride;return h;}
 D3D12_GPU_DESCRIPTOR_HANDLE ring_gpu(UINT t){auto h=srv->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(t)*STRIDE*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE output(UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*rtv_stride;return h;}
 // Whole-queue wait: only ensure() (resize, device or path change) and a ring use never seen submitted call it.
 bool idle(){++idle_calls;ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 // RING_WAIT: true once the GPU is past fence value `use`. A value signalled from submitted() gets a targeted wait
 // (3 s at most); one never seen submitted falls back to idle(). False means the caller bypasses this frame.
 bool reusable(UINT64 use){if(!use||ring_fence->GetCompletedValue()>=use)return true;
  if(use>signalled){++ring_unconfirmed;return idle();}
  ++ring_waits;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(ring_fence->SetEventOnCompletion(use,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 bool texture(ComPtr<ID3D12Resource>& out,UINT w,UINT h,DXGI_FORMAT f,bool render){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=f;td.SampleDesc.Count=1;td.Flags=render?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;return checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out)));}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 // blend: K07 composite, target.rgb = src.rgb + target.rgb*src.a; alpha is never written.
 template<size_t N> bool pipeline(ComPtr<ID3D12PipelineState>& out,const unsigned char(&ps)[N],DXGI_FORMAT f,bool blend=false){D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={volumetric_bytecode::Vertex,sizeof(volumetric_bytecode::Vertex)};pd.PS={ps,N};auto& b=pd.BlendState.RenderTarget[0];b.RenderTargetWriteMask=15;
  if(blend){b.BlendEnable=TRUE;b.SrcBlend=D3D12_BLEND_ONE;b.DestBlend=D3D12_BLEND_SRC_ALPHA;b.BlendOp=D3D12_BLEND_OP_ADD;b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=7;}   // red, green, blue
  pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=f;pd.SampleDesc.Count=1;return checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&out)));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc,UINT lamp_path){device=d;queue=q;path=lamp_path;width=UINT(desc.Width);height=desc.Height;format=desc.Format;D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=TABLE;ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_CBV;ranges[1].NumDescriptors=1;ranges[1].BaseShaderRegister=1;ranges[1].OffsetInDescriptorsFromTableStart=TABLE;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,56};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={path?2u:1u,ranges};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  // LOW-1: no input layout (SV_VertexID only) and nothing but the pixel shader reads the root.
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  constexpr auto hdr=DXGI_FORMAT_R16G16B16A16_FLOAT;
  bool integrate_ok=path==PATH_CONSTANTS?pipeline(integrate,volumetric_bytecode::IntegrateConstants,hdr)&&pipeline(integrate_small[0],volumetric_bytecode::IntegrateConstants0,hdr)&&pipeline(integrate_small[1],volumetric_bytecode::IntegrateConstants2,hdr)&&pipeline(integrate_small[2],volumetric_bytecode::IntegrateConstants4,hdr):
   path==PATH_BRANCH?pipeline(integrate,volumetric_bytecode::IntegrateConstantsBranch,hdr):pipeline(integrate,volumetric_bytecode::Integrate,hdr);
  if(!integrate_ok||!pipeline(accumulate,volumetric_bytecode::Accumulate,hdr)||!pipeline(composite,volumetric_bytecode::Composite,format,true)||!pipeline(matrix_pipeline,volumetric_bytecode::Matrices,DXGI_FORMAT_R32G32B32A32_FLOAT)||!pipeline(probe_pipeline,volumetric_bytecode::Probe,DXGI_FORMAT_R32G32B32A32_FLOAT))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=STRIDE*TABLES;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.NumDescriptors=TABLE;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&staging))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=7;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  if(!checked(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&ring_fence))))return false;
  if(!texture(matrix_texture,4,22,DXGI_FORMAT_R32G32B32A32_FLOAT,true)||!texture(history[0],(width+1)/2,(height+1)/2,hdr,true)||!texture(history[1],(width+1)/2,(height+1)/2,hdr,true)||!texture(volume,(width+1)/2,(height+1)/2,hdr,true)||!texture(ocean_dummy,1,1,hdr,true))return false;if(!texture(probe_target,32,24,DXGI_FORMAT_R32G32B32A32_FLOAT,true)||!checked(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&probe_fence))))return false;
  auto probe_desc=probe_target->GetDesc();device->GetCopyableFootprints(&probe_desc,0,1,0,&probe_footprint,nullptr,nullptr,&probe_bytes);
  D3D12_HEAP_PROPERTIES readback_heap{};readback_heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=probe_bytes;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(!checked(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&probe_readback))))return false;device->CreateRenderTargetView(probe_target.Get(),nullptr,output(3));
  // Staging slots: 0 null (K07: nothing reads scene colour), 1 depth, 2 and 5-15 shadows, 3 volume, 4 matrices,
  // 16-17 history, 18 ocean. RTVs: 0 volume, 1 target, 2 matrices, 3 probe, 4-5 history, 6 ocean dummy.
  view(0,nullptr,hdr);view(4,matrix_texture.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT);device->CreateRenderTargetView(matrix_texture.Get(),nullptr,output(2));view(3,volume.Get(),hdr);device->CreateRenderTargetView(volume.Get(),nullptr,output(0));
  device->CreateRenderTargetView(ocean_dummy.Get(),nullptr,output(6));
  for(UINT i=0;i<2;++i){history_state[i]=D3D12_RESOURCE_STATE_COMMON;view(16+i,history[i].Get(),hdr);device->CreateRenderTargetView(history[i].Get(),nullptr,output(4+i));}
  if(path){D3D12_HEAP_PROPERTIES upload{};upload.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=lamp_stride*TABLES;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   if(!checked(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&lamp_buffer))))return false;D3D12_RANGE none{0,0};if(!checked(lamp_buffer->Map(0,&none,(void**)&lamp_mapped)))return false;memset(lamp_mapped,0,lamp_stride*TABLES);
    for(UINT t=0;t<TABLES;++t){D3D12_CONSTANT_BUFFER_VIEW_DESC cd{lamp_buffer->GetGPUVirtualAddress()+UINT64(t)*lamp_stride,lamp_stride};auto h=ring_cpu(t);h.ptr+=SIZE_T(TABLE)*srv_stride;device->CreateConstantBufferView(&cd,h);}}
  timer.create(device.Get(),queue.Get());return true;
 }
 ID3D12PipelineState* integrate_for(UINT lamps){if(path!=PATH_CONSTANTS||!permutations||lamps>4)return integrate.Get();return integrate_small[lamps==0?0:lamps<=2?1:2].Get();}
public:
 struct ProbeStats{float lit_min{1},lit_max{},lit_average{},coverage{},distance{};};
 void request_probe(){if(probe_consumed){probe_recorded=probe_submitted=probe_consumed=false;++probe_sequence;}}
 // After each ExecuteCommandLists on any queue (platform::after_execute). Signals the ring fence after the list
 // that last bound a table, and the probe fence after the probe's list.
 void submitted(ID3D12CommandQueue* submitted_queue,UINT count,ID3D12CommandList*const* lists){if(submitted_queue!=queue.Get())return;
  for(UINT i=0;i<count;++i){if(pending&&lists[i]==ring_list){if(SUCCEEDED(queue->Signal(ring_fence.Get(),pending)))signalled=pending;pending=0;ring_list=nullptr;}
   if(probe_recorded&&!probe_submitted&&lists[i]==probe_list.Get()&&SUCCEEDED(queue->Signal(probe_fence.Get(),probe_sequence))){probe_submitted=true;probe_list.Reset();}}}
 // True while a recorded list still waits for submitted(); the .inc skips the render mutex otherwise (CPU-13).
 bool wants_submit()const{return pending||(probe_recorded&&!probe_submitted);}
 bool probe(ProbeStats& stats){if(probe_consumed||!probe_submitted||probe_fence->GetCompletedValue()<probe_sequence)return false;unsigned char* data{};D3D12_RANGE range{0,SIZE_T(probe_bytes)};if(FAILED(probe_readback->Map(0,&range,(void**)&data)))return false;
  for(UINT y=0;y<24;++y)for(UINT x=0;x<32;++x){auto p=(float*)(data+probe_footprint.Offset+y*probe_footprint.Footprint.RowPitch)+x*4;stats.lit_min=std::min(stats.lit_min,p[0]);stats.lit_max=std::max(stats.lit_max,p[0]);stats.lit_average+=p[0]/768;stats.coverage+=p[1]/768;stats.distance+=p[2]/768;}
  D3D12_RANGE none{0,0};probe_readback->Unmap(0,&none);probe_consumed=true;return true;}
 HRESULT error()const{return last_error;}
 uint32_t lamp_path()const{return path;}
 uint32_t idle_count()const{return idle_calls;}
 uint32_t ring_wait_count()const{return ring_waits;}
 uint32_t ring_unconfirmed_count()const{return ring_unconfirmed;}
 ID3D12Device* current_device()const{return device.Get();}
 bool loaded()const{return volume!=nullptr;}
 // K09: frees every GPU object after one queue drain (the .inc calls it once, 120 frames after the pass went off).
 void release(){if(device)idle();auto calls=idle_calls,waits=ring_waits,unconfirmed=ring_unconfirmed;*this=Gpu{};idle_calls=calls;ring_waits=waits;ring_unconfirmed=unconfirmed;}
 // Path 1 only: false always draws the 8-slot build (tests compare the two; reset by every rebuild).
 void use_permutations(bool on){permutations=on;}
 // K25: the next rendered frame starts from cleared history (0,0,0,1), what skipped frames would have left there.
 void forget_history(){history_cleared=false;}
 // Smoothed GPU milliseconds of the whole lighting pass; negative until measured.
 double gpu_milliseconds()const{return timer.milliseconds();}
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc,UINT lamp_path=PATH_MARCHED){if(!d||!q||lamp_path>PATH_BRANCH||desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1||!desc.Width||!desc.Height||desc.Width>8192||desc.Height>8192||(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return false;
  if(volume&&device.Get()==d&&queue.Get()==q&&width==desc.Width&&height==desc.Height&&format==desc.Format&&path==lamp_path)return true;
  // K09: a lost old device must not block the new one; only a resize or path change on the same device waits.
  if(device&&!idle()&&device.Get()==d)return false;
  Gpu next;if(!next.initialize(d,q,desc,lamp_path)){last_error=next.error();return false;}auto calls=idle_calls,waits=ring_waits,unconfirmed=ring_unconfirmed;*this=std::move(next);idle_calls=calls;ring_waits=waits;ring_unconfirmed=unconfirmed;return true;}
 // Borrowed native targets are sampled only for this pass. Their original
 // states and all caller bindings must be restored before the native code resumes.
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,ID3D12Resource* depth,D3D12_RESOURCE_STATES depth_state,ID3D12Resource* shadow,D3D12_RESOURCE_STATES shadow_state,const Frame& f){
  std::array<ID3D12Resource*,12> shadows{};shadows.fill(shadow);std::array<D3D12_RESOURCE_STATES,12> states{};states.fill(shadow_state);return record(list,target,depth,depth_state,shadows,states,f);
 }
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,ID3D12Resource* depth,D3D12_RESOURCE_STATES depth_state,const std::array<ID3D12Resource*,12>& shadows,const std::array<D3D12_RESOURCE_STATES,12>& states,const Frame& f,const Ocean& ocean_in=Ocean{}){
  if(!volume||!list||!target||!depth||!valid(f)||target==depth)return false;
  auto supported=[](const D3D12_RESOURCE_DESC& d,bool shadow){return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.SampleDesc.Count==1&&d.DepthOrArraySize==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==DXGI_FORMAT_R32_TYPELESS||d.Format==DXGI_FORMAT_R32_FLOAT||(shadow&&(d.Format==DXGI_FORMAT_R16_TYPELESS||d.Format==DXGI_FORMAT_R16_UNORM)));};
  auto dd=depth->GetDesc();if(!supported(dd,false)||dd.Width!=width||dd.Height!=height)return false;
  UINT sun_count=UINT(f.forward[3]),count=f.local_shadow_count>0?8+UINT(f.local_shadow_count):sun_count;bool changed=depth_input.Get()!=depth;
  for(UINT i=0;i<count;++i){if(!shadows[i]||shadows[i]==target||shadows[i]==depth||!supported(shadows[i]->GetDesc(),true))return false;changed|=shadow_inputs[i].Get()!=shadows[i];}
  // An unusable ocean input binds the zero texture: no pixel is ocean, so sea modes 1-2 change nothing.
  ID3D12Resource* ocean=ocean_in.resource;if(ocean){auto od=ocean->GetDesc();if(ocean==target||ocean==depth||od.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||od.SampleDesc.Count!=1||od.DepthOrArraySize!=1||od.Width!=width||od.Height!=height||(od.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||(od.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&od.Format!=DXGI_FORMAT_R16G16B16A16_TYPELESS))ocean=nullptr;}
  ID3D12Resource* ocean_view=ocean?ocean:ocean_dummy.Get();changed|=ocean_input.Get()!=ocean_view;
  ++serial;
  // Paths 1-2 take the next table every frame: its lamp slot is then free to rewrite.
  if(changed||path){UINT next=(table+1)%TABLES;if(!reusable(table_use[next]))return false;
   depth_input=depth;view(1,depth,DXGI_FORMAT_R32_FLOAT);
   for(UINT i=0;i<12;++i){auto input=shadows[std::min(i,count-1)];shadow_inputs[i]=input;auto sd=input->GetDesc();view(i?i+4:2,input,sd.Format==DXGI_FORMAT_R16_TYPELESS||sd.Format==DXGI_FORMAT_R16_UNORM?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R32_FLOAT);}
   ocean_input=ocean_view;view(18,ocean_view,DXGI_FORMAT_R16G16B16A16_FLOAT);
   auto& refs=table_refs[next];refs[0]=depth_input;for(UINT i=0;i<12;++i)refs[1+i]=shadow_inputs[i];refs[13]=ocean_input;
   device->CopyDescriptorsSimple(TABLE,ring_cpu(next),cpu(0),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);table=next;}
  if(path){static_assert(sizeof(f.lights)==lamp_stride);memcpy(lamp_mapped+SIZE_T(table)*lamp_stride,f.lights.data(),lamp_stride);}
  table_use[table]=serial;
  timer.begin(list);
  // K07: no copy of the target. The volume becomes a render target; the target stays one.
  {auto b=barrier(volume.Get(),read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);}
  if(!ocean_cleared){auto b=barrier(ocean_dummy.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);const float zero[4]{};list->ClearRenderTargetView(output(6),zero,0,nullptr);b=barrier(ocean_dummy.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);ocean_cleared=true;}
  if(depth_state!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){auto b=barrier(depth,depth_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}for(UINT i=0;i<count;++i){bool duplicate=false;for(UINT j=0;j<i;++j)duplicate|=shadows[j]==shadows[i];if(!duplicate&&states[i]!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){auto b=barrier(shadows[i],states[i],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}}
  if(ocean&&ocean_in.state!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){auto b=barrier(ocean,ocean_in.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRootDescriptorTable(1,ring_gpu(table));
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  auto matrix_barrier=barrier(matrix_texture.Get(),read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&matrix_barrier);
  auto matrix_handle=output(2);list->OMSetRenderTargets(1,&matrix_handle,FALSE,nullptr);list->SetPipelineState(matrix_pipeline.Get());
  for(UINT i=0;i<count;++i){D3D12_VIEWPORT vp{0,float(i),4,1,0,1};D3D12_RECT sc{0,LONG(i),4,LONG(i+1)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);list->SetGraphicsRoot32BitConstants(0,16,i>=8?f.local_shadows[i-8].data():i?f.additional_shadows[i-1].data():f.shadow.data(),0);list->DrawInstanced(3,1,0,0);}
  for(UINT i=0;i<UINT(f.fog_color[3]);++i){D3D12_VIEWPORT vp{0,float(12+i),4,1,0,1};D3D12_RECT sc{0,LONG(12+i),4,LONG(13+i)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);list->SetGraphicsRoot32BitConstants(0,16,f.lights[i].data(),0);list->DrawInstanced(3,1,0,0);}
  for(UINT i=0;i<2;++i){D3D12_VIEWPORT vp{0,float(20+i),4,1,0,1};D3D12_RECT sc{0,LONG(20+i),4,LONG(21+i)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);list->SetGraphicsRoot32BitConstants(0,16,f.temporal[i].data(),0);list->DrawInstanced(3,1,0,0);}
  matrix_barrier=barrier(matrix_texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&matrix_barrier);list->SetGraphicsRoot32BitConstants(0,56,&f,0);
  D3D12_VIEWPORT viewport{0,0,float((width+1)/2),float((height+1)/2),0,1};D3D12_RECT scissor{0,0,LONG((width+1)/2),LONG((height+1)/2)};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);auto handle=output(0);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(integrate_for(UINT(f.fog_color[3])));list->DrawInstanced(3,1,0,0);
  auto b=barrier(volume.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);
  // Temporal: read one history, write the other. Both must be shader
  // resources before the composite samples them through the same table.
  auto to=[&](UINT i,D3D12_RESOURCE_STATES state){if(history_state[i]!=state){auto t=barrier(history[i].Get(),history_state[i],state);list->ResourceBarrier(1,&t);history_state[i]=state;}};
  // New render targets hold whatever was in video memory (garbage on AMD), so clear the history once before reading it.
  if(!history_cleared){const float clear[4]{0,0,0,1};for(UINT i=0;i<2;++i){to(i,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ClearRenderTargetView(output(4+i),clear,0,nullptr);}history_cleared=true;}
  if(f.temporal[1][3]==1){UINT write=f.temporal[1][4]==1?0:1;to(1-write,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);to(write,D3D12_RESOURCE_STATE_RENDER_TARGET);handle=output(4+write);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(accumulate.Get());list->DrawInstanced(3,1,0,0);}
  to(0,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);to(1,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  viewport.Width=float(width);viewport.Height=float(height);scissor.right=LONG(width);scissor.bottom=LONG(height);list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);device->CreateRenderTargetView(target,nullptr,output(1));handle=output(1);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(composite.Get());list->DrawInstanced(3,1,0,0);timer.end(list);
  if(!probe_recorded){auto probe_barrier=barrier(probe_target.Get(),probe_written?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&probe_barrier);D3D12_VIEWPORT vp{0,0,32,24,0,1};D3D12_RECT sc{0,0,32,24};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);auto ph=output(3);list->OMSetRenderTargets(1,&ph,FALSE,nullptr);list->SetPipelineState(probe_pipeline.Get());list->DrawInstanced(3,1,0,0);probe_barrier=barrier(probe_target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&probe_barrier);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=probe_target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=probe_readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=probe_footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);probe_list=list;probe_recorded=probe_written=true;}
  if(ocean&&ocean_in.state!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){b=barrier(ocean,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,ocean_in.state);list->ResourceBarrier(1,&b);}
  if(depth_state!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){b=barrier(depth,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,depth_state);list->ResourceBarrier(1,&b);}for(UINT i=0;i<count;++i){bool duplicate=false;for(UINT j=0;j<i;++j)duplicate|=shadows[j]==shadows[i];if(!duplicate&&states[i]!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){b=barrier(shadows[i],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,states[i]);list->ResourceBarrier(1,&b);}}
  pending=serial;ring_list=list;read=true;return true;
 }
};
}
