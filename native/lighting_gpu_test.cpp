#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "lighting/volumetric_gpu.h"
#include "aa/scene_smaa_bindings.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
int main(int argc,char** argv){std::cout<<std::unitbuf;
 // K39: the shared WARP device (debug layer, GPU-based validation where the Graphics Tools are installed).
 // --no-gbv keeps the debug layer and the InfoQueue check but turns GPU-based validation off, for a comparison run.
 const bool gbv=!(argc>1&&std::strcmp(argv[1],"--no-gbv")==0);
 gpu_test::Device dev;assert(gpu_test::create(dev,gbv));ComPtr<ID3D12Device> device=dev.device;std::cout<<"Device validated="<<dev.validated<<" GPU-based validation requested="<<(dev.validated&&gbv)<<std::endl;
 ComPtr<ID3D12CommandQueue> queue=dev.queue;ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](UINT w,UINT h,DXGI_FORMAT format){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=w;t.desc.Height=h;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=format==DXGI_FORMAT_R16G16B16A16_FLOAT?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 // Restore a deliberately different native HUD root/pipeline and clipped viewport.
 const char* shader="cbuffer Color:register(b0){float4 color;}float4 V(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}float4 P():SV_Target{return color;}";ComPtr<ID3DBlob> vs,ps,code,error;assert(SUCCEEDED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"V","vs_5_0",0,0,&vs,&error)));assert(SUCCEEDED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"P","ps_5_0",0,0,&ps,&error)));D3D12_ROOT_PARAMETER arg{};arg.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;arg.Constants={0,0,4};arg.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;D3D12_ROOT_SIGNATURE_DESC rd{1,&arg,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};assert(SUCCEEDED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error)));ComPtr<ID3D12RootSignature> hud_root;assert(SUCCEEDED(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&hud_root))));D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=hud_root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;pd.SampleDesc.Count=1;ComPtr<ID3D12PipelineState> hud;assert(SUCCEEDED(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&hud))));D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> rtv;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> srv;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv))));
 volumetric::Gpu gpu;unsigned cases{};
 auto run=[&](float shadow_value,float distance,float density,float fog,UINT steps,UINT w=64,UINT h=48,bool reversed=false,bool shadow16=false,bool farther=false,bool native_light=false,float intensity=1,float focus=.35f,unsigned local_mode=0,float sun_strength=4,float clarity=0,bool temporal=false,float edge=0){wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));auto color=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT),depth=texture(w,h,DXGI_FORMAT_R32_FLOAT),shadow=texture(32,32,shadow16?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R32_FLOAT);auto far_shadow=texture(32,32,shadow16?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R32_FLOAT);assert(gpu.ensure(device.Get(),queue.Get(),color.desc));gpu.request_probe();
  for(auto item:{&color,&depth,&shadow,&far_shadow}){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(item->upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<item->desc.Height;++y)for(UINT x=0;x<item->desc.Width;++x){auto p=data+item->footprint.Offset+y*item->footprint.Footprint.RowPitch;if(item==&color){auto half=(DirectX::PackedVector::HALF*)p+x*4;for(UINT c=0;c<3;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(.05f);half[3]=DirectX::PackedVector::XMConvertFloatToHalf(1);}else if((item==&shadow||item==&far_shadow)&&shadow16)((uint16_t*)p)[x]=uint16_t(std::clamp(item==&shadow&&farther?(reversed?0.f:1.f):shadow_value,0.f,1.f)*65535.f);else ((float*)p)[x]=(item==&shadow||item==&far_shadow)?(item==&shadow&&farther?(reversed?0.f:1.f):shadow_value):reversed?.1f/distance:1-.1f/distance;}item->upload->Unmap(0,nullptr);copy(*item,true);auto b=volumetric::barrier(item->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,item==&color?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}
  volumetric::Frame f;f.sun={0,0,1,reversed?2.f:1.f};if(reversed)f.projection={0,.1f,1,0};f.medium={density,0,0,.4f};f.controls={float(steps),100,sun_strength,fog};f.shadow[0]=f.shadow[5]=.01f;f.shadow[10]=reversed?-.001f:.001f;f.shadow[14]=reversed?1.f:0.f;f.shadow[15]=1;if(native_light){f.sun={.462083f,0,.886829f,reversed?2.f:1.f};f.sun_color={.305571f,.280569f,.245261f,0};f.medium[3]=focus;f.controls[2]=3*intensity;}f.temporal[1][0]=clarity;if(temporal){f.temporal[1][3]=1;f.temporal[1][5]=.9f;}if(farther){f.additional_shadows[0]=f.shadow;f.shadow[12]=10;f.forward[3]=2;}f.shadow[12]+=edge;
  if(local_mode){f.controls[2]=f.controls[3]=0;f.medium[3]=0;f.sun_color[3]=1;f.fog_color[3]=1;auto& light=f.lights[0];light={0,0,8,6,0,0,-1,.9f,1,1,1,local_mode==1?0.f:1.f,-1,2,1,0};if(local_mode==3)light[4]=1,light[6]=0;if(local_mode==4){light[12]=8;f.local_shadow_count=1;f.local_shadows[0]=f.shadow;}}
  scene_smaa::Bindings saved;saved.signature(hud_root.Get());saved.pipeline=hud.Get();saved.heap_count=1;saved.heaps[0]=srv.Get();saved.topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;saved.viewport_count=saved.scissor_count=saved.target_count=1;saved.viewports[0]={0,0,8,8,0,1};saved.scissors[0]={0,0,4,8};device->CreateRenderTargetView(color.resource.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());saved.targets[0]=rtv->GetCPUDescriptorHandleForHeapStart();float green[]={0,1,0,1};saved.constants(0,4,green,0);assert(saved.ready());std::array<ID3D12Resource*,12> shadows{};shadows.fill(shadow.resource.Get());if(farther)shadows[1]=far_shadow.resource.Get();std::array<D3D12_RESOURCE_STATES,12> states{};states.fill(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);assert(gpu.record(list.Get(),color.resource.Get(),depth.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,shadows,states,f));saved.restore(list.Get());list->DrawInstanced(3,1,0,0);
  auto b=volumetric::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);copy(color,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};volumetric::Gpu::ProbeStats premature;assert(!gpu.probe(premature));queue->ExecuteCommandLists(1,lists);gpu.submitted(queue.Get(),1,lists);wait();volumetric::Gpu::ProbeStats probe;bool probe_ready=gpu.probe(probe);std::cout<<"Probe ready="<<probe_ready<<" coverage="<<probe.coverage<<" lit="<<probe.lit_average<<" distance="<<probe.distance<<std::endl;assert(probe_ready&&(edge!=0||probe.coverage>.95f));bool illuminated=reversed?shadow_value<.5f:shadow_value>.5f;assert(illuminated?probe.lit_average>.95f:probe.lit_average<.05f);assert(!gpu.probe(probe));unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(color.bytes)};assert(SUCCEEDED(color.readback->Map(0,&range,(void**)&pixels)));auto at=[&](UINT x,UINT y,UINT c){auto p=(const DirectX::PackedVector::HALF*)(pixels+color.footprint.Offset+y*color.footprint.Footprint.RowPitch);return DirectX::PackedVector::XMConvertHalfToFloat(p[x*4+c]);};assert(at(2,2,0)==0&&at(2,2,1)==1&&at(2,2,2)==0);assert(at(5,2,1)!=1);float result=at(w/2,h/2,0);assert(std::isfinite(result)&&result>=0);assert(at(w/2,h/2,3)==1);color.readback->Unmap(0,nullptr);++cases;return result;};
 for(UINT steps:{16u,24u,32u,48u}){float lit=run(1,100,.02f,0,steps),blocked=run(0,100,.02f,0,steps);assert(lit>blocked+.05f);std::cout<<"Occluded shafts steps="<<steps<<" lit="<<lit<<" blocked="<<blocked<<'\n';}
 for(UINT steps:{16u,24u,32u,48u}){float lit=run(0,100,.02f,0,steps,64,48,true),blocked=run(1,100,.02f,0,steps,64,48,true);assert(lit>blocked+.05f);std::cout<<"Reversed-depth shafts steps="<<steps<<" lit="<<lit<<" blocked="<<blocked<<'\n';}
 for(UINT steps:{16u,24u,32u,48u}){float lit=run(0,100,.02f,0,steps,64,48,true,true),blocked=run(1,100,.02f,0,steps,64,48,true,true);assert(lit>blocked+.05f);std::cout<<"Native R16 shadow shafts steps="<<steps<<" lit="<<lit<<" blocked="<<blocked<<'\n';}
 // The near cascade deliberately does not cover the view. The second must
 // supply occlusion instead of treating all farther samples as fully lit.
 for(UINT steps:{16u,24u,32u,48u}){float lit=run(0,100,.02f,0,steps,64,48,true,true,true),blocked=run(1,100,.02f,0,steps,64,48,true,true,true);assert(lit>blocked+.05f);std::cout<<"Farther cascade shafts steps="<<steps<<" lit="<<lit<<" blocked="<<blocked<<'\n';}
 // Regression at measured game sun RGB, low fog, and the captured sun angle:
 // raw phase normalization used to leave this contrast almost invisible.
 for(UINT steps:{16u,24u,32u,48u}){float lit=run(0,30,.001f,1,steps,64,48,true,true,true,true),blocked=run(1,30,.001f,1,steps,64,48,true,true,true,true);assert(lit>blocked+.04f);std::cout<<"Measured-light shafts steps="<<steps<<" lit="<<lit<<" blocked="<<blocked<<'\n';}
 float zero_beam=run(0,30,.001f,1,48,64,48,true,true,true,true,0),normal_beam=run(0,30,.001f,1,48,64,48,true,true,true,true,1),double_beam=run(0,30,.001f,1,48,64,48,true,true,true,true,2),max_beam=run(0,30,.001f,1,48,64,48,true,true,true,true,4);
 assert(std::abs((double_beam-zero_beam)-2*(normal_beam-zero_beam))<.002f);assert(std::abs((max_beam-zero_beam)-4*(normal_beam-zero_beam))<.002f);
 float focused_beam=run(0,30,.001f,1,48,64,48,true,true,true,true,1,.7f);assert(std::isfinite(focused_beam)&&focused_beam>zero_beam);
 float point_light=run(0,30,.02f,0,48,64,48,true,true,false,false,1,.35f,1),spot_light=run(0,30,.02f,0,48,64,48,true,true,false,false,1,.35f,2),outside_cone=run(0,30,.02f,0,48,64,48,true,true,false,false,1,.35f,3);
 std::cout<<"Local point="<<point_light<<" spot="<<spot_light<<" outside="<<outside_cone<<std::endl;assert(point_light>.0555f&&spot_light>outside_cone+.001f&&std::abs(outside_cone-.05f)<.001f);
 float local_lit=run(0,30,.02f,0,48,64,48,true,true,false,false,1,.35f,4),local_blocked=run(1,30,.02f,0,48,64,48,true,true,false,false,1,.35f,4);assert(local_lit>local_blocked+.001f&&std::abs(local_blocked-.05f)<.001f);
 // Lamp beams use clear air (0.006/m) with a soft roll-off, so they are faint
 // but present here; the thresholds only check they light the air at all.
 // Clear-air beams must not dim shadowed scenery or inherit fog density.
 for(bool reversed:{false,true}){
  float blocked=run(reversed?1.f:0.f,100,.008f,0,48,64,48,reversed);
  assert(std::abs(blocked-.05f)<.001f);
  float thin=run(reversed?0.f:1.f,100,.001f,0,48,64,48,reversed);
  float thick=run(reversed?0.f:1.f,100,.008f,0,48,64,48,reversed);
  assert(thin>blocked+.05f&&std::abs(thin-thick)<.001f);
 }
 float nearby=run(1,2,.02f,1,24,64,48,false,false,false,false,1,.35f,0,0);
 float distant=run(1,100,.02f,1,24,64,48,false,false,false,false,1,.35f,0,0);
 assert(distant>nearby+.15f);
 assert(std::abs(run(1,100,0,1,24,64,48,false,false,false,false,1,.35f,0,0)-.05f)<.001f);
 assert(std::abs(run(1,100,0,1,24,96,64,false,false,false,false,1,.35f,0,0)-.05f)<.001f);

 // Clarity thins rays that are lit end to end (the old fog look) but never
 // brightens shadowed scenery. Temporal smoothing without history stays close
 // to the single-frame result.
 for(bool reversed:{false,true}){
  float lit=reversed?0.f:1.f,dark=reversed?1.f:0.f;
  float classic=run(lit,100,.002f,0,48,64,48,reversed,false,false,false,1,.35f,0,4,0),clear=run(lit,100,.002f,0,48,64,48,reversed,false,false,false,1,.35f,0,4,1),light=run(lit,100,.002f,0,48,64,48,reversed,false,false,false,1,.35f,0,4,.85f);
  std::cout<<"Clarity classic="<<classic<<" light="<<light<<" clear="<<clear<<std::endl;assert(clear<light&&light<classic&&std::abs(clear-.05f)<.01f);
  assert(std::abs(run(dark,100,.002f,0,48,64,48,reversed,false,false,false,1,.35f,0,4,1)-.05f)<.001f);
  // V3 lights the fog: sunlit fog is brighter than fog in shadow; V2 fog ignores shadow.
  {float sunny=run(lit,100,.01f,1,48,64,48,reversed,false,false,false,1,.35f,0,0,1),shaded=run(dark,100,.01f,1,48,64,48,reversed,false,false,false,1,.35f,0,0,1),flat_lit=run(lit,100,.01f,1,48,64,48,reversed,false,false,false,1,.35f,0,0,0),flat_dark=run(dark,100,.01f,1,48,64,48,reversed,false,false,false,1,.35f,0,0,0);
   std::cout<<"Fog light sunny="<<sunny<<" shaded="<<shaded<<std::endl;assert(sunny>shaded+.01f&&std::abs(flat_lit-flat_dark)<.002f);}
  float single=run(lit,100,.002f,0,48,64,48,reversed),smoothed=run(lit,100,.002f,0,48,64,48,reversed,false,false,false,1,.35f,0,4,0,true);
  std::cout<<"Temporal single="<<single<<" smoothed="<<smoothed<<std::endl;assert(std::isfinite(smoothed)&&std::abs(single-smoothed)<.02f);
 }
 // Air at the last cascade's outer edge has no shadow information past it: beams fade out
 // there instead of counting the air as fully lit (the lit wall where cascades end).
 {float lit=run(1,100,.02f,0,48),blocked=run(0,100,.02f,0,48),edge=run(1,100,.02f,0,48,64,48,false,false,false,false,1,.35f,0,4,0,false,-.98f);
  std::cout<<"Cascade edge lit="<<lit<<" edge="<<edge<<" blocked="<<blocked<<std::endl;assert(edge<blocked+.1f*(lit-blocked));}
 // ---- AnyAPI 0.40.2 (package 6) ----
 using DirectX::PackedVector::HALF;struct Shot{std::vector<HALF> texels;float centre[4]{};};
 auto same=[](const Shot& a,const Shot& b){return a.texels==b.texels;};
 // One frame on fresh inputs: colour .05 (alpha as given), depth at `distance`, every shadow map `shadow_value`,
 // gbuffer_ocean (0,1,0,0) when `ocean`. report=false leaves out Gpu::submitted (the ring then cannot see the list).
 auto shot=[&](const volumetric::Frame& f,UINT path,float shadow_value,float distance,bool ocean=false,float alpha=1,bool report=true){const UINT w=64,h=48;
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto color=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT),depth=texture(w,h,DXGI_FORMAT_R32_FLOAT),shadow=texture(32,32,DXGI_FORMAT_R32_FLOAT),water=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
  bool ready=gpu.ensure(device.Get(),queue.Get(),color.desc,path);if(!ready)std::cout<<"ensure failed path="<<path<<" hr=0x"<<std::hex<<unsigned(gpu.error())<<std::dec<<(SUCCEEDED(gpu.error())?" (no failed call recorded: the idle wait before re-creation failed)":"")<<std::endl;assert(ready);
  for(auto item:{&color,&depth,&shadow,&water}){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(item->upload->Map(0,&none,(void**)&data)));
   for(UINT y=0;y<item->desc.Height;++y)for(UINT x=0;x<item->desc.Width;++x){auto p=data+item->footprint.Offset+y*item->footprint.Footprint.RowPitch;
    if(item==&color||item==&water){auto half=(HALF*)p+x*4;float v[4]{.05f,.05f,.05f,alpha};if(item==&water){v[0]=v[2]=v[3]=0;v[1]=1;}for(UINT c=0;c<4;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(v[c]);}
    else ((float*)p)[x]=item==&shadow?shadow_value:1-.1f/distance;}
   item->upload->Unmap(0,nullptr);copy(*item,true);auto b=volumetric::barrier(item->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,item==&color?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}
  std::array<ID3D12Resource*,12> shadows{};shadows.fill(shadow.resource.Get());std::array<D3D12_RESOURCE_STATES,12> states{};states.fill(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  volumetric::Ocean o;if(ocean)o.resource=water.resource.Get();
  assert(gpu.record(list.Get(),color.resource.Get(),depth.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,shadows,states,f,o));
  auto b=volumetric::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);copy(color,false);assert(SUCCEEDED(list->Close()));
  ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);if(report)gpu.submitted(queue.Get(),1,lists);wait();
  Shot out;unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(color.bytes)};assert(SUCCEEDED(color.readback->Map(0,&range,(void**)&pixels)));
  for(UINT y=0;y<h;++y){auto row=(const HALF*)(pixels+color.footprint.Offset+y*color.footprint.Footprint.RowPitch);out.texels.insert(out.texels.end(),row,row+w*4);}
  for(UINT c=0;c<4;++c)out.centre[c]=DirectX::PackedVector::XMConvertHalfToFloat(out.texels[((h/2)*w+w/2)*4+c]);color.readback->Unmap(0,nullptr);
  for(float v:out.centre)assert(std::isfinite(v));++cases;return out;};
 // Sun from behind the camera, fog `fog` with extinction `density`, `lamps` lights in front (odd ones spots).
 auto scene=[](float density,float fog,float sun,UINT lamps,bool temporal=false){volumetric::Frame f;f.sun={0,0,1,1};f.medium={density,0,0,.4f};f.controls={32,100,sun,fog};f.shadow[0]=f.shadow[5]=.01f;f.shadow[10]=.001f;f.shadow[15]=1;
  if(lamps){f.sun_color[3]=1;f.fog_color[3]=float(lamps);for(UINT i=0;i<lamps;++i)f.lights[i]={(i&1?.75f:-.75f)*float(i),0,8+float(i),6,0,0,-1,.9f,.5f+.1f*float(i),1,1,i&1?1.f:0.f,-1,2,1,0};}
  if(temporal){f.temporal[1][3]=1;f.temporal[1][5]=.9f;}return f;};
 // K39/WI-1: the AMD constants path at every lamp count. LAMP_SLOTS 0/2/4 permutations are bit-identical to the
 // 8-slot build, the branch build matches it, and both stay within fp16 noise of the marched lamps.
 {float previous=0;for(UINT n:{0u,1u,2u,3u,4u,5u,8u}){auto f=scene(.002f,0,0,n);auto marched=shot(f,volumetric::PATH_MARCHED,1,30),fast=shot(f,volumetric::PATH_CONSTANTS,1,30);gpu.use_permutations(false);auto eight=shot(f,volumetric::PATH_CONSTANTS,1,30);auto branch=shot(f,volumetric::PATH_BRANCH,1,30);
  std::cout<<"Constants lamps="<<n<<" marched="<<marched.centre[0]<<" permutation="<<fast.centre[0]<<" eight="<<eight.centre[0]<<" branch="<<branch.centre[0]<<std::endl;
  if(!same(fast,eight)){size_t differ=0;for(size_t i=0;i<fast.texels.size();++i)differ+=fast.texels[i]!=eight.texels[i];std::cout<<"Constants lamps="<<n<<" permutation differs from eight in "<<differ<<" of "<<fast.texels.size()<<" channels"<<std::endl;}
  assert(same(fast,eight)&&std::abs(branch.centre[0]-eight.centre[0])<1e-4f&&std::abs(fast.centre[0]-marched.centre[0])<2e-3f&&fast.centre[0]>=previous);previous=fast.centre[0];if(n)assert(fast.centre[0]>.0505f);}}
 // K39: two temporal frames. The second blends the history the first wrote; a static view stays put.
 {auto f1=scene(.004f,1,4,2,true);f1.temporal[1][4]=1;auto a=shot(f1,volumetric::PATH_MARCHED,1,100);auto f2=f1;f2.temporal[1][4]=0;f2.temporal[0]={0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,1};auto b=shot(f2,volumetric::PATH_MARCHED,1,100);
  std::cout<<"Temporal first="<<a.centre[0]<<" second="<<b.centre[0]<<std::endl;assert(std::abs(a.centre[0]-b.centre[0])<.01f&&b.centre[3]==1);
  auto f3=f2;f3.temporal[1][4]=1;f3.temporal[0][15]=2;auto dark=shot(f3,volumetric::PATH_MARCHED,0,100),fresh=shot(scene(.004f,1,4,2),volumetric::PATH_MARCHED,0,100);assert(std::abs(dark.centre[0]-fresh.centre[0])<.02f);}
 // K08: new inputs every frame go through the descriptor ring: no idle(), whatever the GPU is doing.
 {auto f=scene(.002f,1,1,1);shot(f,volumetric::PATH_MARCHED,1,30);UINT idle=gpu.idle_count(),waits=gpu.ring_wait_count();for(int i=0;i<20;++i)shot(f,volumetric::PATH_MARCHED,i&1?1.f:0.f,30.f+i);assert(gpu.idle_count()==idle);
  ComPtr<ID3D12CommandAllocator> al[4];ComPtr<ID3D12GraphicsCommandList> gl[4];UINT64 done[4]{};for(int k=0;k<4;++k){assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&al[k]))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,al[k].Get(),nullptr,IID_PPV_ARGS(&gl[k]))));assert(SUCCEEDED(gl[k]->Close()));}
  auto color=texture(64,48,DXGI_FORMAT_R16G16B16A16_FLOAT),sm=texture(32,32,DXGI_FORMAT_R32_FLOAT);Texture depths[2]{texture(64,48,DXGI_FORMAT_R32_FLOAT),texture(64,48,DXGI_FORMAT_R32_FLOAT)};
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));for(auto t:{&color,&sm,&depths[0],&depths[1]}){auto b=volumetric::barrier(t->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,t==&color?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);}
  const float black[4]{0,0,0,1};device->CreateRenderTargetView(color.resource.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(),black,0,nullptr);assert(SUCCEEDED(list->Close()));{ID3D12CommandList* l[]={list.Get()};queue->ExecuteCommandLists(1,l);}wait();
  std::array<ID3D12Resource*,12> shadows{};shadows.fill(sm.resource.Get());std::array<D3D12_RESOURCE_STATES,12> states{};states.fill(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  for(int i=0;i<24;++i){int k=i%4;if(done[k]&&fence->GetCompletedValue()<done[k]){assert(SUCCEEDED(fence->SetEventOnCompletion(done[k],event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);}
   assert(SUCCEEDED(al[k]->Reset()));assert(SUCCEEDED(gl[k]->Reset(al[k].Get(),nullptr)));assert(gpu.record(gl[k].Get(),color.resource.Get(),depths[i&1].resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,shadows,states,f));assert(SUCCEEDED(gl[k]->Close()));
   ID3D12CommandList* l[]={gl[k].Get()};queue->ExecuteCommandLists(1,l);gpu.submitted(queue.Get(),1,l);assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));done[k]=serial;}
  wait();std::cout<<"Descriptor ring frames=44 idle_calls="<<gpu.idle_count()-idle<<" ring_waits="<<gpu.ring_wait_count()-waits<<std::endl;assert(gpu.idle_count()==idle);
  // A list the ring never saw submitted: the fifth change falls back to one idle(), once.
  UINT unconfirmed=gpu.ring_unconfirmed_count();for(int i=0;i<5;++i)shot(f,volumetric::PATH_MARCHED,1,40.f+i,false,1,false);assert(gpu.ring_unconfirmed_count()==unconfirmed+1&&gpu.idle_count()==idle+1);shot(f,volumetric::PATH_MARCHED,1,30);}
 // K05/WI-12: the sea plane ends the ray on ocean pixels (the ocean writes no depth). The 1x1 zero texture and
 // lamp-only mode without lamps change nothing, bit for bit; mode 1 cuts lamp light past the sea surface.
 {auto f=scene(.02f,1,0,0);f.camera={0,0,0,10};f.forward={0,-.70710678f,.70710678f,1};f.up={0,.70710678f,.70710678f,1};
  auto off=shot(f,volumetric::PATH_MARCHED,1,100,true);auto g=f;g.temporal[1][8]=2;auto clamped=shot(g,volumetric::PATH_MARCHED,1,100,true),dummy=shot(g,volumetric::PATH_MARCHED,1,100,false);auto m1=f;m1.temporal[1][8]=1;auto lamps_only=shot(m1,volumetric::PATH_MARCHED,1,100,true);
  std::cout<<"Sea plane off="<<off.centre[0]<<" clamped="<<clamped.centre[0]<<std::endl;assert(same(off,dummy)&&same(off,lamps_only)&&clamped.centre[0]<off.centre[0]-.05f);
  // A lamp under the sea surface (its whole reach past the sea hit of the centre ray): mode 1 removes it exactly.
  auto lit=scene(.002f,0,0,1);lit.camera=f.camera;lit.forward=f.forward;lit.up=f.up;lit.lights[0]={0,-20,20,6,0,0,-1,.9f,1,1,1,0,-1,2,1,0};auto unlit=lit;unlit.fog_color[3]=0;unlit.sun_color[3]=0;
  auto beyond=shot(lit,volumetric::PATH_MARCHED,1,100,true),none=shot(unlit,volumetric::PATH_MARCHED,1,100,true);lit.temporal[1][8]=1;auto cut=shot(lit,volumetric::PATH_MARCHED,1,100,true);
  std::cout<<"Sea lamp beyond="<<beyond.centre[0]<<" cut="<<cut.centre[0]<<" none="<<none.centre[0]<<std::endl;assert(beyond.centre[0]>none.centre[0]+1e-3f&&cut.centre[0]==none.centre[0]);}
 // ANY_ATMOS_FLASH_GLOW: lightning lights the fogged part of the view by glow*(1-transmittance); clear air gets none.
 {auto f=scene(.01f,1,0,0);auto plain=shot(f,volumetric::PATH_MARCHED,1,100);auto g=f;g.temporal[1][12]=g.temporal[1][13]=g.temporal[1][14]=1;auto glow=shot(g,volumetric::PATH_MARCHED,1,100);
  std::cout<<"Flash glow plain="<<plain.centre[0]<<" glow="<<glow.centre[0]<<std::endl;assert(std::abs((glow.centre[0]-plain.centre[0])-(1-std::exp(-1.f)))<.01f);
  auto clear=scene(0,1,0,0);auto c0=shot(clear,volumetric::PATH_MARCHED,1,100);clear.temporal[1][12]=clear.temporal[1][13]=clear.temporal[1][14]=1;assert(same(c0,shot(clear,volumetric::PATH_MARCHED,1,100)));}
 // K07: the blend composite never writes alpha.
 {auto s=shot(scene(.01f,1,4,2),volumetric::PATH_MARCHED,1,100,false,.25f);assert(s.centre[3]==.25f);}
 // K25: skipping a frame where nothing contributes (forget_history, as record() does) gives the same next frame,
 // bit for bit, as drawing it; and the drawn nothing-frame leaves the target exactly as it was.
 {auto rows=[](volumetric::Frame f,UINT index,bool previous){f.temporal[1][4]=index&1?0.f:1.f;if(previous)f.temporal[0]={0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,0};f.temporal[0][15]=float(index);return f;};
  auto content=scene(.01f,1,0,1,true),nothing=scene(0,0,0,0,true);
  auto a1=shot(rows(content,0,false),volumetric::PATH_MARCHED,1,100),a2=shot(rows(nothing,1,true),volumetric::PATH_MARCHED,1,100),a3=shot(rows(content,2,true),volumetric::PATH_MARCHED,1,100);
  auto b1=shot(rows(content,0,false),volumetric::PATH_MARCHED,1,100);gpu.forget_history();auto b3=shot(rows(content,2,true),volumetric::PATH_MARCHED,1,100);
  HALF input[4];for(int c=0;c<4;++c)input[c]=DirectX::PackedVector::XMConvertFloatToHalf(c<3?.05f:1.f);for(size_t i=0;i<a2.texels.size();++i)assert(a2.texels[i]==input[i%4]);
  std::cout<<"K25 drawn="<<a3.centre[0]<<" skipped="<<b3.centre[0]<<std::endl;assert(same(a1,b1)&&same(a3,b3));}
 // K10/K25: a frame with no sun shafts and no clarity-lit fog never needs the sun cascades: sun.w 0 gives the same
 // image bit for bit (half-lit shadow maps, so a cascade lookup would show).
 {for(float clarity:{0.f,.85f}){auto f=scene(.002f,clarity>0?0.f:1.f,0,3);f.temporal[1][0]=clarity;auto a=shot(f,volumetric::PATH_MARCHED,.5f,30);auto g=f;g.sun[3]=0;assert(same(a,shot(g,volumetric::PATH_MARCHED,.5f,30)));}}
 // K09: release() frees every GPU object after one queue drain; the next frame re-creates them with the same image.
 {auto f=scene(.004f,1,4,2);auto before=shot(f,volumetric::PATH_MARCHED,1,100);UINT idle=gpu.idle_count();gpu.release();assert(!gpu.loaded()&&gpu.idle_count()==idle+1&&!gpu.current_device());
  auto after=shot(f,volumetric::PATH_MARCHED,1,100);std::cout<<"Release before="<<before.centre[0]<<" after="<<after.centre[0]<<std::endl;assert(gpu.loaded()&&same(before,after));}
 gpu_test::wait(dev);gpu_test::expect_clean(dev,"lighting_gpu");CloseHandle(event);std::cout<<"PASS: "<<cases<<" HDR lighting GPU cases: shadow occlusion, depth-limited fog, zero-density identity, resizing, alpha and crisp restored HUD; constants path 0-8 lamps with bit-identical permutations, two-frame temporal, descriptor ring without idle, sea plane, flash glow, blend alpha, K25 skip identity, sun-free lamp frames, release and re-create\n";
}
