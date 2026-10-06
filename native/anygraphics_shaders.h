#pragma once
#include <string>
namespace graphics_shaders {
static const char* common=R"HLSL(
Texture2D image:register(t0);Texture2D original:register(t1);Texture2D auxiliary:register(t2);
SamplerState sample_image:register(s0);
cbuffer Frame:register(b0){float4 output_size;float4 input_size;}
cbuffer Parameters:register(b1){float4 values[16];}
float option(int i){return values[i/4][i%4];}
float3 color(float2 uv){return image.SampleLevel(sample_image,uv,0).rgb;}
float luminance(float3 c){return dot(c,float3(.2126,.7152,.0722));}
)HLSL";
static std::string smoothing(){return std::string(common)+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float2 step=input_size.zw;float3 c=color(uv),n=color(uv-float2(0,step.y)),s=color(uv+float2(0,step.y)),e=color(uv+float2(step.x,0)),w=color(uv-float2(step.x,0));
 float horizontal=abs(luminance(e)-luminance(w)),vertical=abs(luminance(n)-luminance(s));
 float range=max(max(luminance(n),luminance(s)),max(luminance(e),luminance(w)))-min(min(luminance(n),luminance(s)),min(luminance(e),luminance(w)));
 float amount=smoothstep(option(4),option(4)*2,range)*option(3);
 float3 neighbors=horizontal>vertical?(n+s)*.5:(e+w)*.5;
 if(option(2)>1.5)neighbors=lerp(neighbors,(n+s+e+w)*.25,.35);
 return float4(lerp(c,neighbors,amount*.75),1);
})HLSL";}
static std::string extract(){return std::string(common)+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float2 step=input_size.zw*.5;float3 c=(color(uv+step)+color(uv-step)+color(uv+float2(step.x,-step.y))+color(uv+float2(-step.x,step.y)))*.25;
 float peak=max(c.r,max(c.g,c.b));float knee=option(12),x=peak-option(11);float soft=clamp(x+knee,0,2*knee);soft=soft*soft/(4*knee+.00001);
 return float4(c*max(x,soft)/max(peak,.00001),1);
})HLSL";}
static std::string blur(bool horizontal){return std::string(common)+(horizontal?"static const float2 axis=float2(1,0);":"static const float2 axis=float2(0,1);")+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float2 step=axis*input_size.zw*option(13);float3 c=color(uv)*.227027;
 c+=(color(uv+step*1.384615)+color(uv-step*1.384615))*.316216;
 c+=(color(uv+step*3.230769)+color(uv-step*3.230769))*.070270;
 return float4(c,1);
})HLSL";}
static std::string composite(){return std::string(common)+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float3 glow=auxiliary.SampleLevel(sample_image,uv,0).rgb;glow=lerp(luminance(glow).xxx,glow,option(16));glow*=float3(1+option(15)*.2,1,1-option(15)*.2);
 return float4(saturate(color(uv)+glow*option(10)),1);
})HLSL";}
static std::string sharpen(){return std::string(common)+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float2 step=input_size.zw*option(7);float3 c=color(uv),blurred=(color(uv+float2(step.x,0))+color(uv-float2(step.x,0))+color(uv+float2(0,step.y))+color(uv-float2(0,step.y)))*.25;
 float limit=option(8);return float4(saturate(c+clamp((c-blurred)*option(6),-limit,limit)),1);
})HLSL";}
static std::string finish(){return std::string(common)+R"HLSL(
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float3 c=color(uv);float2 centered=uv-.5;
 if(option(37)>.5){float2 shift=centered*2*input_size.zw*option(38);c.r=color(uv+shift).r;c.b=color(uv-shift).b;}
 c*=exp2(option(17));float l=luminance(c);
 c+=option(25)*pow(1-saturate(l),2)*.25+option(26)*pow(saturate(l),2)*.25;
 c=(c-.5)*option(19)+.5+option(20);c=max(c+option(27),0)/option(28);
 c*=float3(1+option(23)*.12+option(24)*.04,1-option(24)*.08,1-option(23)*.12+option(24)*.04);
 float maxc=max(c.r,max(c.g,c.b)),minc=min(c.r,min(c.g,c.b));float saturation=option(21)*(1+option(22)*(1-saturate(maxc-minc)));c=lerp(luminance(c).xxx,c,saturation);
 c=pow(max(c,0),1/option(18));
 if(option(29)>.5&&option(29)<1.5)c=lerp(c,smoothstep(0,1,saturate(c)),.35);
 if(option(29)>1.5)c=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);
 if(option(30)>.5){float distance=length(centered*float2(output_size.x/output_size.y,1))*2;c*=1-smoothstep(option(32),option(32)+option(33),distance)*option(31);}
 if(option(34)>.5){float2 pixel=floor(position.xy/max(option(36),1));float noise=frac(sin(dot(pixel,float2(12.9898,78.233)))*43758.5453)-.5;c+=noise*option(35);}
 if((option(39)>.5&&option(39)<1.5&&uv.x<.5)||(option(39)>1.5&&uv.x>.5))c=original.SampleLevel(sample_image,uv,0).rgb;
 return float4(saturate(c),1);
})HLSL";}
}
