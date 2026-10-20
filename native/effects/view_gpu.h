#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <utility>
#include "view_bytecode.h"
#include "view_frame.h"
#include "../gpu_timer.h"
// Scene view display: lights the second camera's gbuffer and draws it into a
// rectangle of the finished screen, before the HUD.
namespace scene_view_gpu {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;
 ComPtr<ID3D12DescriptorHeap> srv,rtv;std::array<ID3D12Resource*,3> inputs{};DXGI_FORMAT format{};UINT srv_stride{};HRESULT last_error{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,DXGI_FORMAT f){device=d;queue=q;format=f;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=3;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,48};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={view_bytecode::Vertex,sizeof(view_bytecode::Vertex)};pd.PS={view_bytecode::View,sizeof(view_bytecode::View)};
  pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE;
  pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=f;pd.SampleDesc.Count=1;
  if(!checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso))))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);timer.create(device.Get(),queue.Get());return true;
 }
public:
 struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 // target: the presentation buffer (RGBA8/BGRA8 UNORM, render-target state).
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target){if(!d||!q||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.SampleDesc.Count!=1||(target.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&target.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return false;
  if(pso&&device.Get()==d&&queue.Get()==q&&format==target.Format)return true;if(device&&!idle())return false;Gpu next;if(!next.initialize(d,q,target.Format)){last_error=next.error();return false;}*this=std::move(next);return true;}
 // rect: destination in pixels; the whole view is scaled into it.
 bool record(ID3D12GraphicsCommandList* list,ID3D12Resource* target,const D3D12_RECT& rect,Input depth,Input normals,Input albedo,const Frame& f){
  if(!pso||!list||!target||!depth.resource||!normals.resource||!albedo.resource||!valid(f)||rect.right<=rect.left||rect.bottom<=rect.top)return false;
  auto td=target->GetDesc();if(td.Format!=format||rect.left<0||rect.top<0||UINT64(rect.right)>td.Width||UINT(rect.bottom)>td.Height)return false;
  auto dd=depth.resource->GetDesc();auto same=[&](ID3D12Resource* r,DXGI_FORMAT a,DXGI_FORMAT b){auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==dd.Width&&d.Height==dd.Height&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==a||d.Format==b);};
  if(!same(depth.resource,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT))return false;
  for(auto* i:{&normals,&albedo})if(!same(i->resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
  std::array<ID3D12Resource*,3> now{depth.resource,normals.resource,albedo.resource};
  if(now!=inputs){if(inputs[0]&&!idle())return false;inputs=now;view(0,depth.resource,DXGI_FORMAT_R32_FLOAT);view(1,normals.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(2,albedo.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}
  timer.begin(list);
  D3D12_RESOURCE_BARRIER begin[3];UINT n=0;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  for(auto* i:{&depth,&normals,&albedo})if(i->state!=read)begin[n++]=barrier(i->resource,i->state,read);
  if(n)list->ResourceBarrier(n,begin);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRootDescriptorTable(1,srv->GetGPUDescriptorHandleForHeapStart());list->SetGraphicsRoot32BitConstants(0,48,&f,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT vp{float(rect.left),float(rect.top),float(rect.right-rect.left),float(rect.bottom-rect.top),0,1};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);
  D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target,&rd,handle);
  list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(pso.Get());list->DrawInstanced(3,1,0,0);
  timer.end(list);
  n=0;for(auto* i:{&depth,&normals,&albedo})if(i->state!=read)begin[n++]=barrier(i->resource,read,i->state);
  if(n)list->ResourceBarrier(n,begin);return true;
 }
};
}
