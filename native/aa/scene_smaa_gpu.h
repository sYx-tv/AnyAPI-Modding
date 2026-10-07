#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <algorithm>
#include "scene_smaa_bytecode.h"
#include "../third_party/smaa/Textures/AreaTex.h"
#include "../third_party/smaa/Textures/SearchTex.h"
namespace scene_smaa {
using Microsoft::WRL::ComPtr;
inline D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};return b;}
class Gpu {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;
 std::array<ComPtr<ID3D12PipelineState>,4> edges_pso,weights_pso,resolve_pso;
 ComPtr<ID3D12DescriptorHeap> srv,rtv;ComPtr<ID3D12Resource> source,edges,weights,area,search,area_upload,search_upload;
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT area_footprint{},search_footprint{};
 UINT srv_stride{},rtv_stride{},width{},height{};DXGI_FORMAT format{};bool uploaded{},source_read{};
 HRESULT last_error=S_OK;
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
 template<size_t V,size_t P> bool pipeline(ComPtr<ID3D12PipelineState>& p,const unsigned char(&v)[V],const unsigned char(&s)[P],DXGI_FORMAT f){D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={v,V};d.PS={s,P};d.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;d.SampleMask=UINT_MAX;d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;d.RasterizerState.DepthClipEnable=TRUE;d.DepthStencilState.DepthEnable=FALSE;d.DepthStencilState.StencilEnable=FALSE;d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=1;d.RTVFormats[0]=f;d.SampleDesc.Count=1;return checked(device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&p)));}
 bool initialize(ID3D12Device* dev,UINT w,UINT h,DXGI_FORMAT f){
  device=dev;width=w;height=h;format=f;
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=3;range.BaseShaderRegister=0;
  D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,8};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[1].DescriptorTable={1,&range};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& s=samplers[i];s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.MaxLOD=D3D12_FLOAT32_MAX;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC rd{2,parameters,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> code,errors;if(!checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&errors))||!checked(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  using namespace scene_smaa_bytecode;
#define SMAA_PIPELINE(q) if(!pipeline(edges_pso[q],Vertex##q,Edges##q,DXGI_FORMAT_R8G8B8A8_UNORM)||!pipeline(weights_pso[q],Vertex##q,Weights##q,DXGI_FORMAT_R8G8B8A8_UNORM)||!pipeline(resolve_pso[q],Vertex##q,Resolve##q,f))return false
  SMAA_PIPELINE(0);SMAA_PIPELINE(1);SMAA_PIPELINE(2);SMAA_PIPELINE(3);
#undef SMAA_PIPELINE
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=9;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))))return false;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(!checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;srv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  if(!texture(source,w,h,f,false)||!texture(edges,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!texture(weights,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!lookup(area,area_upload,area_footprint,AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,areaTexBytes,AREATEX_PITCH)||!lookup(search,search_upload,search_footprint,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,searchTexBytes,SEARCHTEX_PITCH))return false;
  for(UINT stage=0;stage<3;++stage){view(stage*3,stage==1?edges.Get():source.Get(),stage==1?DXGI_FORMAT_R8G8B8A8_UNORM:f);view(stage*3+1,stage==2?weights.Get():area.Get(),stage==2?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8G8_UNORM);view(stage*3+2,search.Get(),DXGI_FORMAT_R8_UNORM);}
  device->CreateRenderTargetView(edges.Get(),nullptr,output(0));device->CreateRenderTargetView(weights.Get(),nullptr,output(1));return true;
 }
 static bool idle(ID3D12Device* d,ID3D12CommandQueue* q){if(!q)return false;ComPtr<ID3D12Fence> fence;if(FAILED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))||FAILED(q->Signal(fence.Get(),1)))return false;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;bool ok=SUCCEEDED(fence->SetEventOnCompletion(1,event))&&WaitForSingleObject(event,3000)==WAIT_OBJECT_0;CloseHandle(event);return ok;}
public:
 HRESULT error()const{return last_error;}
 bool ready()const{return source&&root;}
 bool ensure(ID3D12Device* dev,ID3D12CommandQueue* queue,const D3D12_RESOURCE_DESC& d){
  if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.SampleDesc.Count!=1||!d.Width||!d.Height||d.Width>8192||d.Height>8192||(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return false;
  if(ready()&&device.Get()==dev&&width==d.Width&&height==d.Height&&format==d.Format)return true;
  if(device&&!idle(device.Get(),queue))return false;
  Gpu next;if(!next.initialize(dev,UINT(d.Width),d.Height,d.Format)){last_error=next.error();return false;}*this=std::move(next);return true;
 }
 // Caller restores its graphics bindings. No HUD, Present hook or CPU image readback.
 void record(ID3D12GraphicsCommandList* list,ID3D12Resource* back,UINT quality,bool enhanced=false){
  if(!uploaded){for(auto pair:{std::pair<ID3D12Resource*,ID3D12Resource*>(area.Get(),area_upload.Get()),{search.Get(),search_upload.Get()}}){auto barrier=transition(pair.first,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&barrier);D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=pair.first;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=pair.second;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=pair.first==area.Get()?area_footprint:search_footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier=transition(pair.first,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&barrier);}uploaded=true;}
  D3D12_RESOURCE_BARRIER before[]={transition(back,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE),transition(source.Get(),source_read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};list->ResourceBarrier(2,before);list->CopyResource(source.Get(),back);D3D12_RESOURCE_BARRIER after[]={transition(back,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),transition(source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};list->ResourceBarrier(2,after);
  list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heap=srv.Get();list->SetDescriptorHeaps(1,&heap);float metrics[]={1.f/width,1.f/height,float(width),float(height),enhanced?.45f:0,0,0,0};list->SetGraphicsRoot32BitConstants(0,8,metrics,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT scissor{0,0,LONG(width),LONG(height)};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);quality=std::min(quality,3u);float clear[4]{};
  for(UINT stage=0;stage<3;++stage){auto target=stage==0?edges.Get():weights.Get();if(stage<2){auto barrier=transition(target,source_read?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&barrier);}else device->CreateRenderTargetView(back,nullptr,output(2));auto handle=output(stage);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);if(stage<2)list->ClearRenderTargetView(handle,clear,0,nullptr);list->SetPipelineState(stage==0?edges_pso[quality].Get():stage==1?weights_pso[quality].Get():resolve_pso[quality].Get());list->SetGraphicsRootDescriptorTable(1,gpu(stage*3));list->DrawInstanced(3,1,0,0);if(stage<2){auto barrier=transition(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&barrier);}}
  source_read=true;
 }
};
}
