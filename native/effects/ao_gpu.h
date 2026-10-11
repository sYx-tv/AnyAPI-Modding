#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <utility>
#include "ao_bytecode.h"
#include "ao_frame.h"
#include "../gpu_timer.h"
// Ambient occlusion pass recorded into the game's SSAO target, right after the
// game's own SSAO and before the composite that consumes it.
namespace scene_ao_gpu {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> occlusion_pso,resolve_pso;
 ComPtr<ID3D12Resource> raw;D3D12_RESOURCE_STATES raw_state{D3D12_RESOURCE_STATE_COMMON};std::array<ID3D12Resource*,3> inputs{};
 ComPtr<ID3D12DescriptorHeap> srv,rtv;UINT width{},height{},srv_stride{},rtv_stride{};DXGI_FORMAT target_format{};HRESULT last_error{};gpu_timer::Timer timer;
 bool checked(HRESULT h){last_error=h;return SUCCEEDED(h);}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srv_stride;return h;}
 D3D12_CPU_DESCRIPTOR_HANDLE output(UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*rtv_stride;return h;}
 bool idle(){ComPtr<ID3D12Fence> f;if(!device||!queue||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||FAILED(queue->Signal(f.Get(),1)))return false;HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(1,e))&&WaitForSingleObject(e,3000)==WAIT_OBJECT_0;CloseHandle(e);return ok;}
 void view(UINT i,ID3D12Resource* resource,DXGI_FORMAT f){D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=f;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&sd,cpu(i));}
 template<size_t N> bool pipeline(ComPtr<ID3D12PipelineState>& out,const unsigned char(&ps)[N],DXGI_FORMAT f){D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={ao_bytecode::Vertex,sizeof(ao_bytecode::Vertex)};pd.PS={ps,N};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=f;pd.SampleDesc.Count=1;return checked(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&out)));}
 bool initialize(ID3D12Device* d,ID3D12CommandQueue* q,UINT w,UINT h,DXGI_FORMAT target){device=d;queue=q;width=w;height=h;target_format=target;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=4;
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,24};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,params,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> code,error;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  if(!pipeline(occlusion_pso,ao_bytecode::Occlusion,DXGI_FORMAT_R16G16_FLOAT)||!pipeline(resolve_pso,ao_bytecode::Resolve,target))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=4;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
  srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16_FLOAT;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if(!checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&raw))))return false;raw_state=D3D12_RESOURCE_STATE_COMMON;
  view(3,raw.Get(),DXGI_FORMAT_R16G16_FLOAT);device->CreateRenderTargetView(raw.Get(),nullptr,output(0));timer.create(device.Get(),queue.Get());return true;
 }
public:
 struct Input {ID3D12Resource* resource{};D3D12_RESOURCE_STATES state{};};
 HRESULT error()const{return last_error;}
 double gpu_milliseconds()const{return timer.milliseconds();}
 // target: the game's R8 SSAO target; inputs: depth (D32), normals and albedo (RGBA16F).
 bool ensure(ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& target){if(!d||!q||target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||target.SampleDesc.Count!=1||target.Width<16||target.Height<16||target.Width>8192||target.Height>8192||!(target.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)||(target.Format!=DXGI_FORMAT_R8_UNORM&&target.Format!=DXGI_FORMAT_R8_TYPELESS))return false;
  if(raw&&device.Get()==d&&queue.Get()==q&&width==target.Width&&height==target.Height)return true;if(device&&!idle())return false;Gpu next;if(!next.initialize(d,q,UINT(target.Width),target.Height,DXGI_FORMAT_R8_UNORM)){last_error=next.error();return false;}*this=std::move(next);return true;}
 bool record(ID3D12GraphicsCommandList* list,Input target,Input depth,Input normals,Input albedo,const Frame& f){
  if(!raw||!list||!target.resource||!depth.resource||!normals.resource||!albedo.resource||!valid(f))return false;
  auto same=[&](ID3D12Resource* r,DXGI_FORMAT a,DXGI_FORMAT b){auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==width&&d.Height==height&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&(d.Format==a||d.Format==b);};
  if(!same(target.resource,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8_TYPELESS)||!same(depth.resource,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT)||!same(normals.resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS)||!same(albedo.resource,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
  std::array<ID3D12Resource*,3> now{depth.resource,normals.resource,albedo.resource};
  if(now!=inputs){if(inputs[0]&&!idle())return false;inputs=now;view(0,depth.resource,DXGI_FORMAT_R32_FLOAT);view(1,normals.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);view(2,albedo.resource,DXGI_FORMAT_R16G16B16A16_FLOAT);}
  timer.begin(list);
  // Borrow the inputs as shader resources and the SSAO target as a render target.
  D3D12_RESOURCE_BARRIER begin[5];UINT n=0;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  for(auto* i:{&depth,&normals,&albedo})if(i->state!=read)begin[n++]=barrier(i->resource,i->state,read);
  if(target.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)begin[n++]=barrier(target.resource,target.state,D3D12_RESOURCE_STATE_RENDER_TARGET);
  if(raw_state!=D3D12_RESOURCE_STATE_RENDER_TARGET){begin[n++]=barrier(raw.Get(),raw_state,D3D12_RESOURCE_STATE_RENDER_TARGET);raw_state=D3D12_RESOURCE_STATE_RENDER_TARGET;}
  if(n)list->ResourceBarrier(n,begin);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRootDescriptorTable(1,srv->GetGPUDescriptorHandleForHeapStart());list->SetGraphicsRoot32BitConstants(0,24,&f,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT sc{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);
  auto handle=output(0);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(occlusion_pso.Get());list->DrawInstanced(3,1,0,0);
  auto b=barrier(raw.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,read);list->ResourceBarrier(1,&b);raw_state=read;
  D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R8_UNORM;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;device->CreateRenderTargetView(target.resource,&rd,output(1));handle=output(1);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(resolve_pso.Get());list->DrawInstanced(3,1,0,0);
  timer.end(list);
  n=0;for(auto* i:{&depth,&normals,&albedo})if(i->state!=read)begin[n++]=barrier(i->resource,read,i->state);
  if(target.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)begin[n++]=barrier(target.resource,D3D12_RESOURCE_STATE_RENDER_TARGET,target.state);
  if(n)list->ResourceBarrier(n,begin);return true;
 }
};
}
