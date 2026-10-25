#pragma once
#include <d3d12.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace scene_smaa {
// Only bindings touched by the fullscreen AA passes are restored. Vertex/index,
// blend factor and stencil reference are never changed by these passes.
// K23: every hooked game call stores only what changed. Arguments at or above
// `used` always have kind 0; a root signature change clears kind below it.
// Root constants reset lazily when an argument becomes kind 5.
struct Bindings {
 ID3D12RootSignature* root{};ID3D12PipelineState* pipeline{};std::array<ID3D12DescriptorHeap*,2> heaps{};UINT heap_count{};
 D3D12_PRIMITIVE_TOPOLOGY topology{};std::array<D3D12_VIEWPORT,16> viewports{};std::array<D3D12_RECT,16> scissors{};UINT viewport_count{},scissor_count{};
 std::array<D3D12_CPU_DESCRIPTOR_HANDLE,8> targets{};D3D12_CPU_DESCRIPTOR_HANDLE depth{};UINT target_count{};bool has_depth{},valid=true;
 struct Argument {UINT kind{},constant_count{};UINT64 value{};std::array<UINT,64> constants{};uint64_t written{};};std::array<Argument,32> arguments{};UINT used{};
 // Start of a frame: forget everything without zeroing 11 KB.
 void reset(){root=nullptr;pipeline=nullptr;heap_count=viewport_count=scissor_count=target_count=0;topology=D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;has_depth=false;valid=true;clear_arguments();}
 void clear_arguments(){for(UINT i=0;i<used;++i)arguments[i].kind=0;used=0;}
 void signature(ID3D12RootSignature* s){if(root!=s){root=s;clear_arguments();}}
 void descriptor(UINT i,UINT kind,UINT64 value){if(i>=arguments.size()){valid=false;return;}auto& a=arguments[i];a.kind=kind;a.value=value;used=std::max(used,i+1);}
 void constants(UINT i,UINT count,const void* data,UINT offset){if(i>=arguments.size()||offset+count>64){valid=false;return;}auto& a=arguments[i];if(a.kind!=5){a.kind=5;a.constant_count=0;a.written=0;}a.constant_count=std::max(a.constant_count,offset+count);memcpy(a.constants.data()+offset,data,count*4);a.written|=(count>=64?~0ull:(1ull<<count)-1)<<offset;used=std::max(used,i+1);}
 bool ready()const{return valid&&root&&pipeline&&heap_count&&topology&&viewport_count&&scissor_count&&target_count;}
 void restore(ID3D12GraphicsCommandList* list)const{
  list->SetGraphicsRootSignature(root);list->SetDescriptorHeaps(heap_count,heaps.data());list->SetPipelineState(pipeline);list->IASetPrimitiveTopology(topology);list->RSSetViewports(viewport_count,viewports.data());list->RSSetScissorRects(scissor_count,scissors.data());list->OMSetRenderTargets(target_count,targets.data(),FALSE,has_depth?&depth:nullptr);
  for(UINT i=0;i<used;++i){auto& a=arguments[i];switch(a.kind){case 1:list->SetGraphicsRootDescriptorTable(i,{a.value});break;case 2:list->SetGraphicsRootConstantBufferView(i,a.value);break;case 3:list->SetGraphicsRootShaderResourceView(i,a.value);break;case 4:list->SetGraphicsRootUnorderedAccessView(i,a.value);break;case 5:for(UINT j=0;j<a.constant_count;++j)if(a.written>>j&1)list->SetGraphicsRoot32BitConstant(i,a.constants[j],j);break;}}
 }
};
// K23: bindings of the last 4 command lists the game recorded this frame (the
// oldest is replaced when a fifth appears), with the last hit cached per thread.
// Only the thread that ran scene_antialiasing::begin() tracks (tracking is
// thread_local) and begin() clears it. find()/end()/->second keep the shape of
// the std::unordered_map it replaces, so the other passes compile unchanged.
struct TrackedList {ID3D12GraphicsCommandList* first{};Bindings second;};
struct ListStates {
 std::array<TrackedList,4> slots;uint32_t count{},next{},peak{};
 static inline thread_local TrackedList* last_hit{};
 TrackedList* end()const{return nullptr;}
 TrackedList* find(ID3D12GraphicsCommandList* l){if(!l)return nullptr;if(last_hit>=slots.data()&&last_hit<slots.data()+count&&last_hit->first==l)return last_hit;for(uint32_t i=0;i<count;++i)if(slots[i].first==l)return last_hit=&slots[i];return nullptr;}
 TrackedList* insert(ID3D12GraphicsCommandList* l){auto& s=count<slots.size()?slots[count++]:slots[next++%slots.size()];s.first=l;s.second.reset();peak=std::max(peak,count);return last_hit=&s;}
 void clear(){for(uint32_t i=0;i<count;++i)slots[i].first=nullptr;count=next=0;last_hit=nullptr;}
 size_t size()const{return count;}
};
}
