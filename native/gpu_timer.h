#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
// GPU time of a pass recorded on a borrowed command list. Timestamps go into an
// 8-frame ring; a slot is read back 7 frames later, long after its list has
// executed, so no fence or CPU wait is needed. The value is a smoothed estimate
// for a settings readout, not a profiler.
namespace gpu_timer {
class Timer {
 static constexpr UINT slots=8;
 Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;Microsoft::WRL::ComPtr<ID3D12Resource> readback;const uint64_t* mapped{};
 uint64_t frequency{},frame{};double smoothed{-1};bool open{};
public:
 bool create(ID3D12Device* device,ID3D12CommandQueue* queue){
  heap.Reset();readback.Reset();mapped=nullptr;frame=0;smoothed=-1;open=false;
  if(!device||!queue||FAILED(queue->GetTimestampFrequency(&frequency))||!frequency)return false;
  D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=slots*2;if(FAILED(device->CreateQueryHeap(&qd,IID_PPV_ARGS(&heap))))return false;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=slots*2*sizeof(uint64_t);d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
  void* data{};D3D12_RANGE all{0,SIZE_T(d.Width)};if(FAILED(readback->Map(0,&all,&data)))return false;mapped=(const uint64_t*)data;return true;
 }
 void begin(ID3D12GraphicsCommandList* list){if(!mapped||open)return;list->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(frame%slots)*2);open=true;}
 void end(ID3D12GraphicsCommandList* list){
  if(!mapped||!open)return;open=false;UINT slot=UINT(frame%slots);
  list->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2+1);list->ResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2,2,readback.Get(),slot*2*sizeof(uint64_t));
  ++frame;if(frame<slots)return;
  // The oldest slot, about to be reused next frame.
  UINT oldest=UINT(frame%slots);uint64_t a=mapped[oldest*2],b=mapped[oldest*2+1];
  if(b>a&&b-a<frequency){double ms=double(b-a)*1000.0/double(frequency);smoothed=smoothed<0?ms:smoothed*.9+ms*.1;}
 }
 // Milliseconds, or a negative value until enough frames were measured.
 double milliseconds()const{return smoothed;}
};
}
