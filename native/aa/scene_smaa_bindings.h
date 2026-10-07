#pragma once
#include <d3d12.h>
#include <array>
#include <algorithm>
#include <cstring>
namespace scene_smaa {
// Only bindings touched by the fullscreen AA passes are restored. Vertex/index,
// blend factor and stencil reference are never changed by these passes.
struct Bindings {
 ID3D12RootSignature* root{};ID3D12PipelineState* pipeline{};std::array<ID3D12DescriptorHeap*,2> heaps{};UINT heap_count{};
 D3D12_PRIMITIVE_TOPOLOGY topology{};std::array<D3D12_VIEWPORT,16> viewports{};std::array<D3D12_RECT,16> scissors{};UINT viewport_count{},scissor_count{};
 std::array<D3D12_CPU_DESCRIPTOR_HANDLE,8> targets{};D3D12_CPU_DESCRIPTOR_HANDLE depth{};UINT target_count{};bool has_depth{},valid=true;
 struct Argument {UINT kind{},constant_count{};UINT64 value{};std::array<UINT,64> constants{};std::array<bool,64> written{};};std::array<Argument,32> arguments{};
 void signature(ID3D12RootSignature* s){if(root!=s){root=s;arguments={};}}
 void descriptor(UINT i,UINT kind,UINT64 value){if(i>=arguments.size()){valid=false;return;}arguments[i]={};arguments[i].kind=kind;arguments[i].value=value;}
 void constants(UINT i,UINT count,const void* data,UINT offset){if(i>=arguments.size()||offset+count>64){valid=false;return;}auto& a=arguments[i];if(a.kind!=5)a={};a.kind=5;a.constant_count=std::max(a.constant_count,offset+count);memcpy(a.constants.data()+offset,data,count*4);for(UINT j=offset;j<offset+count;++j)a.written[j]=true;}
 bool ready()const{return valid&&root&&pipeline&&heap_count&&topology&&viewport_count&&scissor_count&&target_count;}
 void restore(ID3D12GraphicsCommandList* list)const{
  list->SetGraphicsRootSignature(root);list->SetDescriptorHeaps(heap_count,heaps.data());list->SetPipelineState(pipeline);list->IASetPrimitiveTopology(topology);list->RSSetViewports(viewport_count,viewports.data());list->RSSetScissorRects(scissor_count,scissors.data());list->OMSetRenderTargets(target_count,targets.data(),FALSE,has_depth?&depth:nullptr);
  for(UINT i=0;i<arguments.size();++i){auto& a=arguments[i];switch(a.kind){case 1:list->SetGraphicsRootDescriptorTable(i,{a.value});break;case 2:list->SetGraphicsRootConstantBufferView(i,a.value);break;case 3:list->SetGraphicsRootShaderResourceView(i,a.value);break;case 4:list->SetGraphicsRootUnorderedAccessView(i,a.value);break;case 5:for(UINT j=0;j<a.constant_count;++j)if(a.written[j])list->SetGraphicsRoot32BitConstant(i,a.constants[j],j);break;}}
 }
};
}
