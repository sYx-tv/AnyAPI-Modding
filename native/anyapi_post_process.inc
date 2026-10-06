#include "anyapi_post_process_v1.h"
#include <d3dcompiler.h>
#include <chrono>
namespace post_process {
using Microsoft::WRL::ComPtr;
struct Pass {
 int owner{-1};std::string id;uint32_t downsample{1};uint64_t input{},auxiliary{};
 AnyPostParametersV1 parameters;ComPtr<ID3DBlob> code;ComPtr<ID3D11PixelShader> shader;
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11RenderTargetView> target;
};
static std::vector<Pass> passes;
static ComPtr<ID3D11Texture2D> scene;
static ComPtr<ID3D11ShaderResourceView> scene_view;
static ComPtr<ID3D11VertexShader> vertex;
static ComPtr<ID3D11PixelShader> copy_shader;
static ComPtr<ID3D11Buffer> frame_buffer,user_buffer;
static ComPtr<ID3D11SamplerState> sampler;
static ComPtr<ID3D11RasterizerState> raster;
static AnyPostStatusV1 snapshot;
static DXGI_FORMAT format{};
static bool failed{};
static std::unordered_map<ID3D11Resource*,ComPtr<ID3D11RenderTargetView>> back_targets;
static const char* vertex_source="struct V{float4 p:SV_Position;float2 uv:TEXCOORD;}; V main(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}";
static const char* copy_source="Texture2D image:register(t0);SamplerState sample_image:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target{return image.SampleLevel(sample_image,uv,0);}";
static bool compile(const char* text,size_t bytes,const char* target,ComPtr<ID3DBlob>& code,std::string& error){
 ComPtr<ID3DBlob> errors;HRESULT hr=D3DCompile(text,bytes,"AnyAPI mod shader",nullptr,nullptr,"main",target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(FAILED(hr)){error=errors?std::string((char*)errors->GetBufferPointer(),std::min<size_t>(errors->GetBufferSize(),240)):"Shader compilation failed";return false;}return true;
}
static void problem(const std::string& error){if(!failed)log_line(ANY_LOG_ERROR,"post_process",error.c_str());failed=true;snapshot.ready=0;strncpy_s(snapshot.error,error.c_str(),_TRUNCATE);}
static uint64_t registration(const AnyPostPassV1* d){
 std::lock_guard<std::recursive_mutex> lock(platform::render_mutex);int owner=platform::current_plugin;
 if(owner<0||owner>=32||!d||d->struct_size!=sizeof(*d)||d->version!=1||!d->id||!d->shader||!d->shader_bytes||d->shader_bytes>65536||passes.size()>=32||(d->downsample!=1&&d->downsample!=2&&d->downsample!=4))return 0;
 if(strlen(d->id)>63||std::count_if(passes.begin(),passes.end(),[&](auto& p){return p.owner==owner;})>=12)return 0;
 for(uint64_t t:{d->input,d->auxiliary})if(t&&(t>passes.size()||passes[t-1].owner!=owner))return 0;
 for(auto& p:passes)if(p.owner==owner&&p.id==d->id)return 0;
 Pass p;p.owner=owner;p.id=d->id;p.downsample=d->downsample;p.input=d->input;p.auxiliary=d->auxiliary;std::string error;
 if(!compile(d->shader,d->shader_bytes,"ps_5_0",p.code,error)){log_line(ANY_LOG_ERROR,platform::plugins[owner].callbacks.id,error.c_str());return 0;}passes.push_back(std::move(p));return passes.size();
}
static bool set(uint64_t token,const AnyPostParametersV1* value){
 std::lock_guard<std::recursive_mutex> lock(platform::render_mutex);
 if(!token||token>passes.size()||passes[token-1].owner!=platform::current_plugin||!value||value->struct_size!=sizeof(*value)||value->enabled>1)return false;
 for(float v:value->values)if(!std::isfinite(v)||std::abs(v)>10000)return false;passes[token-1].parameters=*value;return true;
}
static bool status(AnyPostStatusV1* value){std::lock_guard<std::recursive_mutex> lock(platform::render_mutex);if(!value||value->struct_size!=sizeof(*value)||value->version!=1)return false;*value=snapshot;return true;}
static void reset(){back_targets.clear();scene.Reset();scene_view.Reset();vertex.Reset();copy_shader.Reset();frame_buffer.Reset();user_buffer.Reset();sampler.Reset();raster.Reset();for(auto& p:passes){p.shader.Reset();p.texture.Reset();p.view.Reset();p.target.Reset();}snapshot.ready=0;snapshot.width=snapshot.height=0;snapshot.error[0]=0;failed=false;}
static bool active(){snapshot.active_passes=0;if(failed)return false;for(auto& p:passes)if(p.parameters.enabled&&p.downsample==1&&!platform::plugins[p.owner].faulted)return true;return false;}
static bool initialize(ID3D11Texture2D* back){
 auto device=platform::device11.Get();D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
 if(desc.SampleDesc.Count!=1||desc.Width>8192||desc.Height>8192||(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)){problem("Unsupported backbuffer format; effects bypassed");return false;}
 format=desc.Format;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.CPUAccessFlags=0;desc.MiscFlags=0;desc.ArraySize=1;desc.MipLevels=1;
 if(FAILED(device->CreateTexture2D(&desc,nullptr,&scene))||FAILED(device->CreateShaderResourceView(scene.Get(),nullptr,&scene_view))){problem("Scene copy allocation failed");return false;}
 ComPtr<ID3DBlob> code;std::string error;
 if(!compile(vertex_source,strlen(vertex_source),"vs_5_0",code,error)||FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex))){problem("Fullscreen vertex shader unavailable");return false;}
 if(!compile(copy_source,strlen(copy_source),"ps_5_0",code,error)||FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&copy_shader))){problem("Copy shader unavailable");return false;}
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 if(FAILED(device->CreateBuffer(&bd,nullptr,&frame_buffer))){problem("Frame constants unavailable");return false;}bd.ByteWidth=256;if(FAILED(device->CreateBuffer(&bd,nullptr,&user_buffer))){problem("Pass constants unavailable");return false;}
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
 if(FAILED(device->CreateSamplerState(&sd,&sampler))||FAILED(device->CreateRasterizerState(&rd,&raster))){problem("Fullscreen pipeline unavailable");return false;}
 snapshot.width=desc.Width;snapshot.height=desc.Height;++snapshot.resource_generation;snapshot.ready=1;log_line(ANY_LOG_INFO,"post_process",("GPU_READY size="+std::to_string(desc.Width)+"x"+std::to_string(desc.Height)+" frame_copy=GPU_ONLY").c_str());return true;
}
static bool prepare(Pass& p,bool texture_needed=true){auto device=platform::device11.Get();if(!p.shader&&FAILED(device->CreatePixelShader(p.code->GetBufferPointer(),p.code->GetBufferSize(),nullptr,&p.shader)))return false;if(!texture_needed||p.texture)return true;
 D3D11_TEXTURE2D_DESC td{};td.Width=std::max(1u,snapshot.width/p.downsample);td.Height=std::max(1u,snapshot.height/p.downsample);td.MipLevels=td.ArraySize=1;td.Format=format;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 return SUCCEEDED(device->CreateTexture2D(&td,nullptr,&p.texture))&&SUCCEEDED(device->CreateRenderTargetView(p.texture.Get(),nullptr,&p.target))&&SUCCEEDED(device->CreateShaderResourceView(p.texture.Get(),nullptr,&p.view));
}
static ID3D11ShaderResourceView* resolve(uint64_t token,ID3D11ShaderResourceView* fallback){if(!token)return fallback;auto& p=passes[token-1];return p.parameters.enabled&&!platform::plugins[p.owner].faulted&&p.view?p.view.Get():resolve(p.input,fallback);}
static void draw(ID3D11PixelShader* shader,ID3D11RenderTargetView* output,UINT w,UINT h,ID3D11ShaderResourceView* input,ID3D11ShaderResourceView* original,ID3D11ShaderResourceView* aux,const float* values){
 auto ctx=platform::context11.Get();ID3D11ShaderResourceView* empty[3]{};ctx->PSSetShaderResources(0,3,empty);ctx->OMSetRenderTargets(1,&output,nullptr);
 float dimensions[]={float(w),float(h),1.f/w,1.f/h,float(snapshot.width),float(snapshot.height),1.f/snapshot.width,1.f/snapshot.height};
 if(input){ComPtr<ID3D11Resource> resource;input->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(SUCCEEDED(resource.As(&texture))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);dimensions[4]=float(desc.Width);dimensions[5]=float(desc.Height);dimensions[6]=1.f/desc.Width;dimensions[7]=1.f/desc.Height;}}
 ctx->UpdateSubresource(frame_buffer.Get(),0,nullptr,dimensions,0,0);if(values)ctx->UpdateSubresource(user_buffer.Get(),0,nullptr,values,0,0);
 ID3D11Buffer* buffers[]={frame_buffer.Get(),user_buffer.Get()};ctx->PSSetConstantBuffers(0,2,buffers);ID3D11ShaderResourceView* views[]={input,original,aux};ctx->PSSetShaderResources(0,3,views);auto sample=sampler.Get();ctx->PSSetSamplers(0,1,&sample);ctx->PSSetShader(shader,nullptr,0);
 D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};ctx->RSSetViewports(1,&vp);ctx->Draw(3,0);ctx->PSSetShaderResources(0,3,empty);ctx->OMSetRenderTargets(0,nullptr,nullptr);
}
// Runs with the existing wrapped backbuffer acquired, before AnyAPI UI composition.
static bool render(ID3D11Resource* back){auto begin=std::chrono::steady_clock::now();ComPtr<ID3D11Texture2D> texture;if(FAILED(back->QueryInterface(IID_PPV_ARGS(&texture))))return false;
 if(!scene&&!initialize(texture.Get()))return false;
 Pass* final=nullptr;for(auto& p:passes)if(p.parameters.enabled&&!platform::plugins[p.owner].faulted)final=&p;bool direct=final&&final->downsample==1;
 // Allocate/validate all resources before writing the game image. A failure bypasses.
 for(auto& p:passes)if(p.parameters.enabled&&!platform::plugins[p.owner].faulted&&!prepare(p,!(direct && &p==final))){problem("Shader pass allocation failed; effects bypassed");return false;}
 auto found=back_targets.find(back);if(found==back_targets.end()){ComPtr<ID3D11RenderTargetView> view;if(FAILED(platform::device11->CreateRenderTargetView(back,nullptr,&view))){problem("Backbuffer target unavailable");return false;}found=back_targets.emplace(back,std::move(view)).first;}auto target=found->second.Get();
 auto ctx=platform::context11.Get();ctx->ClearState();ctx->CopyResource(scene.Get(),back);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->RSSetState(raster.Get());
 ID3D11ShaderResourceView* latest=scene_view.Get();snapshot.active_passes=0;
 for(auto& p:passes)if(p.parameters.enabled&&!platform::plugins[p.owner].faulted){auto input=resolve(p.input,latest),aux=resolve(p.auxiliary,scene_view.Get());D3D11_TEXTURE2D_DESC desc{};if(direct && &p==final){desc.Width=snapshot.width;desc.Height=snapshot.height;}else p.texture->GetDesc(&desc);draw(p.shader.Get(),direct && &p==final?target:p.target.Get(),desc.Width,desc.Height,input,scene_view.Get(),aux,p.parameters.values);if(p.downsample==1)latest=p.view.Get();++snapshot.active_passes;}
 if(!direct)draw(copy_shader.Get(),target,snapshot.width,snapshot.height,latest,nullptr,nullptr,nullptr);ctx->ClearState();++snapshot.frames;snapshot.cpu_submit_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return true;
}
static const AnyPostProcessV1 api{sizeof(AnyPostProcessV1),1,registration,set,status};
}
static const AnyPostProcessV1* anyapi_post_process_service(){return &post_process::api;}
