#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <algorithm>
#include "scene_smaa_bytecode.h"
#include "../third_party/smaa/Textures/AreaTex.h"
#include "../third_party/smaa/Textures/SearchTex.h"
#include "../gpu_timer.h"
#include "../anyapi_scene_frame.h"
namespace scene_smaa {
using Microsoft::WRL::ComPtr;
// Shader-visible heap (CBV_SRV_UAV). Root table 1 (t0..t2, 3 descriptors) is
// bound at edges, weights, resolve, temporal_* or output_*; root table 2 (t3
// gbuffer_ocean, t4 gbuffer_0) at extras. K05: the 3-wide tables are packed, so
// widening table 1 to 4 would read the next table's first descriptor.
namespace heap {constexpr UINT edges=0,weights=3,resolve=6,temporal_from1=9,temporal_from0=12,output_history0=15,output_history1=16,output_resolved=17,pad=18,extras=20,count=22,table=3,extras_table=2;}
static_assert(heap::resolve+heap::table<=heap::temporal_from1&&heap::temporal_from1+heap::table<=heap::temporal_from0&&heap::temporal_from0+heap::table<=heap::output_history0&&heap::output_resolved+heap::table<=heap::extras&&heap::extras+heap::extras_table==heap::count);
constexpr UINT root_constants=44; // render_metrics, smoothing, 8 camera rows, options (cbuffer Frame in scene_smaa.hlsl)
// options.x bits (TAA_* in scene_smaa.hlsl): ANY_TAA_* in 0..2, plus the inputs bound this frame.
constexpr uint32_t flag_catmull_rom=1,flag_still_v2=2,flag_thin_rescue=4,flag_api=7,flag_ocean=256,flag_rim=512;
// Optional passes after SMAA. constants: projection, right, up, forward,
// previous camera (w: jitter u), previous right, previous up, previous forward
// (w: jitter v), matching the shader's Frame block after smoothing.
// flags: ANY_TAA_* bits; ocean/albedo: gbuffer_ocean and gbuffer_0 (null = not bound).
// sea_height: camera height above the sea plane (m). dither: output dither in LSB (0 or 1).
struct Extra {float sharpen{};bool temporal{},history_valid{};float history_weight{.9f};ID3D12Resource* depth{};D3D12_RESOURCE_STATES depth_state{};std::array<float,32> constants{};
 uint32_t flags{};float sea_height{},dither{};ID3D12Resource* ocean{};D3D12_RESOURCE_STATES ocean_state{};ID3D12Resource* albedo{};D3D12_RESOURCE_STATES albedo_state{};
 bool split_output{}; // tests only: keep the separate output pass with sharpening off (MRT parity)
};
inline D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;
 std::array<ComPtr<ID3D12PipelineState>,4> edges_pso,weights_pso,resolve_pso;ComPtr<ID3D12PipelineState> temporal_pso,temporal_mrt_pso,output_pso;
 ComPtr<ID3D12DescriptorHeap> srv,rtv;ComPtr<ID3D12Resource> source,edges,weights,area,search,area_upload,search_upload,resolved,depth_input,ocean_input,albedo_input;std::array<ComPtr<ID3D12Resource>,2> history;
 D3D12_RESOURCE_STATES resolved_state{};std::array<D3D12_RESOURCE_STATES,2> history_state{};UINT history_frame{};gpu_timer::Timer timer;
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT area_footprint{},search_footprint{};
 UINT srv_stride{},rtv_stride{},width{},height{};DXGI_FORMAT format{};bool uploaded{},source_read{};
 HRESULT last_error=S_OK;ID3D12CommandQueue* queue_for_idle{};
 // K43: a failed (re)initialisation is retried with backoff, not every frame.
 scene_frame::Backoff retry;ID3D12Device* failed_device{};UINT64 failed_width{};UINT failed_height{};DXGI_FORMAT failed_format{};
 bool checked(HRESULT hr){last_error=hr;return SUCCEEDED(hr);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT n)const{auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(n)*srv_stride;return h;}
 D3D12_GPU_DESCRIPTOR_HANDLE gpu(UINT n)const{auto h=srv->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(n)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE output(UINT n)const{auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(n)*rtv_stride;return h;}
 bool texture(ComPtr<ID3D12Resource>& r,UINT w,UINT h,DXGI_FORMAT f,bool render){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=f;d.SampleDesc.Count=1;d.Flags=render?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_CLEAR_VALUE clear{};clear.Format=f;return checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,render?&clear:nullptr,IID_PPV_ARGS(&r)));}
 bool lookup(ComPtr<ID3D12Resource>& r,ComPtr<ID3D12Resource>& upload,D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,UINT w,UINT h,DXGI_FORMAT f,const unsigned char* data,UINT pitch){
  if(!texture(r,w,h,f,false))return false;auto desc=r->GetDesc();UINT64 size{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&size);
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(!checked(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))))return false;
  void* memory{};D3D12_RANGE empty{0,0};if(!checked(upload->Map(0,&empty,&memory)))return false;for(UINT y=0;y<h;++y)memcpy((unsigned char*)memory+footprint.Offset+SIZE_T(y)*footprint.Footprint.RowPitch,data+SIZE_T(y)*pitch,pitch);upload->Unmap(0,nullptr);return true;
 }
 void view(UINT slot,ID3D12Resource* r,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=f;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;device->CreateShaderResourceView(r,&d,cpu(slot));}
 template<size_t V,size_t P> bool pipeline(ComPtr<ID3D12PipelineState>& p,const unsigned char(&v)[V],const unsigned char(&s)[P],DXGI_FORMAT f,DXGI_FORMAT second=DXGI_FORMAT_UNKNOWN){D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={v,V};d.PS={s,P};d.BlendState.RenderTarget[0].RenderTargetWriteMask=d.BlendState.RenderTarget[1].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;d.SampleMask=UINT_MAX;d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;d.RasterizerState.DepthClipEnable=TRUE;d.DepthStencilState.DepthEnable=FALSE;d.DepthStencilState.StencilEnable=FALSE;d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=second?2:1;d.RTVFormats[0]=f;d.RTVFormats[1]=second;d.SampleDesc.Count=1;return checked(device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&p)));}
 bool initialize(ID3D12Device* dev,ID3D12CommandQueue* queue,UINT w,UINT h,DXGI_FORMAT f){
  device=dev;queue_for_idle=queue;width=w;height=h;format=f;
  D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=heap::table;ranges[0].BaseShaderRegister=0;ranges[1].NumDescriptors=heap::extras_table;ranges[1].BaseShaderRegister=3;
  D3D12_ROOT_PARAMETER parameters[3]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,root_constants};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  for(UINT i=1;i<3;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i-1]};parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  // LOW-1: no input layout (the vertex shader uses SV_VertexID only and no root arguments); only the pixel stage sees them.
  D3D12_ROOT_SIGNATURE_DESC rd{3,parameters,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};ComPtr<ID3DBlob> code,errors;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&errors))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  using namespace scene_smaa_bytecode;
  // AA-14: SMAA writes only .rg of the edges.
#define SMAA_PIPELINE(q) if(!pipeline(edges_pso[q],Vertex##q,Edges##q,DXGI_FORMAT_R8G8_UNORM)||!pipeline(weights_pso[q],Vertex##q,Weights##q,DXGI_FORMAT_R8G8B8A8_UNORM)||!pipeline(resolve_pso[q],Vertex##q,Resolve##q,f))return false
  SMAA_PIPELINE(0);SMAA_PIPELINE(1);SMAA_PIPELINE(2);SMAA_PIPELINE(3);
#undef SMAA_PIPELINE
  if(!pipeline(temporal_pso,Vertex3,Temporal3,DXGI_FORMAT_R16G16B16A16_FLOAT)||!pipeline(temporal_mrt_pso,Vertex3,TemporalMRT3,DXGI_FORMAT_R16G16B16A16_FLOAT,f)||!pipeline(output_pso,Vertex3,Output3,f))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=heap::count;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  if(!texture(source,w,h,f,false)||!texture(edges,w,h,DXGI_FORMAT_R8G8_UNORM,true)||!texture(weights,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!lookup(area,area_upload,area_footprint,AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,areaTexBytes,AREATEX_PITCH)||!lookup(search,search_upload,search_footprint,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,searchTexBytes,SEARCHTEX_PITCH))return false;
  for(UINT stage=0;stage<3;++stage){view(stage*3,stage==1?edges.Get():source.Get(),stage==1?DXGI_FORMAT_R8G8_UNORM:f);view(stage*3+1,stage==2?weights.Get():area.Get(),stage==2?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8G8_UNORM);view(stage*3+2,search.Get(),DXGI_FORMAT_R8_UNORM);}
  device->CreateRenderTargetView(edges.Get(),nullptr,output(0));device->CreateRenderTargetView(weights.Get(),nullptr,output(1));
  // Temporal/output tables (3 SRVs each): 9 and 12 = resolved, other history,
  // depth; 15, 16 and 17 start the output tables for history 0, history 1 and
  // the resolved image; 18-19 pad the last tables. Placeholders until targets()
  // and the first depth bind; 20-21 are null until an ocean or rim input binds.
  for(UINT slot:{heap::temporal_from1+2,heap::temporal_from0+2,heap::pad,heap::pad+1})view(slot,search.Get(),DXGI_FORMAT_R8_UNORM);
  view(heap::extras,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);view(heap::extras+1,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
  timer.create(device.Get(),queue);return true;
 }
 // AA-14: the resolved image (TAA or sharpening) and the two RGBA16F histories
 // (TAA) are created on first use. A failure leaves SMAA writing the back buffer.
 bool targets(bool temporal){
  if(!resolved){if(!texture(resolved,width,height,format,true)){resolved.Reset();return false;}
   view(heap::temporal_from1,resolved.Get(),format);view(heap::temporal_from0,resolved.Get(),format);view(heap::output_resolved,resolved.Get(),format);device->CreateRenderTargetView(resolved.Get(),nullptr,output(3));resolved_state=D3D12_RESOURCE_STATE_COMMON;}
  if(temporal&&!history[1]){if(!texture(history[0],width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true)||!texture(history[1],width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true)){history[0].Reset();history[1].Reset();return false;}
   view(heap::temporal_from1+1,history[1].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(heap::temporal_from0+1,history[0].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(heap::output_history0,history[0].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(heap::output_history1,history[1].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);
   device->CreateRenderTargetView(history[0].Get(),nullptr,output(4));device->CreateRenderTargetView(history[1].Get(),nullptr,output(5));history_state={D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON};history_frame=0;}
  return true;
 }
 // Descriptors are volatile: a frame still in flight may read a slot, so a
 // changed input waits for the queue once before its view is rewritten (as depth).
 bool bind(ComPtr<ID3D12Resource>& held,ID3D12Resource* r,UINT slot,DXGI_FORMAT f){if(held.Get()==r)return true;if(held&&!idle(device.Get(),queue_for_idle))return false;held=r;view(slot,r,f);return true;}
 static bool idle(ID3D12Device* d,ID3D12CommandQueue* q){if(!q)return false;ComPtr<ID3D12Fence> fence;if(FAILED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))||FAILED(q->Signal(fence.Get(),1)))return false;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;bool ok=SUCCEEDED(fence->SetEventOnCompletion(1,event))&&WaitForSingleObject(event,3000)==WAIT_OBJECT_0;CloseHandle(event);return ok;}
public:
 HRESULT error()const{return last_error;}
 bool ready()const{return source&&root;}
 bool offscreen_allocated()const{return resolved!=nullptr;}
 bool history_allocated()const{return history[0]!=nullptr&&history[1]!=nullptr;}
 // Smoothed GPU milliseconds of the whole AA pass; negative until measured.
 double gpu_milliseconds()const{return timer.milliseconds();}

 bool ensure(ID3D12Device* dev,ID3D12CommandQueue* queue,const D3D12_RESOURCE_DESC& d){
  if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.SampleDesc.Count!=1||!d.Width||!d.Height||d.Width>8192||d.Height>8192||(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return false;
  if(ready()&&device.Get()==dev&&width==d.Width&&height==d.Height&&format==d.Format)return true;
  // K43: the same request failed: retry after 1, 2, 4 .. 60 s, with no idle() in between. A new size, format or device retries now.
  uint64_t now=GetTickCount64();bool same=failed_device==dev&&failed_width==d.Width&&failed_height==d.Height&&failed_format==d.Format;
  if(same&&!retry.ready(now))return false;
  auto fail=[&](HRESULT hr){last_error=hr;if(!same)retry.reset();retry.failed(now);failed_device=dev;failed_width=d.Width;failed_height=d.Height;failed_format=d.Format;return false;};
  // K09: wait for frames in flight before their resources go, unless the device itself changed (the queue passed in is the new device's).
  if(device&&device.Get()==dev&&!idle(device.Get(),queue))return fail(E_FAIL);
  Gpu next;if(!next.initialize(dev,queue,UINT(d.Width),d.Height,d.Format))return fail(next.error());
  *this=std::move(next);return true;
 }
 // Caller restores its graphics bindings. No HUD, Present hook or CPU image readback.
 // With extra temporal or sharpening work, SMAA resolves into an offscreen
 // image and the final pass writes the scene target (with sharpening off, TAA
 // writes it directly through a second render target). Returns false only when a
 // requested depth input is unusable; SMAA output is still written.
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* back,UINT quality,bool enhanced=false,const Extra* extra=nullptr){
  timer.begin(list);bool temporal=extra&&extra->temporal,depth_ok=true;uint32_t flags=extra?extra->flags&flag_api:0;
  if(temporal){auto dd=extra->depth?extra->depth->GetDesc():D3D12_RESOURCE_DESC{};depth_ok=extra->depth&&dd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&dd.Width==width&&dd.Height==height&&dd.SampleDesc.Count==1&&(dd.Format==DXGI_FORMAT_R32_TYPELESS||dd.Format==DXGI_FORMAT_R32_FLOAT)&&!(dd.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
   if(depth_ok&&depth_input.Get()!=extra->depth){if(depth_input&&!idle(device.Get(),queue_for_idle))depth_ok=false;else{depth_input=extra->depth;view(heap::temporal_from1+2,extra->depth,DXGI_FORMAT_R32_FLOAT);view(heap::temporal_from0+2,extra->depth,DXGI_FORMAT_R32_FLOAT);}}
   temporal=depth_ok;
   // K05, AA-6: optional inputs; a missing or unusable one only clears its flag.
   if(temporal&&extra->ocean&&bind(ocean_input,extra->ocean,heap::extras,DXGI_FORMAT_R16G16B16A16_FLOAT))flags|=flag_ocean;
   if(temporal&&extra->albedo&&bind(albedo_input,extra->albedo,heap::extras+1,DXGI_FORMAT_R16G16B16A16_FLOAT))flags|=flag_rim;}
  bool offscreen=extra&&(temporal||extra->sharpen>0);
  if(offscreen&&!targets(temporal)){offscreen=temporal=false;}
  bool mrt=temporal&&extra->sharpen<=0&&!extra->split_output;
  if(!uploaded){for(auto pair:{std::pair<ID3D12Resource*,ID3D12Resource*>(area.Get(),area_upload.Get()),{search.Get(),search_upload.Get()}}){auto barrier=transition(pair.first,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&barrier);D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=pair.first;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=pair.second;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=pair.first==area.Get()?area_footprint:search_footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier=transition(pair.first,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&barrier);}uploaded=true;}
  D3D12_RESOURCE_BARRIER before[]={transition(back,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE),transition(source.Get(),source_read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};list->ResourceBarrier(2,before);list->CopyResource(source.Get(),back);D3D12_RESOURCE_BARRIER after[]={transition(back,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),transition(source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};list->ResourceBarrier(2,after);
  float metrics[root_constants]={1.f/width,1.f/height,float(width),float(height),enhanced?.45f:0,extra?extra->sharpen:0,extra?extra->history_weight:0,extra&&extra->history_valid&&history_frame?1.f:0};if(extra)std::copy(extra->constants.begin(),extra->constants.end(),metrics+8);
  memcpy(&metrics[40],&flags,4);metrics[41]=flags&flag_ocean?extra->sea_height:0;metrics[42]=offscreen?extra->dither:0;
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]={srv.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRoot32BitConstants(0,root_constants,metrics,0);list->SetGraphicsRootDescriptorTable(2,gpu(heap::extras));list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT scissor{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);quality=std::min(quality,3u);float clear[4]{};
  for(UINT stage=0;stage<3;++stage){auto target=stage==0?edges.Get():weights.Get();if(stage<2){auto barrier=transition(target,source_read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&barrier);}else if(offscreen){if(resolved_state!=D3D12_RESOURCE_STATE_RENDER_TARGET){auto barrier=transition(resolved.Get(),resolved_state,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&barrier);resolved_state=D3D12_RESOURCE_STATE_RENDER_TARGET;}}else device->CreateRenderTargetView(back,nullptr,output(2));auto handle=output(stage==2&&offscreen?3:stage);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);if(stage<2)list->ClearRenderTargetView(handle,clear,0,nullptr);list->SetPipelineState(stage==0?edges_pso[quality].Get():stage==1?weights_pso[quality].Get():resolve_pso[quality].Get());list->SetGraphicsRootDescriptorTable(1,gpu(stage*3));list->DrawInstanced(3,1,0,0);if(stage<2){auto barrier=transition(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&barrier);}}
  source_read=true;
  if(offscreen){
   auto to=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES next){if(state!=next){auto b=transition(r,state,next);list->ResourceBarrier(1,&b);state=next;}};
   to(resolved.Get(),resolved_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);UINT image=heap::output_resolved;
   if(temporal){UINT write=history_frame&1;to(history[1-write].Get(),history_state[1-write],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);to(history[write].Get(),history_state[write],D3D12_RESOURCE_STATE_RENDER_TARGET);
    // Borrowed game targets: one batched transition to pixel-shader read and one back.
    D3D12_RESOURCE_BARRIER borrow[3];UINT borrowed=0;auto lend=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES s){if(r&&s!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)borrow[borrowed++]=transition(r,s,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);};
    lend(extra->depth,extra->depth_state);if(flags&flag_ocean)lend(extra->ocean,extra->ocean_state);if(flags&flag_rim)lend(extra->albedo,extra->albedo_state);if(borrowed)list->ResourceBarrier(borrowed,borrow);
    if(mrt){device->CreateRenderTargetView(back,nullptr,output(2));D3D12_CPU_DESCRIPTOR_HANDLE handles[]={output(4+write),output(2)};list->OMSetRenderTargets(2,handles,FALSE,nullptr);list->SetPipelineState(temporal_mrt_pso.Get());}
    else{auto handle=output(4+write);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(temporal_pso.Get());}
    list->SetGraphicsRootDescriptorTable(1,gpu(write?heap::temporal_from0:heap::temporal_from1));list->DrawInstanced(3,1,0,0);
    for(UINT i=0;i<borrowed;++i)std::swap(borrow[i].Transition.StateBefore,borrow[i].Transition.StateAfter);if(borrowed)list->ResourceBarrier(borrowed,borrow);
    to(history[write].Get(),history_state[write],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);image=heap::output_history0+write;++history_frame;}
   if(!mrt){device->CreateRenderTargetView(back,nullptr,output(2));auto handle=output(2);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(output_pso.Get());list->SetGraphicsRootDescriptorTable(1,gpu(image));list->DrawInstanced(3,1,0,0);}
  }
  timer.end(list);return depth_ok;
 }
};
}
