#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <iostream>
#include <vector>
#include "anygraphics_options.h"
#include "anygraphics_shaders.h"
using Microsoft::WRL::ComPtr;
using namespace graphics_options;
static ComPtr<ID3DBlob> compile(const std::string& text,const char* profile){ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(text.c_str(),text.size(),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);if(FAILED(hr)&&error)std::cerr<<(char*)error->GetBufferPointer();assert(SUCCEEDED(hr));return code;}
int main(){ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx)));
 const UINT width=16,height=16;D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> input,output,readback;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&input)));assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&output)));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&readback)));
 ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11RenderTargetView> target;assert(SUCCEEDED(device->CreateShaderResourceView(input.Get(),nullptr,&view)));assert(SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&target)));
 auto code=compile("struct V{float4 p:SV_Position;float2 uv:TEXCOORD;};V main(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}","vs_5_0");ComPtr<ID3D11VertexShader> vertex;assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex)));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> frame,user;assert(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&frame)));bd.ByteWidth=256;assert(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&user)));float dimensions[]={16,16,1.f/16,1.f/16,16,16,1.f/16,1.f/16};ctx->UpdateSubresource(frame.Get(),0,nullptr,dimensions,0,0);
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;assert(SUCCEEDED(device->CreateSamplerState(&sd,&sampler)));
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;assert(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
 std::vector<unsigned char> pixels(width*height*4);auto uniform=[&](){for(size_t i=0;i<pixels.size();i+=4){pixels[i]=64;pixels[i+1]=128;pixels[i+2]=192;pixels[i+3]=255;}};uniform();
 auto run=[&](const std::string& source,const std::array<float,64>& values){ctx->ClearState();ctx->UpdateSubresource(input.Get(),0,nullptr,pixels.data(),width*4,0);auto shader_code=compile(source,"ps_5_0");ComPtr<ID3D11PixelShader> shader;assert(SUCCEEDED(device->CreatePixelShader(shader_code->GetBufferPointer(),shader_code->GetBufferSize(),nullptr,&shader)));ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(shader.Get(),nullptr,0);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->RSSetState(raster.Get());D3D11_VIEWPORT vp{0,0,16,16,0,1};ctx->RSSetViewports(1,&vp);auto rt=target.Get();ctx->OMSetRenderTargets(1,&rt,nullptr);ID3D11ShaderResourceView* views[]={view.Get(),view.Get(),view.Get()};ctx->PSSetShaderResources(0,3,views);auto sample=sampler.Get();ctx->PSSetSamplers(0,1,&sample);ctx->UpdateSubresource(user.Get(),0,nullptr,values.data(),0,0);ID3D11Buffer* buffers[]={frame.Get(),user.Get()};ctx->PSSetConstantBuffers(0,2,buffers);ctx->Draw(3,0);ctx->ClearState();ctx->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped;assert(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));std::vector<unsigned char> result(pixels.size());for(UINT y=0;y<height;++y)memcpy(result.data()+y*width*4,(unsigned char*)mapped.pData+y*mapped.RowPitch,width*4);ctx->Unmap(readback.Get(),0);return result;};
 auto v=defaults();auto neutral=run(graphics_shaders::finish(),v);for(size_t i=0;i<pixels.size();++i)assert(std::abs(int(pixels[i])-neutral[i])<=1);
 v[Exposure]=1;auto exposed=run(graphics_shaders::finish(),v);assert(exposed[0]>=127&&exposed[1]>=254&&exposed[2]>=254);
 v[Comparison]=1;auto split=run(graphics_shaders::finish(),v);assert(split[0]==64&&split[1]==128&&split[2]==192&&split[12*4]>=127);
 v=defaults();v[Gamma]=.5f;auto gamma=run(graphics_shaders::finish(),v);assert(std::abs(gamma[0]-16)<=1&&std::abs(gamma[1]-64)<=1&&std::abs(gamma[2]-145)<=1);
 v=defaults();v[BloomThreshold]=0;auto extracted=run(graphics_shaders::extract(),v);assert(extracted[0]>=63);auto blurred=run(graphics_shaders::blur(true),v);assert(std::abs(blurred[0]-64)<=1);auto bloom=run(graphics_shaders::composite(),v);assert(bloom[0]>neutral[0]&&bloom[1]>neutral[1]);
 for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){size_t i=(y*width+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=x>=y?230:20;pixels[i+3]=255;}v=defaults();v[Antialias]=2;auto smooth=run(graphics_shaders::smoothing(),v);size_t edge=(8*width+8)*4;assert(smooth[edge]<pixels[edge]&&smooth[edge]>20);
 auto sharp=run(graphics_shaders::sharpen(),v);assert(sharp[edge]>pixels[edge]);uniform();v=defaults();v[Vignette]=1;v[VignetteAmount]=.7f;v[VignetteRadius]=.1f;auto vignette=run(graphics_shaders::finish(),v);assert(vignette[0]<neutral[0]);
 std::cout<<"PASS: GPU pixels validate neutral color, exposure, comparison, gamma, bloom, blur, edge smoothing, sharpening and vignette\n";
}
