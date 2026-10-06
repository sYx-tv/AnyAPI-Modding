#define NOMINMAX
#include <windows.h>
#include <cstring>
#include "anyapi_services_v1.h"
#include "anyapi_post_process_v1.h"
// Minimal independent shader mod. Register from Ready; update from an owned
// callback. This example affects the finished game image including native HUD.
static const AnyPostProcessV1* post;
static uint64_t pass;
static void render(const AnyFrameV1*,AnyCanvasV1*,void*){
 if(!post||!pass)return;AnyPostParametersV1 p;p.enabled=1;
 p.values[0]=1.05f;post->set(pass,&p);
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){
 if(!host||!callbacks||host->abi!=ANYAPI_MOD_ABI||host->struct_size!=sizeof(*host))return false;
 *callbacks={};callbacks->id="example.post_process";callbacks->render=render;return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){
 auto services=AnyAPI_Services();post=services?(const AnyPostProcessV1*)services->query("anyapi.post_process",1):nullptr;
 if(!post||post->version!=1||post->struct_size!=sizeof(*post))return;
 const char* shader="Texture2D image:register(t0);SamplerState s:register(s0);cbuffer Values:register(b1){float4 user[16];}float4 main(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target{return float4(saturate(image.SampleLevel(s,uv,0).rgb*user[0].x),1);}";
 AnyPostPassV1 d;d.id="brightness";d.shader=shader;d.shader_bytes=uint32_t(strlen(shader));pass=post->register_pass(&d);
}
