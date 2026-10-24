#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <utility>
#include "water_bytecode.h"
#include "water_frame.h"
#include "../gpu_timer.h"
// Ocean reflection pass: copies the game's HDR target, marches reflected rays
// for ocean pixels at half resolution, then redraws only the ocean pixels with
// the upsampled reflection and the sun glint. Runs before volumetric lighting.
namespace scene_water_gpu {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> march,composite;
 // Heap: two tables of {colour copy, depth, ocean, reflection}; the march's table has a null reflection, since it renders into it.
 ComPtr<ID3D12DescriptorHeap> srv,rtv;ComPtr<ID3D12Resource> source,half;D3D12_RESOURCE_STATES source_state{D3D12_RESOURCE_STATE_COMMON};std::array<ID3D12Resource*,2> inputs{};UINT width{},height{},half_width{},half_height{},srv_stride{},rtv_stride{};HRESULT last_error{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE target_view(UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*rtv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 bool pipeline(const unsigned char* ps,size_t size,UINT8 mask,ComPtr<ID3D12PipelineState>& out){
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={water_bytecode::Vertex,sizeof(water_bytecode::Vertex)};pd.PS={ps,size};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=mask;
  pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;pd.SampleDesc.Count=1;
  return checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&out)));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,UINT w,UINT h){device=d;queue=q;width=w;height=h;half_width=(w+1)/2;half_height=(h+1)/2;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=4;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,56};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  // No blending: the composite writes the finished colour of ocean pixels and
  // discards the rest; colour only, since the target's alpha belongs to the game.
  if(!pipeline(water_bytecode::March,sizeof(water_bytecode::March),D3D12_COLOR_WRITE_ENABLE_ALL,march)||!pipeline(water_bytecode::Composite,sizeof(water_bytecode::Composite),D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE,composite))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=8;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
  if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&source))))return false;source_state=D3D12_RESOURCE_STATE_COMMON;
  td.Width=half_width;td.Height=half_height;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&half))))return false;
  for(UINT table=0;table<2;++table){view(table*4,source.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);view(table*4+3,table?half.Get():nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);}
  D3D12_RENDER_TARGET_VIEW_DESC hr{};hr.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;hr.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;device->CreateRenderTargetView(half.Get(),&hr,target_view(1));
  timer.create(device.Get(),queue.Get());return true;
 }
public:
 struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 // target: the game's HDR colour (RGBA16F, render-target state); it is copied, then its ocean pixels are redrawn.
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target){if(!d||!q||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.SampleDesc.Count!=1||target.Width<16||target.Height<16||target.Width>16384||target.Height>16384||!(target.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||target.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
  if(composite&&device.Get()==d&&queue.Get()==q&&width==target.Width&&height==target.Height)return true;if(device&&!idle())return false;Gpu next;if(!next.initialize(d,q,UINT(target.Width),target.Height)){last_error=next.error();return false;}*this=std::move(next);return true;}
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,Input depth,Input ocean,const Frame& f){
  if(!composite||!list||!target||!depth.resource||!ocean.resource||!valid(f))return false;
  auto same=[&](ID3D12Resource* r,DXGI_FORMAT a,DXGI_FORMAT b){auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==width&&d.Height==height&&d.SampleDesc.Count==1&&d.MipLevels==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==a||d.Format==b);};
  if(!same(target,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT)||!same(depth.resource,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT)||!same(ocean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
  std::array<ID3D12Resource*,2> now{depth.resource,ocean.resource};
  if(now!=inputs){if(inputs[0]&&!idle())return false;inputs=now;for(UINT table=0;table<2;++table){view(table*4+1,depth.resource,DXGI_FORMAT_R32_FLOAT);view(table*4+2,ocean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}}
  timer.begin(list);
  // Copy the scene, then read the copy while redrawing the ocean pixels of the target.
  D3D12_RESOURCE_BARRIER b[5];UINT n=0;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;bool reflections=f.view_up[3]>0;
  b[n++]=barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);if(source_state!=D3D12_RESOURCE_STATE_COPY_DEST)b[n++]=barrier(source.Get(),source_state,D3D12_RESOURCE_STATE_COPY_DEST);
  list->ResourceBarrier(n,b);list->CopyResource(source.Get(),target);
  n=0;b[n++]=barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);b[n++]=barrier(source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read);source_state=read;
  for(auto* i:{&depth,&ocean})if(i->state!=read)b[n++]=barrier(i->resource,i->state,read);if(reflections)b[n++]=barrier(half.Get(),read,D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->ResourceBarrier(n,b);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRoot32BitConstants(0,56,&f,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  auto table=[&](UINT t){auto h=srv->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(t)*4*srv_stride;return h;};
  // Half resolution march (skipped when only the glint is on).
  if(reflections){D3D12_VIEWPORT hv{0,0,float(half_width),float(half_height),0,1};D3D12_RECT hs{0,0,LONG(half_width),LONG(half_height)};list->RSSetViewports(1,&hv);list->RSSetScissorRects(1,&hs);auto handle=target_view(1);
   list->SetGraphicsRootDescriptorTable(1,table(0));list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(march.Get());list->DrawInstanced(3,1,0,0);
   b[0]=barrier(half.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,read);list->ResourceBarrier(1,b);}
  D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT sc{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);
  D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;auto handle=target_view(0);device->CreateRenderTargetView(target,&rd,handle);
  list->SetGraphicsRootDescriptorTable(1,table(1));list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(composite.Get());list->DrawInstanced(3,1,0,0);
  timer.end(list);
  n=0;for(auto* i:{&depth,&ocean})if(i->state!=read)b[n++]=barrier(i->resource,read,i->state);
  if(n)list->ResourceBarrier(n,b);return true;
 }
};
}
