#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <utility>
#include "effects_frame.h"
#include "effects_bytecode.h"
#include "../gpu_timer.h"
// Scene effects pass: exposure metering, bloom chain and the finishing
// composite, recorded on the game's command list at the HDR additive stage.
namespace scene_effects_gpu {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
// A borrowed game target and its recorded state (namespace scope so it can be a defaulted argument).
struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
class Gpu {
 // Descriptor groups of six (t0..t5), one per draw. t4/t5 (normals, ocean) are bound in the Adapt groups only.
 static constexpr UINT slots=6,levels=6,group_luma=0,group_adapt=1,group_down=3,group_up=group_down+levels,group_composite=group_up+levels-1,groups=group_composite+2,readback_slots=8;
 struct Texture {ComPtr<ID3D12Resource> resource;D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};UINT width{},height{};};
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;
 ComPtr<ID3D12PipelineState> luma_pso,adapt_pso,down_pso,up_pso,composite_pso;
 Texture source,luma;std::array<Texture,2> adapted;std::array<Texture,levels> bloom;ComPtr<ID3D12Resource> depth_input,normals_input,ocean_input,readback;const float* mapped{};
 std::array<bool,readback_slots> metered{};bool used{};
 ComPtr<ID3D12DescriptorHeap> srv,rtv;UINT width{},height{},srv_stride{},rtv_stride{};DXGI_FORMAT format{};HRESULT last_error{};uint64_t frame{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 D3D12_GPU_DESCRIPTOR_HANDLE gpu_group(UINT group){auto h=srv->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(group*slots)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE output(UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*rtv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 bool create(Texture& t,UINT w,UINT h,DXGI_FORMAT f,bool render){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=f;td.SampleDesc.Count=1;td.Flags=render?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;t.width=w;t.height=h;t.state=D3D12_RESOURCE_STATE_COMMON;return checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&t.resource)));}
 void view(UINT group,UINT slot,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(group*slots+slot));}
 void to(ID3D12GraphicsCommandList* list,Texture& t,D3D12_RESOURCE_STATES state){if(t.state==state)return;auto b=barrier(t.resource.Get(),t.state,state);list->ResourceBarrier(1,&b);t.state=state;}
 template<size_t N> bool pipeline(ComPtr<ID3D12PipelineState>& out,const unsigned char(&ps)[N],DXGI_FORMAT f,bool additive=false){D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={effects_bytecode::Vertex,sizeof(effects_bytecode::Vertex)};pd.PS={ps,N};auto& blend=pd.BlendState.RenderTarget[0];blend.RenderTargetWriteMask=15;if(additive){blend.BlendEnable=TRUE;blend.SrcBlend=blend.DestBlend=blend.SrcBlendAlpha=blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;}pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=f;pd.SampleDesc.Count=1;return checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&out)));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc){device=d;queue=q;width=UINT(desc.Width);height=desc.Height;format=desc.Format;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=slots;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,UINT(sizeof(Frame)/4)};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  // LOW-1: no input layout, and only the pixel stage reads root data.
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  if(!pipeline(luma_pso,effects_bytecode::Luma,DXGI_FORMAT_R32G32_FLOAT)||!pipeline(adapt_pso,effects_bytecode::Adapt,DXGI_FORMAT_R32G32B32A32_FLOAT)||!pipeline(down_pso,effects_bytecode::Down,DXGI_FORMAT_R16G16B16A16_FLOAT)||!pipeline(up_pso,effects_bytecode::Up,DXGI_FORMAT_R16G16B16A16_FLOAT,true)||!pipeline(composite_pso,effects_bytecode::Composite,format))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=groups*slots;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=4+levels;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  if(!create(source,width,height,format,false)||!create(luma,64,36,DXGI_FORMAT_R32G32_FLOAT,true)||!create(adapted[0],1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,true)||!create(adapted[1],1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,true))return false;
  for(UINT k=0;k<levels;++k)if(!create(bloom[k],std::max(1u,width>>(k+1)),std::max(1u,height>>(k+1)),DXGI_FORMAT_R16G16B16A16_FLOAT,true))return false;
  // Unused slots get null views so every table entry is defined.
  for(UINT i=0;i<groups*slots;++i)view(i/slots,i%slots,nullptr,DXGI_FORMAT_R8G8B8A8_UNORM);
  view(group_luma,0,source.resource.Get(),format);
  for(UINT w=0;w<2;++w){view(group_adapt+w,0,luma.resource.Get(),DXGI_FORMAT_R32G32_FLOAT);view(group_adapt+w,1,adapted[1-w].resource.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT);
   view(group_composite+w,0,source.resource.Get(),format);view(group_composite+w,1,bloom[0].resource.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(group_composite+w,2,adapted[w].resource.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT);}
  for(UINT k=0;k<levels;++k)view(group_down+k,0,k?bloom[k-1].resource.Get():source.resource.Get(),k?DXGI_FORMAT_R16G16B16A16_FLOAT:format);
  for(UINT k=0;k+1<levels;++k)view(group_up+k,0,bloom[k+1].resource.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);
  device->CreateRenderTargetView(luma.resource.Get(),nullptr,output(0));device->CreateRenderTargetView(adapted[0].resource.Get(),nullptr,output(1));device->CreateRenderTargetView(adapted[1].resource.Get(),nullptr,output(2));
  for(UINT k=0;k<levels;++k)device->CreateRenderTargetView(bloom[k].resource.Get(),nullptr,output(4+k));
  // Readback ring for the adapted values, read a few frames after writing.
  D3D12_HEAP_PROPERTIES rh{};rh.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=readback_slots*512;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(!checked(device->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
  void* data{};D3D12_RANGE all{0,SIZE_T(bd.Width)};if(!checked(readback->Map(0,&all,&data)))return false;mapped=(const float*)data;
  timer.create(device.Get(),queue.Get());return true;
 }
 void constants(ID3D12GraphicsCommandList* list,Frame f,float flag,float second,float tx,float ty){f.extras[0]=flag;f.extras[1]=second;f.extras[2]=tx;f.extras[3]=ty;list->SetGraphicsRoot32BitConstants(0,UINT(sizeof(Frame)/4),&f,0);}
 void viewport(ID3D12GraphicsCommandList* list,UINT w,UINT h){D3D12_VIEWPORT vp{0,0,float(w),float(h),0,1};D3D12_RECT sc{0,0,LONG(w),LONG(h)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);}
public:
 // Adapted values read back from the GPU: fast and slow log2 luminance, sun visibility.
 struct Measured {bool valid{};float fast{},slow{},visibility{};};
 // A slot is only read if the frame that wrote it metered (K25: metering is skipped when unused).
 Measured measured()const{Measured m;if(!mapped||frame<readback_slots||!metered[frame%readback_slots])return m;auto p=mapped+size_t(frame%readback_slots)*128;if(p[3]<.5f||!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))return m;m.valid=true;m.fast=p[0];m.slow=p[1];m.visibility=p[2];return m;}
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 bool loaded()const{return bool(source.resource);}
 // Already initialised for this device, queue and target: ensure() would do nothing.
 bool current(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc)const{return source.resource&&device.Get()==d&&queue.Get()==q&&width==desc.Width&&height==desc.Height&&format==desc.Format;}
 // K09: drop every resource and reference (after one idle); ensure() re-creates lazily. Caller holds platform::render_mutex.
 void release(){if(device)idle();*this=Gpu{};}
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc){if(!d||!q||desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1||desc.Width<16||desc.Height<16||desc.Width>8192||desc.Height>8192||desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;if(current(d,q,desc))return true;
  // CSG-17: a failed drain blocks re-creation only on the same device; a new device proceeds.
  if(device&&!idle()&&device.Get()==d)return false;Gpu next;if(!next.initialize(d,q,desc)){last_error=next.error();return false;}*this=std::move(next);return true;}
 using Input=scene_effects_gpu::Input;
 // The HDR target is borrowed in RENDER_TARGET state and returned in it; the
 // depth buffer, normals and ocean are returned in their recorded states. reset drops adaptation history.
 // normals (gbuffer_1) and ocean (gbuffer_ocean) are optional: without them the sun test is depth only (V2).
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,ID3D12Resource* depth,D3D12_RESOURCE_STATES depth_state,const Frame& f,bool reset,Input normals={},Input ocean={}){
  if(!source.resource||!list||!target||!depth||target==depth||!valid(f))return false;
  auto td=target->GetDesc(),dd=depth->GetDesc();if(td.Width!=width||td.Height!=height||td.Format!=format||dd.Width!=width||dd.Height!=height||dd.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||(dd.Format!=DXGI_FORMAT_R32_TYPELESS&&dd.Format!=DXGI_FORMAT_R32_FLOAT)||(dd.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return false;
  auto gbuffer=[&](const Input& i){if(!i.resource)return true;auto d=i.resource->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==width&&d.Height==height&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS);};
  if(!gbuffer(normals)||!gbuffer(ocean))return false;
  // K09: inputs are held by reference, so a recreated target can never reuse a stale view's address.
  // Views in a shader-visible heap that a submitted list may still read are rewritten only after a drain.
  if(depth_input.Get()!=depth||normals_input.Get()!=normals.resource||ocean_input.Get()!=ocean.resource){if(used&&!idle())return false;
   depth_input=depth;normals_input=normals.resource;ocean_input=ocean.resource;for(UINT g:{group_luma,group_adapt,group_adapt+1,group_composite,group_composite+1})view(g,3,depth,DXGI_FORMAT_R32_FLOAT);
   for(UINT w=0;w<2;++w){view(group_adapt+w,4,normals.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(group_adapt+w,5,ocean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}}
  // XC-3: a fresh Gpu's adapted targets are undefined until cleared.
  if(frame==0)reset=true;
  // K25: metering (Luma, Adapt, readback) runs only when eye adaptation or glare reads it. Glare meters even
  // while the sun is off screen (sun_screen.z 0), so Adapt can hold the last visibility (FIN-3).
  const bool meter=meters(f);
  const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  timer.begin(list);
  {D3D12_RESOURCE_BARRIER b=barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);to(list,source,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(source.resource.Get(),target);b=barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);to(list,source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
  {D3D12_RESOURCE_BARRIER b[3];UINT n=0;if(depth_state!=read)b[n++]=barrier(depth,depth_state,read);
   if(meter)for(auto* i:{&normals,&ocean})if(i->resource&&i->state!=read)b[n++]=barrier(i->resource,i->state,read);if(n)list->ResourceBarrier(n,b);}
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  // Metering.
  UINT write=UINT(frame&1);D3D12_CPU_DESCRIPTOR_HANDLE handle{};metered[frame%readback_slots]=meter;
  if(meter){
   to(list,luma,D3D12_RESOURCE_STATE_RENDER_TARGET);handle=output(0);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);viewport(list,64,36);list->SetPipelineState(luma_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu_group(group_luma));constants(list,f,0,0,1.f/64,1.f/36);list->DrawInstanced(3,1,0,0);to(list,luma,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
   auto& previous=adapted[1-write];
   if(reset){to(list,previous,D3D12_RESOURCE_STATE_RENDER_TARGET);const float zero[4]{};list->ClearRenderTargetView(output(2-write),zero,0,nullptr);}
   to(list,previous,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);to(list,adapted[write],D3D12_RESOURCE_STATE_RENDER_TARGET);handle=output(1+write);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);viewport(list,1,1);list->SetPipelineState(adapt_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu_group(group_adapt+write));constants(list,f,0,0,0,0);list->DrawInstanced(3,1,0,0);
  }
  to(list,adapted[write],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  // Bloom: downsample chain, then tent upsamples added back up the chain.
  if(f.extras[0]>0){
   for(UINT k=0;k<levels;++k){to(list,bloom[k],D3D12_RESOURCE_STATE_RENDER_TARGET);handle=output(4+k);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);viewport(list,bloom[k].width,bloom[k].height);list->SetPipelineState(down_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu_group(group_down+k));
    UINT sw=k?bloom[k-1].width:width,sh=k?bloom[k-1].height:height;constants(list,f,k?0.f:1.f,0,1.f/sw,1.f/sh);list->DrawInstanced(3,1,0,0);to(list,bloom[k],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
   for(UINT k=levels-1;k-->0;){to(list,bloom[k],D3D12_RESOURCE_STATE_RENDER_TARGET);handle=output(4+k);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);viewport(list,bloom[k].width,bloom[k].height);list->SetPipelineState(up_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu_group(group_up+k));
    // FIN-20: each up step weights the smaller level by r (tuning[0]; 1 = V2), so level n carries r^n.
    constants(list,f,f.tuning[0],0,1.f/bloom[k+1].width,1.f/bloom[k+1].height);list->DrawInstanced(3,1,0,0);to(list,bloom[k],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
  }else to(list,bloom[0],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  // K33/FIN-11: the readback copy waits for Adapt; doing it after the bloom chain lets that work overlap.
  if(meter){to(list,adapted[write],D3D12_RESOURCE_STATE_COPY_SOURCE);
   D3D12_TEXTURE_COPY_LOCATION from{},into{};from.pResource=adapted[write].resource.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;into.pResource=readback.Get();into.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;into.PlacedFootprint.Offset=UINT64(frame%readback_slots)*512;into.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,1,1,1,256};list->CopyTextureRegion(&into,0,0,0,&from,nullptr);
   to(list,adapted[write],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
  // Finishing composite into the game's HDR target.
  device->CreateRenderTargetView(target,nullptr,output(3));handle=output(3);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);viewport(list,width,height);list->SetPipelineState(composite_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu_group(group_composite+write));list->SetGraphicsRoot32BitConstants(0,UINT(sizeof(Frame)/4),&f,0);list->DrawInstanced(3,1,0,0);
  timer.end(list);
  {D3D12_RESOURCE_BARRIER b[3];UINT n=0;if(depth_state!=read)b[n++]=barrier(depth,read,depth_state);
   if(meter)for(auto* i:{&normals,&ocean})if(i->resource&&i->state!=read)b[n++]=barrier(i->resource,read,i->state);if(n)list->ResourceBarrier(n,b);}
  ++frame;used=true;return true;
 }
};
}
