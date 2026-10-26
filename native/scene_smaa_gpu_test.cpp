#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>
#include "tests/gpu_test_device.h"
#include "aa/scene_smaa_gpu.h"
#include "aa/scene_smaa_bindings.h"
using Microsoft::WRL::ComPtr;
// K39: WARP with the debug layer and GPU-based validation (gpu_test::create);
// every case waits and fails on any ERROR/CORRUPTION message (expect_clean).
static gpu_test::Device d;static ComPtr<ID3D12CommandAllocator> allocator;static ComPtr<ID3D12GraphicsCommandList> list;
static std::vector<ComPtr<ID3D12Resource>> keep; // upload/readback buffers alive until the case's wait
static ComPtr<ID3D12DescriptorHeap> heap(D3D12_DESCRIPTOR_HEAP_TYPE type,UINT n,bool visible){D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=type;hd.NumDescriptors=n;hd.Flags=visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE;ComPtr<ID3D12DescriptorHeap> h;assert(SUCCEEDED(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&h))));return h;}
static void begin(){assert(gpu_test::wait(d));keep.clear();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));}
static void submit(const char* where){assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};d.queue->ExecuteCommandLists(1,lists);assert(gpu_test::wait(d));gpu_test::expect_clean(d,where);}
static ComPtr<ID3D12Resource> texture(UINT w,UINT h,DXGI_FORMAT f,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear=nullptr){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=f;td.SampleDesc.Count=1;td.Flags=flags;ComPtr<ID3D12Resource> r;assert(SUCCEEDED(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,state,clear,IID_PPV_ARGS(&r))));return r;}
static ComPtr<ID3D12Resource> buffer(UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=1;b.DepthOrArraySize=1;b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> r;assert(SUCCEEDED(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r))));keep.push_back(r);return r;}
// 4-byte texels: pixel(x,y) returns R,G,B,A in bytes 0..3 (BGRA targets get the same bytes, so tests use grey or symmetric colours there).
static void upload(ID3D12Resource* t,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after,const std::function<uint32_t(UINT,UINT)>& pixel){auto td=t->GetDesc();UINT64 bytes{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};d.device->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);unsigned char* m{};D3D12_RANGE empty{0,0};assert(SUCCEEDED(up->Map(0,&empty,(void**)&m)));for(UINT y=0;y<td.Height;++y)for(UINT x=0;x<td.Width;++x){uint32_t v=pixel(x,y);memcpy(m+fp.Offset+y*fp.Footprint.RowPitch+x*4,&v,4);}up->Unmap(0,nullptr);
 if(before!=D3D12_RESOURCE_STATE_COPY_DEST){auto b=scene_smaa::transition(t,before,D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&b);}D3D12_TEXTURE_COPY_LOCATION to{t,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}},from{up.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}};to.SubresourceIndex=0;from.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);auto b=scene_smaa::transition(t,D3D12_RESOURCE_STATE_COPY_DEST,after);list->ResourceBarrier(1,&b);}
// Records a copy to a readback buffer (t goes from `state` to COPY_SOURCE and stays there); read() after submit().
struct Readback {ComPtr<ID3D12Resource> buffer;D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT bpp{};std::vector<unsigned char> bytes;
 const unsigned char* at(UINT x,UINT y)const{return bytes.data()+y*fp.Footprint.RowPitch+x*bpp;}};
static Readback copy_out(ID3D12Resource* t,D3D12_RESOURCE_STATES state,UINT bpp){Readback r;r.bpp=bpp;auto td=t->GetDesc();UINT64 bytes{};d.device->GetCopyableFootprints(&td,0,1,0,&r.fp,nullptr,nullptr,&bytes);r.buffer=buffer(bytes,D3D12_HEAP_TYPE_READBACK);auto b=scene_smaa::transition(t,state,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION from{t,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}},to{r.buffer.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}};from.SubresourceIndex=0;to.PlacedFootprint=r.fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);r.bytes.resize(size_t(bytes));return r;}
static void read(Readback& r){unsigned char* m{};D3D12_RANGE range{0,r.bytes.size()},empty{0,0};assert(SUCCEEDED(r.buffer->Map(0,&range,(void**)&m)));memcpy(r.bytes.data(),m+r.fp.Offset,r.bytes.size()-size_t(r.fp.Offset));r.buffer->Unmap(0,&empty);}
static uint32_t grey(unsigned char v){return v|v<<8|v<<16|0xff000000u;}
// A camera 10 m above the sea looking along +z; depth = .1/z (reversed), so depth .005 is 20 m.
static scene_smaa::Extra taa_extra(ID3D12Resource* depth){scene_smaa::Extra e;e.temporal=true;e.history_valid=true;e.history_weight=.9f;e.depth=depth;e.depth_state=D3D12_RESOURCE_STATE_DEPTH_WRITE;e.flags=scene_smaa::flag_catmull_rom|scene_smaa::flag_still_v2;
 float k[32]={0,.1f,1,0, 1,0,0,.5f, 0,1,0,.5f, 0,0,1,10000, 0,0,0,0, 1,0,0,.5f, 0,1,0,.5f, 0,0,1,0};std::copy(k,k+32,e.constants.begin());return e;}
int main(){std::cout<<std::unitbuf;assert(gpu_test::create(d));auto device=d.device;
 assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 // Deliberately different HUD pipeline/root/viewport/scissor, restored after AA.
 D3D12_ROOT_PARAMETER arg{};arg.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;arg.Constants={0,0,4};arg.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;D3D12_ROOT_SIGNATURE_DESC rd{1,&arg,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> signature,errors,vs,ps;assert(SUCCEEDED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors)));ComPtr<ID3D12RootSignature> root;assert(SUCCEEDED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root))));
 const char* shader="cbuffer Color:register(b0){float4 color;}float4 V(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}float4 P():SV_Target{return color;}";assert(SUCCEEDED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"V","vs_5_0",0,0,&vs,&errors)));assert(SUCCEEDED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"P","ps_5_0",0,0,&ps,&errors)));
 auto srv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,true);auto rtv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,4,false);auto dsv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);UINT rtv_stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 auto rtv_at=[&](UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*rtv_stride;return h;};
 std::vector<ComPtr<ID3D12PipelineState>> huds;auto hud_for=[&](DXGI_FORMAT format){D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.SampleMask=UINT_MAX;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=format;pd.SampleDesc.Count=1;ComPtr<ID3D12PipelineState> hud;assert(SUCCEEDED(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&hud))));huds.push_back(hud);return hud.Get();};
 // The game's bindings as the AA hooks capture them; restored after AA, then the HUD draws green into x<4 of an 8x8 viewport.
 auto bindings=[&](ID3D12Resource* back,DXGI_FORMAT format){scene_smaa::Bindings saved;saved.reset();saved.signature(root.Get());saved.pipeline=hud_for(format);saved.heap_count=1;saved.heaps[0]=srv.Get();saved.topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;saved.viewport_count=saved.scissor_count=saved.target_count=1;saved.viewports[0]={0,0,8,8,0,1};saved.scissors[0]={0,0,4,8};device->CreateRenderTargetView(back,nullptr,rtv_at(0));saved.targets[0]=rtv_at(0);float green[]={0,1,0,1};saved.constants(0,4,green,0);assert(saved.ready());return saved;};
 auto hud_ok=[](const Readback& r){auto ui=r.at(2,2);auto clipped=r.at(6,2);return ui[0]==0&&ui[1]==255&&ui[2]==0&&(clipped[1]!=255||clipped[0]==255);};
 scene_smaa::Gpu aa;
 // 0.40.1 cases: SMAA quality, flat colour, straight edges, enhanced and sharpening; HUD crisp and clipped after restore.
 auto check=[&](UINT w,UINT h,DXGI_FORMAT format,UINT quality,bool flat,bool enhanced=false,bool straight=false,float sharpen=0){
  begin();auto back=texture(w,h,format,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);assert(aa.ensure(device.Get(),d.queue.Get(),back->GetDesc()));
  upload(back.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET,[&](UINT x,UINT y){return grey(flat?117:((straight?x>w/2:float(x)>.6f*y+15)?255:0));});
  auto saved=bindings(back.Get(),format);scene_smaa::Extra extra;extra.sharpen=sharpen;aa.record(list.Get(),back.Get(),quality,enhanced,sharpen>0?&extra:nullptr);saved.restore(list.Get());list->DrawInstanced(3,1,0,0);
  auto out=copy_out(back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,4);submit("smaa");read(out);unsigned smooth{},softness{};
  for(UINT y=10;y<h-2;++y)for(UINT x=10;x<w-2;++x){auto p=out.at(x,y);if(straight)assert(p[0]==(x>w/2?255:0)&&p[0]==p[1]&&p[1]==p[2]);else if(flat)assert(abs(int(p[0])-117)<=1&&p[0]==p[1]&&p[1]==p[2]);else{softness+=std::min(unsigned(p[0]),255u-p[0]);if(p[0]>3&&p[0]<252)++smooth;}}
  assert(hud_ok(out));if(!flat&&!straight)assert(smooth>20);std::cout<<"PASS: SMAA quality="<<quality<<" size="<<w<<'x'<<h<<" blended_edge_pixels="<<smooth<<" edge_blending="<<softness<<" enhanced="<<enhanced<<" flat="<<flat<<" restored HUD stays crisp and clipped\n";return softness;};
 for(UINT quality=0;quality<4;++quality){check(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,quality,false);check(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,quality,true);}check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false);check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,true);
 auto ordinary=check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false);auto enhanced=check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false,true);assert(enhanced>ordinary);check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,true,true);
 check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false,false,true);check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false,true,true);
 // Sharpening goes through the offscreen resolve and output pass: flat stays flat, straight edges stay exact, the HUD is restored.
 check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,true,false,false,.8f);check(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,3,false,false,true,.8f);check(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,2,false,false,false,.3f);
 std::cout<<"PASS: 18 SMAA cases (0.40.1 set)\n";

 // ---- TAA. Depth is R32_TYPELESS in DEPTH_WRITE (as the game's), cleared through a DSV.
 struct Scene {UINT w,h;DXGI_FORMAT format;ComPtr<ID3D12Resource> back,depth,ocean,albedo;D3D12_RESOURCE_STATES back_state=D3D12_RESOURCE_STATE_COPY_DEST;};
 auto make_scene=[&](UINT w,UINT h,DXGI_FORMAT format,float depth_value){Scene s{w,h,format};s.back=texture(w,h,format,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=depth_value;s.depth=texture(w,h,DXGI_FORMAT_R32_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear);
  D3D12_CLEAR_VALUE colour{};colour.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;s.ocean=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,&colour);s.albedo=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,&colour);
  begin();D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(s.depth.Get(),&dv,dsv->GetCPUDescriptorHandleForHeapStart());list->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,depth_value,0,0,nullptr);
  float sea[4]={0,1,0,0},rim[4]={.5f,.5f,.5f,1};device->CreateRenderTargetView(s.ocean.Get(),nullptr,rtv_at(1));device->CreateRenderTargetView(s.albedo.Get(),nullptr,rtv_at(2));list->ClearRenderTargetView(rtv_at(1),sea,0,nullptr);list->ClearRenderTargetView(rtv_at(2),rim,0,nullptr);submit("scene");return s;};
 // One frame: upload the image, AA (extra may be null), restore, HUD, read back.
 auto frame=[&](scene_smaa::Gpu& g,Scene& s,const std::function<uint32_t(UINT,UINT)>& image,const scene_smaa::Extra* extra,UINT quality=3,bool* depth_ok=nullptr){begin();assert(g.ensure(device.Get(),d.queue.Get(),s.back->GetDesc()));
  upload(s.back.Get(),s.back_state,D3D12_RESOURCE_STATE_RENDER_TARGET,image);auto saved=bindings(s.back.Get(),s.format);bool ok=g.record(list.Get(),s.back.Get(),quality,false,extra);if(depth_ok)*depth_ok=ok;saved.restore(list.Get());list->DrawInstanced(3,1,0,0);
  auto out=copy_out(s.back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,4);s.back_state=D3D12_RESOURCE_STATE_COPY_SOURCE;submit("taa");read(out);assert(hud_ok(out));return out;};
 auto diagonal=[](UINT x,UINT y){return grey(float(x)>.6f*y+15?230:20);};
 {// AA-14: lazy targets. SMAA alone allocates neither; sharpening the resolved image only; TAA both.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);frame(g,s,diagonal,nullptr);assert(!g.offscreen_allocated()&&!g.history_allocated());
  scene_smaa::Extra sharp;sharp.sharpen=.5f;frame(g,s,diagonal,&sharp);assert(g.offscreen_allocated()&&!g.history_allocated()&&!g.temporal_ran());
  auto e=taa_extra(s.depth.Get());bool ok{};frame(g,s,diagonal,&e,3,&ok);assert(ok&&g.history_allocated()&&g.temporal_ran());std::cout<<"PASS: TAA targets created on first use (AA-14)\n";}
 {// Failed target creation: SMAA writes the frame, temporal_ran() is false (the caller stops jitter), and no new attempt is made before the 1 s backoff.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());e.fail_targets=true;bool ok{};auto start=GetTickCount64();
  frame(g,s,diagonal,&e,3,&ok);assert(ok&&!g.temporal_ran()&&!g.offscreen_allocated()&&!g.history_allocated());
  e.fail_targets=false;frame(g,s,diagonal,&e,3,&ok);assert(ok);if(GetTickCount64()-start<900)assert(!g.temporal_ran()&&!g.offscreen_allocated()&&!g.history_allocated());
  Sleep(1100);frame(g,s,diagonal,&e,3,&ok);assert(ok&&g.temporal_ran()&&g.history_allocated());std::cout<<"PASS: TAA target failure falls back to SMAA with backoff\n";}
 {// Static scene converges: 6 identical frames stay within 2 LSB of the 1-frame result away from the edge, and flat areas stay exact.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());e.dither=0;auto first=frame(g,s,diagonal,&e);Readback last;for(int i=0;i<5;++i)last=frame(g,s,diagonal,&e);
  unsigned changed{};for(UINT y=10;y<60;++y)for(UINT x=10;x<60;++x){int a=first.at(x,y)[0],b=last.at(x,y)[0];if(std::abs(a-b)>2)++changed;if(std::abs(float(x)-.6f*y-15)>4)assert(std::abs(b-(float(x)>.6f*y+15?230:20))<=1);}
  std::cout<<"PASS: TAA static scene stable over 6 frames, flat areas exact, edge pixels changed="<<changed<<"\n";}
 {// AA-7: history invalid and a half-pixel jitter: the straight edge is resampled half a pixel over (column w/2 becomes mid grey), other columns exact.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());e.dither=0;e.history_valid=false;auto edge=[](UINT x,UINT){return grey(x>32?255:0);};
  auto plain=frame(g,s,edge,&e);for(UINT x=8;x<56;++x)assert(plain.at(x,20)[0]==(x>32?255:0));
  e.constants[19]=.5f/64;auto moved=frame(g,s,edge,&e);int mid=moved.at(32,20)[0];assert(mid>=120&&mid<=135);for(UINT x=8;x<56;++x)if(x!=32)assert(moved.at(x,20)[0]==(x>32?255:0));std::cout<<"PASS: rejected pixels re-centred by the jitter (AA-7) mid="<<mid<<"\n";}
 {// Depth rejection: 3-px stripes, then the same stripes shifted by 3 px (inverted). At the same distance the history survives the
  // (wide) 3x3 box and pulls the output away from the new frame; at 10x the distance it is rejected and the output equals SMAA alone.
  // Period 6, not 4: the resolve's undither replaces any 4x4-periodic pattern (2-px stripes) with its box average, which leaves
  // no edge for the history to survive in. SMAA keeps the straight stripes exact, so SMAA alone is the stripes themselves.
  auto stripes=[](UINT phase){return [phase](UINT x,UINT){return grey(((x+phase)/3)%2?255:0);};};
  scene_smaa::Gpu ref;auto r=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto alone=frame(ref,r,stripes(3),nullptr);for(UINT y=10;y<60;++y)for(UINT x=10;x<60;++x)assert(alone.at(x,y)[0]==(((x+3)/3)%2?255:0));
  for(bool reject:{false,true}){scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());e.dither=0;frame(g,s,stripes(0),&e);
   if(reject){begin();D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(s.depth.Get(),&dv,dsv->GetCPUDescriptorHandleForHeapStart());list->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,.0005f,0,0,nullptr);submit("depth");}
   auto out=frame(g,s,stripes(3),&e);unsigned far_off{},worst{};for(UINT y=10;y<60;++y)for(UINT x=10;x<60;++x){unsigned dlt=unsigned(std::abs(int(out.at(x,y)[0])-int(alone.at(x,y)[0])));worst=std::max(worst,dlt);far_off+=dlt>40;}
   if(reject)assert(worst<=1);else assert(far_off>50*50/4);std::cout<<"PASS: TAA history "<<(reject?"rejected at another depth":"kept at the same depth")<<" far_from_SMAA="<<far_off<<" max="<<worst<<"\n";}}
 {// K05/AA-6 inputs: ocean (y>0) and rim (w=1) tables bound under GPU-based validation, 3 frames; then the same scene without them. Output finite and HUD restored.
  scene_smaa::Gpu g;auto s=make_scene(96,80,DXGI_FORMAT_B8G8R8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());e.ocean=s.ocean.Get();e.ocean_state=D3D12_RESOURCE_STATE_RENDER_TARGET;e.albedo=s.albedo.Get();e.albedo_state=D3D12_RESOURCE_STATE_RENDER_TARGET;e.sea_height=10;
  bool ok{};for(int i=0;i<3;++i){frame(g,s,diagonal,&e,3,&ok);assert(ok);}e.ocean=e.albedo=nullptr;frame(g,s,diagonal,&e,3,&ok);assert(ok);
  e.flags|=scene_smaa::flag_thin_rescue;frame(g,s,diagonal,&e,3,&ok);assert(ok);std::cout<<"PASS: TAA ocean and rim inputs, thin rescue, BGRA, no validation errors\n";}
 {// AA-15: the MRT path (sharpening 0) writes the same back buffer as Temporal + Output (split_output), dither on, 3 frames each, RGBA and BGRA.
  for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}){scene_smaa::Gpu a,b;auto sa=make_scene(96,80,format,.005f),sb=make_scene(96,80,format,.005f);auto ea=taa_extra(sa.depth.Get()),eb=taa_extra(sb.depth.Get());ea.dither=eb.dither=1;eb.split_output=true;
   Readback ra,rb;for(int i=0;i<3;++i){ra=frame(a,sa,diagonal,&ea);rb=frame(b,sb,diagonal,&eb);}unsigned differ{},worst{};
   for(UINT y=0;y<80;++y)for(UINT x=8;x<96;++x)for(int c=0;c<3;++c){unsigned dlt=unsigned(std::abs(int(ra.at(x,y)[c])-int(rb.at(x,y)[c])));if(dlt){++differ;worst=std::max(worst,dlt);}}
   assert(worst<=1);std::cout<<"PASS: MRT output matches the separate output pass format="<<format<<" differing_channels="<<differ<<" max="<<worst<<"\n";}}
 {// FIN-4: dither on a flat 117 image moves values by at most 1 LSB and is not constant; black and white stay exact.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);scene_smaa::Extra e;e.sharpen=0.0001f;e.dither=1;auto out=frame(g,s,[](UINT x,UINT){return grey(x<16?0:x<32?255:117);},&e);
  unsigned moved{};for(UINT y=10;y<60;++y)for(UINT x=10;x<60;++x){int v=out.at(x,y)[0];if(x<14)assert(v==0);else if(x>17&&x<30)assert(v==255);else if(x>34){assert(std::abs(v-117)<=1);moved+=v!=117;}}assert(moved>50);std::cout<<"PASS: output dither within 1 LSB, spared at black and white, dithered="<<moved<<"\n";}
 {// Resize with history: 64x64 then 96x80, history recreated and invalid on the first new frame; no validation errors.
  scene_smaa::Gpu g;auto s=make_scene(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto e=taa_extra(s.depth.Get());frame(g,s,diagonal,&e);frame(g,s,diagonal,&e);
  auto t=make_scene(96,80,DXGI_FORMAT_R8G8B8A8_UNORM,.005f);auto f=taa_extra(t.depth.Get());bool ok{};frame(g,t,diagonal,&f,3,&ok);assert(ok&&g.history_allocated());frame(g,t,diagonal,&f,3,&ok);assert(ok);std::cout<<"PASS: TAA resize mid-history\n";}
 {// AA-3: the gather undither against the 0.40.1 16-tap kernel (UnditherProbe: rgb = new - old, a = early-out taken).
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants={0,0,scene_smaa::root_constants};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={1,&range};p[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};for(UINT i=0;i<2;++i){auto& sd=samplers[i];sd.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sd.MaxLOD=D3D12_FLOAT32_MAX;sd.ShaderRegister=i;sd.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
  D3D12_ROOT_SIGNATURE_DESC prd{2,p,2,samplers,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> code;assert(SUCCEEDED(D3D12SerializeRootSignature(&prd,D3D_ROOT_SIGNATURE_VERSION_1,&code,&errors)));ComPtr<ID3D12RootSignature> probe_root;assert(SUCCEEDED(device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&probe_root))));
  using namespace scene_smaa_bytecode;D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=probe_root.Get();pd.VS={Vertex3,sizeof(Vertex3)};pd.PS={UnditherProbe3,sizeof(UnditherProbe3)};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.SampleMask=UINT_MAX;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;pd.SampleDesc.Count=1;ComPtr<ID3D12PipelineState> probe;assert(SUCCEEDED(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&probe))));
  auto bayer=[](UINT x,UINT y){static const int m[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};return m[(y&3)*4+(x&3)];};
  auto rgb=[](int r,int g,int b){return uint32_t(r)|uint32_t(g)<<8|uint32_t(b)<<16|0xff000000u;};
  struct Pattern {const char* name;std::function<uint32_t(UINT,UINT)> pixel;bool natural;};uint32_t seed=12345;std::vector<uint32_t> noise(64*64);for(auto& n:noise){seed=seed*1664525u+1013904223u;n=rgb(seed>>24,(seed>>16)&255,(seed>>8)&255);}
  std::vector<Pattern> patterns={{"flat",[&](UINT,UINT){return rgb(117,117,117);},true},{"gradient",[&](UINT x,UINT){return rgb(x*4,x*4,x*4);},true},
   {"cloud25",[&](UINT x,UINT y){return bayer(x,y)<4?rgb(255,255,255):rgb(120,160,220);},true},{"cloud50",[&](UINT x,UINT y){return bayer(x,y)<8?rgb(255,255,255):rgb(120,160,220);},true},{"cloud75",[&](UINT x,UINT y){return bayer(x,y)<12?rgb(255,255,255):rgb(120,160,220);},true},
   {"grass",[&](UINT x,UINT y){return bayer(x,y)<8?rgb(40,110,30):rgb(70,90,60);},true},{"edge",[&](UINT x,UINT){return x>30?rgb(250,250,250):rgb(10,10,10);},true},{"noise",[&](UINT x,UINT y){return noise[y*64+x];},true},
   {"magenta_flat_green",[&](UINT x,UINT y){return bayer(x,y)<8?rgb(255,128,255):rgb(0,128,0);},false}};
  auto source=texture(64,64,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_CLEAR_VALUE fc{};fc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;auto target=texture(64,64,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,&fc);
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for(UINT i=0;i<3;++i){auto h=srv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*stride;device->CreateShaderResourceView(i?nullptr:source.Get(),&sv,h);}
  D3D12_RESOURCE_STATES source_state=D3D12_RESOURCE_STATE_COPY_DEST,target_state=D3D12_RESOURCE_STATE_RENDER_TARGET;
  for(auto& pattern:patterns){begin();upload(source.Get(),source_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,pattern.pixel);source_state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;if(target_state!=D3D12_RESOURCE_STATE_RENDER_TARGET){auto b=scene_smaa::transition(target.Get(),target_state,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);}
   device->CreateRenderTargetView(target.Get(),nullptr,rtv_at(3));gpu_test::poison(list.Get(),target.Get(),rtv_at(3));float k[scene_smaa::root_constants]{1.f/64,1.f/64,64,64};
   list->SetGraphicsRootSignature(probe_root.Get());ID3D12DescriptorHeap* heaps[]={srv.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRoot32BitConstants(0,scene_smaa::root_constants,k,0);list->SetGraphicsRootDescriptorTable(1,srv->GetGPUDescriptorHandleForHeapStart());list->SetPipelineState(probe.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D12_VIEWPORT vp{0,0,64,64,0,1};D3D12_RECT sc{0,0,64,64};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);auto rt=rtv_at(3);list->OMSetRenderTargets(1,&rt,FALSE,nullptr);list->DrawInstanced(3,1,0,0);
   auto out=copy_out(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,16);target_state=D3D12_RESOURCE_STATE_COPY_SOURCE;submit("undither");read(out);float worst=0;unsigned flat_exit{},moment_exit{};
   for(UINT y=0;y<64;++y)for(UINT x=0;x<64;++x){float v[4];memcpy(v,out.at(x,y),16);for(int c=0;c<4;++c)assert(std::isfinite(v[c]));for(int c=0;c<3;++c)worst=std::max(worst,std::abs(v[c]));flat_exit+=v[3]==1;moment_exit+=v[3]==.5f;}
   if(pattern.natural)assert(worst<=.5f/255);if(!strcmp(pattern.name,"flat"))assert(flat_exit==64*64);
   std::cout<<(pattern.natural?"PASS":"INFO")<<": undither "<<pattern.name<<" max_diff_lsb="<<worst*255<<" flat_exit="<<flat_exit<<" moment_exit="<<moment_exit<<" full="<<64*64-flat_exit-moment_exit<<"\n";}}
 assert(gpu_test::wait(d));gpu_test::expect_clean(d,"end");
 std::cout<<"PASS: SMAA, TAA (lazy targets, target failure backoff, convergence, re-centring, depth rejection, ocean/rim inputs, resize, MRT parity), output dither and undither parity with no D3D12 errors (validated="<<d.validated<<")\n";
}
