#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <utility>
#include "contact_bytecode.h"
#include "contact_frame.h"
#include "../gpu_timer.h"
// Contact shadow pass: copies the game's HDR target, then rewrites it with
// short-range sun shadows removed from the copy. Runs before volumetric lighting
// but after the ocean, so ocean pixels (gbuffer_ocean) are left alone.
namespace scene_contact_gpu {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;
 ComPtr<ID3D12DescriptorHeap> srv,rtv;ComPtr<ID3D12Resource> source;D3D12_RESOURCE_STATES source_state{D3D12_RESOURCE_STATE_COMMON};std::array<ComPtr<ID3D12Resource>,4> inputs{};bool used{};UINT width{},height{},srv_stride{};HRESULT last_error{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,UINT w,UINT h){device=d;queue=q;width=w;height=h;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=5;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,40};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={contact_bytecode::Vertex,sizeof(contact_bytecode::Vertex)};pd.PS={contact_bytecode::Contact,sizeof(contact_bytecode::Contact)};
  pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
  pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;pd.SampleDesc.Count=1;
  if(!checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso))))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=5;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
  if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&source))))return false;source_state=D3D12_RESOURCE_STATE_COMMON;view(0,source.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);
  timer.create(device.Get(),queue.Get());return true;
 }
public:
 struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 bool loaded()const{return bool(pso);}
 bool current(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target)const{return pso&&device.Get()==d&&queue.Get()==q&&width==target.Width&&height==target.Height;}
 void release(){if(device)idle();*this=Gpu{};}
 // target: the game's HDR colour (RGBA16F, render-target state); it is copied, then rewritten.
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target){if(!d||!q||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.SampleDesc.Count!=1||target.Width<16||target.Height<16||target.Width>16384||target.Height>16384||!(target.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||target.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
  if(current(d,q,target))return true;if(device&&!idle()&&device.Get()==d)return false;Gpu next;if(!next.initialize(d,q,UINT(target.Width),target.Height)){last_error=next.error();return false;}*this=std::move(next);return true;}
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,Input depth,Input normals,Input albedo,Input ocean,const Frame& f){
  if(!pso||!list||!target||!depth.resource||!normals.resource||!albedo.resource||!ocean.resource||!valid(f))return false;
  auto same=[&](ID3D12Resource* r,DXGI_FORMAT a,DXGI_FORMAT b){auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==width&&d.Height==height&&d.SampleDesc.Count==1&&d.MipLevels==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==a||d.Format==b);};
  if(!same(target,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT)||!same(depth.resource,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT))return false;
  for(auto* i:{&normals,&albedo,&ocean})if(!same(i->resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
  if(inputs[0].Get()!=depth.resource||inputs[1].Get()!=normals.resource||inputs[2].Get()!=albedo.resource||inputs[3].Get()!=ocean.resource){if(used&&!idle())return false;
   inputs[0]=depth.resource;inputs[1]=normals.resource;inputs[2]=albedo.resource;inputs[3]=ocean.resource;view(1,depth.resource,DXGI_FORMAT_R32_FLOAT);view(2,normals.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(3,albedo.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(4,ocean.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}
  timer.begin(list);
  // Copy the scene, then read the copy while writing the target.
  D3D12_RESOURCE_BARRIER b[6];UINT n=0;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  b[n++]=barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);if(source_state!=D3D12_RESOURCE_STATE_COPY_DEST)b[n++]=barrier(source.Get(),source_state,D3D12_RESOURCE_STATE_COPY_DEST);
  list->ResourceBarrier(n,b);list->CopyResource(source.Get(),target);
  n=0;b[n++]=barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);b[n++]=barrier(source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read);source_state=read;
  for(auto* i:{&depth,&normals,&albedo,&ocean})if(i->state!=read)b[n++]=barrier(i->resource,i->state,read);
  list->ResourceBarrier(n,b);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRootDescriptorTable(1,srv->GetGPUDescriptorHandleForHeapStart());list->SetGraphicsRoot32BitConstants(0,40,&f,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT sc{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);
  D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target,&rd,handle);
  list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(pso.Get());list->DrawInstanced(3,1,0,0);
  timer.end(list);
  n=0;for(auto* i:{&depth,&normals,&albedo,&ocean})if(i->state!=read)b[n++]=barrier(i->resource,read,i->state);
  if(n)list->ResourceBarrier(n,b);used=true;return true;
 }
};
}
