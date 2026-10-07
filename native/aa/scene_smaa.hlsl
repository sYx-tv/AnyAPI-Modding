// Scene SMAA 1x kernels, recorded at the native pre-HUD composition boundary.
// Official SMAA shader and lookup data are pinned in third_party/smaa.
#define SMAA_HLSL_4_1
#define SMAA_RT_METRICS render_metrics
cbuffer Frame : register(b0) { float4 render_metrics; float4 smoothing; };
#include "../third_party/smaa/SMAA.hlsl"
Texture2D source_image : register(t0);
Texture2D area_or_weights : register(t1);
Texture2D search_image : register(t2);
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float2 Edges(Varying v) : SV_Target {
    float4 offset[3]; SMAAEdgeDetectionVS(v.uv,offset);
    return SMAAColorEdgeDetectionPS(v.uv,offset,source_image);
}
float4 Weights(Varying v) : SV_Target {
    float2 pixel;float4 offset[3];SMAABlendingWeightCalculationVS(v.uv,pixel,offset);
    return SMAABlendingWeightCalculationPS(v.uv,pixel,offset,source_image,area_or_weights,search_image,float4(0,0,0,0));
}
float4 Resolve(Varying v) : SV_Target {
    float4 offset;SMAANeighborhoodBlendingVS(v.uv,offset);
    float4 result=SMAANeighborhoodBlendingPS(v.uv,offset,source_image,area_or_weights);
    // Optional extra spatial edge treatment. Never samples HUD pixels, never
    // accumulates history, and leaves flat regions and straight edges alone.
    if(smoothing.x>0) {
        float3 n=source_image.SampleLevel(LinearSampler,v.uv-float2(0,render_metrics.y),0).rgb;
        float3 s=source_image.SampleLevel(LinearSampler,v.uv+float2(0,render_metrics.y),0).rgb;
        float3 w=source_image.SampleLevel(LinearSampler,v.uv-float2(render_metrics.x,0),0).rgb;
        float3 e=source_image.SampleLevel(LinearSampler,v.uv+float2(render_metrics.x,0),0).rgb;
        float3 luma=float3(.299,.587,.114);
        float N=dot(n,luma),S=dot(s,luma),W=dot(w,luma),E=dot(e,luma),C=dot(result.rgb,luma);
        float range=max(max(N,S),max(W,E))-min(min(N,S),min(W,E));
        float dx=abs(W-E),dy=abs(N-S);
        float diagonal=saturate(min(dx,dy)/max(max(dx,dy),.0001)*2);
        float3 tangent=dy>=dx?(w+e)*.5:(n+s)*.5;
        float strength=smoothing.x*diagonal*saturate((range-.03)*8);
        result.rgb=lerp(result.rgb,tangent,strength);
    }
    return result;
}
