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
// The game draws its clouds and distant grass with a fixed 4x4 ordered
// (Bayer) screen-door dither for transparency. It never moves, so TAA cannot
// average it away and sharpening makes it worse. Any 4x4-periodic pattern has
// the same 4x4 box average wherever the box sits, while real edges and
// texture do not: where the pixels vary a lot but the box averages two pixels
// either side agree, replace the pixel with the box average.
float3 box4(float2 origin){float2 p=render_metrics.xy;float3 sum=0;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x)sum+=source_image.SampleLevel(LinearSampler,(origin+float2(1+2*x,1+2*y))*p,0).rgb;
    return sum*.25;}
float3 undither(float2 uv,float3 c){
    float2 p=render_metrics.xy,pixel=floor(uv/p);float3 luma=float3(.299,.587,.114);
    float3 mean=0;float m1=0,m2=0;
    [unroll]for(int y=-2;y<2;++y)[unroll]for(int x=-2;x<2;++x){float3 t=source_image.SampleLevel(PointSampler,(pixel+float2(x,y)+.5)*p,0).rgb;mean+=t;float l=dot(t,luma);m1+=l;m2+=l*l;}
    mean/=16;m1/=16;float sigma=sqrt(max(m2/16-m1*m1,0));
    float centre=dot(mean,luma),spread=0;
    spread=max(spread,abs(dot(box4(pixel+float2(-4,-2)),luma)-centre));spread=max(spread,abs(dot(box4(pixel+float2(0,-2)),luma)-centre));
    spread=max(spread,abs(dot(box4(pixel+float2(-2,-4)),luma)-centre));spread=max(spread,abs(dot(box4(pixel+float2(-2,0)),luma)-centre));
    float k=saturate((sigma-.012)*40)*saturate((1-spread/(.2*sigma+.0001))*2);
    return lerp(c,mean,k);
}
float4 Resolve(Varying v) : SV_Target {
    float4 offset;SMAANeighborhoodBlendingVS(v.uv,offset);
    float4 result=SMAANeighborhoodBlendingPS(v.uv,offset,source_image,area_or_weights);
    result.rgb=undither(v.uv,result.rgb);
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
float pixel_distance(float2 uv){return min(view_distance(search_image.SampleLevel(PointSampler,uv,0).r)/max(dot(view_ray(uv),forward_axis.xyz),.05),forward_axis.w);}
float2 previous_uv(float3 relative,out float z){z=dot(relative,previous_forward.xyz);return float2(dot(relative,previous_right.xyz)/(max(z,.001)*previous_right.w),dot(relative,previous_up.xyz)/(max(z,.001)*previous_up.w))*float2(.5,-.5)+.5;}
// Clips toward the box centre instead of clamping each channel, so rejected
// history keeps its hue relationship rather than turning into a bright fringe.
float3 clip_box(float3 h,float3 low,float3 high){float3 c=(low+high)*.5,e=(high-low)*.5+.0001,d=h-c;float3 a=abs(d/e);float m=max(a.x,max(a.y,a.z));return m>1?c+d/m:h;}
float4 Temporal(Varying v) : SV_Target {
    float3 current=undither(v.uv,source_image.SampleLevel(PointSampler,v.uv,0).rgb);
    float distance=pixel_distance(v.uv);
    if(smoothing.w<.5)return float4(current,distance);
    // 3x3 neighbourhood: min/max box and mean/variance in YCoCg, plus the
    // closest depth so silhouettes reproject with the foreground's motion.
    float3 low=to_ycocg(current),high=low,m1=0,m2=0;float nearest=distance;float2 nearest_uv=v.uv;float around[9];
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){float2 o=v.uv+float2(x,y)*render_metrics.xy;float3 s=to_ycocg(source_image.SampleLevel(PointSampler,o,0).rgb);low=min(low,s);high=max(high,s);m1+=s;m2+=s*s;float d=pixel_distance(o);around[(y+1)*3+x+1]=d;if(d<nearest){nearest=d;nearest_uv=o;}}
    // Variance clipping (mean +/- 1.25 sigma) inside the min/max box. A plain
    // min/max box at a tree edge contains the sky, so sky history survived.
    float3 mean=m1/9,sigma=sqrt(max(m2/9-mean*mean,0));low=max(low,mean-1.25*sigma);high=min(high,mean+1.25*sigma);
    // World-static content: reproject through the previous camera.
    float z,own_z;float3 near_point=view_ray(nearest_uv)*nearest-previous_camera.xyz;float2 motion=previous_uv(near_point,z)-nearest_uv;
    float3 own=view_ray(v.uv)*distance-previous_camera.xyz;previous_uv(own,own_z);float expected=length(near_point);
    float2 uv=v.uv+motion;
    // Closest-depth test: the nearest surface of this 3x3 footprint must appear
    // somewhere in the reprojected 3x3 history footprint (four gathers). Jitter
    // moves a silhouette across the centre pixel, and requiring every bilinear
    // tap to match rejected history on almost every edge. Colour clipping
    // below is the ghosting guard; this only rejects true disocclusion.
    float2 half_pixel=.5*render_metrics.xy;float4 e=expected;
    float4 taps=min(min(abs(area_or_weights.GatherAlpha(PointSampler,uv-half_pixel)-e),abs(area_or_weights.GatherAlpha(PointSampler,uv+half_pixel)-e)),min(abs(area_or_weights.GatherAlpha(PointSampler,uv+float2(half_pixel.x,-half_pixel.y))-e),abs(area_or_weights.GatherAlpha(PointSampler,uv+float2(-half_pixel.x,half_pixel.y))-e)));
    // Slope-aware tolerance: the smaller one-sided distance step on each axis
    // follows grazing ground (many % per pixel far away) but not a silhouette.
    float slope=max(min(abs(around[3]-distance),abs(around[5]-distance)),min(abs(around[1]-distance),abs(around[7]-distance)));
    float tolerance=max(.03*expected,.1)+2*slope;
    bool moved_ok=z>.01&&own_z>.01&&all(uv>=0)&&all(uv<=1)&&min(min(taps.x,taps.y),min(taps.z,taps.w))<tolerance;
    float4 moved=area_or_weights.SampleLevel(LinearSampler,uv,0);
    // Camera-locked content (your own vehicle, held items) stays on the same
    // pixel. Only nearby surfaces qualify, so distant foliage cannot pick up
    // whatever happened to be on that pixel last frame.
    float4 still=area_or_weights.SampleLevel(PointSampler,v.uv,0);
    bool still_ok=!moved_ok&&distance<30&&abs(still.a-distance)<max(.02*distance,.05);
    float weight=moved_ok||still_ok?smoothing.z:0;
    // Fast motion keeps a little more of the current frame to limit smearing.
    weight*=lerp(1,.85,saturate(length(motion/render_metrics.xy)/24));
    float3 history=from_ycocg(clip_box(to_ycocg(moved_ok?moved.rgb:still.rgb),low,high));
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
    float3 amount=sqrt(saturate(min(low,1-high)/max(high,.0001)));
    float3 w=amount*(-1/lerp(8,5,saturate(smoothing.y)));
    return float4(saturate((b*w+d*w+f*w+h*w+e)/(1+4*w)),1);
}
