#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <utility>
#include "lens_bytecode.h"
#include "lens_frame.h"
#include "../gpu_timer.h"
// Lens pass: eases a 1x1 autofocus distance, copies the game's HDR target and
// redraws it with depth of field and camera motion blur. Runs after volumetric
// lighting and the night sky, before the finishing pass.
namespace scene_lens_gpu {
using Microsoft::WRL::ComPtr;
// A borrowed game target and its recorded state (namespace scope so it can be a defaulted argument).
struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> lens,focus;
 // Heap: two tables of {colour copy, depth, previous focus, current focus, ocean, albedo}, one per focus parity.
 static constexpr UINT slots=6;
 ComPtr<ID3D12DescriptorHeap> srv,rtv;ComPtr<ID3D12Resource> source,focus_texture[2];D3D12_RESOURCE_STATES source_state{D3D12_RESOURCE_STATE_COMMON};std::array<ComPtr<ID3D12Resource>,3> inputs{};bool used{};UINT width{},height{},srv_stride{},rtv_stride{},parity{};HRESULT last_error{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE target_view(UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*rtv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 bool pipeline(const unsigned char* ps,size_t size,DXGI_FORMAT format,UINT8 mask,ComPtr<ID3D12PipelineState>& out){
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={lens_bytecode::Vertex,sizeof(lens_bytecode::Vertex)};pd.PS={ps,size};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=mask;
  pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=format;pd.SampleDesc.Count=1;
  return checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&out)));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,UINT w,UINT h){device=d;queue=q;width=w;height=h;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=slots;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,UINT(sizeof(Frame)/4)};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  // The lens writes colour only: the target's alpha belongs to the game.
  if(!pipeline(lens_bytecode::Lens,sizeof(lens_bytecode::Lens),DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE,lens)||!pipeline(lens_bytecode::Focus,sizeof(lens_bytecode::Focus),DXGI_FORMAT_R32_FLOAT,D3D12_COLOR_WRITE_ENABLE_ALL,focus))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2*slots;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
  if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&source))))return false;source_state=D3D12_RESOURCE_STATE_COMMON;
  td.Width=td.Height=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  for(auto& t:focus_texture)if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&t))))return false;
  for(UINT table=0;table<2;++table){view(table*slots,source.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(table*slots+1,nullptr,DXGI_FORMAT_R32_FLOAT);view(table*slots+2,focus_texture[table].Get(),DXGI_FORMAT_R32_FLOAT);view(table*slots+3,focus_texture[1-table].Get(),DXGI_FORMAT_R32_FLOAT);
   view(table*slots+4,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);view(table*slots+5,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);}
  D3D12_RENDER_TARGET_VIEW_DESC fd{};fd.Format=DXGI_FORMAT_R32_FLOAT;fd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;for(UINT i=0;i<2;++i)device->CreateRenderTargetView(focus_texture[i].Get(),&fd,target_view(1+i));
  timer.create(device.Get(),queue.Get());return true;
 }
public:
 using Input=scene_lens_gpu::Input;
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 // target: the game's HDR colour (RGBA16F, render-target state); it is copied, then redrawn.
 bool loaded()const{return bool(lens);}
 bool current(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target)const{return lens&&device.Get()==d&&queue.Get()==q&&width==target.Width&&height==target.Height;}
 void release(){if(device)idle();*this=Gpu{};}
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target){if(!d||!q||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.SampleDesc.Count!=1||target.Width<16||target.Height<16||target.Width>16384||target.Height>16384||!(target.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||target.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
  if(current(d,q,target))return true;if(device&&!idle()&&device.Get()==d)return false;Gpu next;if(!next.initialize(d,q,UINT(target.Width),target.Height)){last_error=next.error();return false;}*this=std::move(next);return true;}
 // ocean (gbuffer_ocean, FIN-8) and albedo (gbuffer_0 rim flag, K29) are optional; f.sea[1] says whether ocean is bound.
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,Input depth,const Frame& f,Input ocean={},Input albedo={}){
  if(!lens||!list||!target||!depth.resource||!valid(f)||(f.sea[1]>.5f)!=(ocean.resource!=nullptr))return false;
  auto same=[&](ID3D12Resource* r,DXGI_FORMAT a,DXGI_FORMAT b){auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==width&&d.Height==height&&d.SampleDesc.Count==1&&d.MipLevels==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==a||d.Format==b);};
  if(!same(target,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT)||!same(depth.resource,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT))return false;
  for(auto* i:{&ocean,&albedo})if(i->resource&&!same(i->resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
  // K09: held by reference; views rewritten only after a drain once a list may have used them.
  if(inputs[0].Get()!=depth.resource||inputs[1].Get()!=ocean.resource||inputs[2].Get()!=albedo.resource){if(used&&!idle())return false;inputs[0]=depth.resource;inputs[1]=ocean.resource;inputs[2]=albedo.resource;
   for(UINT table=0;table<2;++table){view(table*slots+1,depth.resource,DXGI_FORMAT_R32_FLOAT);view(table*slots+4,ocean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(table*slots+5,albedo.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}}
  timer.begin(list);const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  // Table `parity` reads focus_texture[parity] as last frame's and writes the other.
  UINT table=parity;ID3D12Resource* written=focus_texture[1-table].Get();
  D3D12_RESOURCE_BARRIER b[6];UINT n=0;
  b[n++]=barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);if(source_state!=D3D12_RESOURCE_STATE_COPY_DEST)b[n++]=barrier(source.Get(),source_state,D3D12_RESOURCE_STATE_COPY_DEST);
  for(auto* i:{&depth,&ocean,&albedo})if(i->resource&&i->state!=read)b[n++]=barrier(i->resource,i->state,read);b[n++]=barrier(written,read,D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->ResourceBarrier(n,b);list->CopyResource(source.Get(),target);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);auto gpu_table=srv->GetGPUDescriptorHandleForHeapStart();gpu_table.ptr+=UINT64(table)*slots*srv_stride;list->SetGraphicsRootDescriptorTable(1,gpu_table);list->SetGraphicsRoot32BitConstants(0,UINT(sizeof(Frame)/4),&f,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  // Focus: one pixel.
  D3D12_VIEWPORT one{0,0,1,1,0,1};D3D12_RECT one_rect{0,0,1,1};list->RSSetViewports(1,&one);list->RSSetScissorRects(1,&one_rect);auto focus_view=target_view(2-table);
  list->OMSetRenderTargets(1,&focus_view,FALSE,nullptr);list->SetPipelineState(focus.Get());list->DrawInstanced(3,1,0,0);
  n=0;b[n++]=barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);b[n++]=barrier(source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read);source_state=read;b[n++]=barrier(written,D3D12_RESOURCE_STATE_RENDER_TARGET,read);
  list->ResourceBarrier(n,b);
  D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT sc{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);
  D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;auto handle=target_view(0);device->CreateRenderTargetView(target,&rd,handle);
  list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(lens.Get());list->DrawInstanced(3,1,0,0);
  timer.end(list);
  n=0;for(auto* i:{&depth,&ocean,&albedo})if(i->resource&&i->state!=read)b[n++]=barrier(i->resource,read,i->state);if(n)list->ResourceBarrier(n,b);
  parity=1-parity;used=true;return true;
 }
};
}
