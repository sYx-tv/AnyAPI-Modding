// Scene SMAA 1x kernels, recorded at the native pre-HUD composition boundary.
// Official SMAA shader and lookup data are pinned in third_party/smaa.
#define SMAA_HLSL_4_1
#define SMAA_RT_METRICS render_metrics
// smoothing: x enhanced edge strength, y sharpening, z history weight, w history valid.
// The camera block is used only by the temporal pass; positions are relative
// to the current camera, previous_camera.w is unused.
cbuffer Frame : register(b0) {
    float4 render_metrics; float4 smoothing;
    float4 projection;   // A B C D: depth=(A*z+B)/(C*z+D)
    float4 right_axis;   // w: horizontal tangent of half field of view
    float4 up_axis;      // w: vertical tangent of half field of view
    float4 forward_axis; // w: maximum distance
    float4 previous_camera; float4 previous_right; float4 previous_up; float4 previous_forward;
};
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

// Temporal AA. t0: this frame's SMAA result, t1: previous history (rgb +
// view distance in alpha), t2: scene depth. Writes the new history.
float view_distance(float depth) {
    float denominator=depth*projection.z-projection.x;
    float z=abs(denominator)>.000001?(projection.y-depth*projection.w)/denominator:forward_axis.w;
    return clamp(abs(z),0,forward_axis.w);
}
float3 view_ray(float2 uv) {
    float2 ndc=uv*float2(2,-2)+float2(-1,1);
    return normalize(forward_axis.xyz+right_axis.xyz*ndc.x*right_axis.w+up_axis.xyz*ndc.y*up_axis.w);
}
float3 to_ycocg(float3 c){return float3(dot(c,float3(.25,.5,.25)),dot(c,float3(.5,0,-.5)),dot(c,float3(-.25,.5,-.25)));}
float3 from_ycocg(float3 c){return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
float4 Temporal(Varying v) : SV_Target {
    float3 current=source_image.SampleLevel(PointSampler,v.uv,0).rgb;
    float3 ray=view_ray(v.uv);
    float distance=min(view_distance(search_image.SampleLevel(PointSampler,v.uv,0).r)/max(dot(ray,forward_axis.xyz),.05),forward_axis.w);
    if(smoothing.w<.5)return float4(current,distance);
    // Colour box of the 3x3 neighbourhood in YCoCg; history is clipped into it.
    float3 low=to_ycocg(current),high=low;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){float3 s=to_ycocg(source_image.SampleLevel(PointSampler,v.uv+float2(x,y)*render_metrics.xy,0).rgb);low=min(low,s);high=max(high,s);}
    // World-static content: reproject through the previous camera.
    float3 relative=ray*distance-previous_camera.xyz;float z=dot(relative,previous_forward.xyz);
    float2 uv=float2(dot(relative,previous_right.xyz)/(max(z,.001)*previous_right.w),dot(relative,previous_up.xyz)/(max(z,.001)*previous_up.w))*float2(.5,-.5)+.5;
    float4 moved=area_or_weights.SampleLevel(LinearSampler,uv,0);float expected=length(relative);
    bool moved_ok=z>.01&&all(uv>=0)&&all(uv<=1)&&abs(moved.a-expected)<max(.05*expected,.15);
    // Camera-locked content (your own vehicle, held items) stays on the same pixel.
    float4 still=area_or_weights.SampleLevel(PointSampler,v.uv,0);
    bool still_ok=abs(still.a-distance)<max(.03*distance,.1);
    float weight=moved_ok||still_ok?smoothing.z:0;
    float3 history=from_ycocg(clamp(to_ycocg(moved_ok?moved.rgb:still.rgb),low,high));
    // Luminance weighting keeps bright specks from flickering through the blend.
    float wc=(1-weight)/(1+dot(current,float3(.299,.587,.114))),wh=weight/(1+dot(history,float3(.299,.587,.114)));
    return float4((current*wc+history*wh)/max(wc+wh,.0001),distance);
}
// Final copy to the scene target with optional contrast-adaptive sharpening
// (after AMD FidelityFX CAS): strongest on soft detail, limited near clipping.
float4 Output(Varying v) : SV_Target {
    float3 e=source_image.SampleLevel(PointSampler,v.uv,0).rgb;
    if(smoothing.y<=0)return float4(e,1);
    float3 b=source_image.SampleLevel(PointSampler,v.uv-float2(0,render_metrics.y),0).rgb;
    float3 h=source_image.SampleLevel(PointSampler,v.uv+float2(0,render_metrics.y),0).rgb;
    float3 d=source_image.SampleLevel(PointSampler,v.uv-float2(render_metrics.x,0),0).rgb;
    float3 f=source_image.SampleLevel(PointSampler,v.uv+float2(render_metrics.x,0),0).rgb;
    float3 low=min(min(min(b,h),min(d,f)),e),high=max(max(max(b,h),max(d,f)),e);
    float3 amount=sqrt(saturate(min(low,2-high)/max(high,.0001)));
    float3 w=amount*(-1/lerp(8,5,saturate(smoothing.y)));
    return float4(saturate((b*w+d*w+f*w+h*w+e)/(1+4*w)),1);
}
