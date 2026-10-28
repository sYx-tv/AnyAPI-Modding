// Scene SMAA 1x kernels, recorded at the native pre-HUD composition boundary.
// Official SMAA shader and lookup data are pinned in third_party/smaa.
#define SMAA_HLSL_4_1
#define SMAA_RT_METRICS render_metrics
// smoothing: x enhanced edge strength, y sharpening, z history weight, w history valid.
// The camera block is used only by the temporal pass; positions are relative
// to the current camera. previous_camera.w and previous_forward.w carry this
// frame's TAA jitter in uv (0 when the camera was not jittered).
// options: x flags (asuint; TAA_*), y camera height above sea level (m), z output dither in LSB, w 0.
cbuffer Frame : register(b0) {
    float4 render_metrics; float4 smoothing;
    float4 projection;   // A B C D: depth=(A*z+B)/(C*z+D)
    float4 right_axis;   // w: horizontal tangent of half field of view
    float4 up_axis;      // w: vertical tangent of half field of view
    float4 forward_axis; // w: maximum distance
    float4 previous_camera; float4 previous_right; float4 previous_up; float4 previous_forward;
    float4 options;
};
#define TAA_CATMULL_ROM 1u
#define TAA_STILL_V2 2u
#define TAA_THIN_RESCUE 4u
#define TAA_OCEAN 256u
#define TAA_RIM 512u
#include "../third_party/smaa/SMAA.hlsl"
Texture2D source_image : register(t0);
Texture2D area_or_weights : register(t1);
Texture2D search_image : register(t2);
Texture2D ocean_image : register(t3);  // gbuffer_ocean: y > 0 = ocean (flag only; the ocean writes no depth)
Texture2D albedo_image : register(t4); // gbuffer_0: w = rim flag (vehicles and parts, tracks, vegetation, clothing, rope)
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
float3 undither_finish(float2 pixel,float3 c,float3 mean,float sigma){float3 luma=float3(.299,.587,.114);
    float centre=dot(mean,luma),spread=0;
    spread=max(spread,abs(dot(box4(pixel+float2(-4,-2)),luma)-centre));spread=max(spread,abs(dot(box4(pixel+float2(0,-2)),luma)-centre));
    spread=max(spread,abs(dot(box4(pixel+float2(-2,-4)),luma)-centre));spread=max(spread,abs(dot(box4(pixel+float2(-2,0)),luma)-centre));
    float k=saturate((sigma-.012)*40)*saturate((1-spread/(.2*sigma+.0001))*2);
    return lerp(c,mean,k);}
// Test reference only (scene_smaa_gpu_test UnditherProbe): the 0.40.1 kernel, 16 point taps.
float3 undither_reference(float2 uv,float3 c){
    float2 p=render_metrics.xy,pixel=floor(uv/p);float3 luma=float3(.299,.587,.114);
    float3 mean=0;float m1=0,m2=0;
    [unroll]for(int y=-2;y<2;++y)[unroll]for(int x=-2;x<2;++x){float3 t=source_image.SampleLevel(PointSampler,(pixel+float2(x,y)+.5)*p,0).rgb;mean+=t;float l=dot(t,luma);m1+=l;m2+=l*l;}
    mean/=16;m1/=16;float sigma=sqrt(max(m2/16-m1*m1,0));
    return undither_finish(pixel,c,mean,sigma);}
// AA-3: the same 16 texels (pixel-2..pixel+1) from gathers at the four 2x2 block
// centres. A 4x4 period whose green varies by less than UNDITHER_FLAT holds no
// screen-door dither: return c after 4 gathers. This matches the full kernel
// (k == 0) unless red or blue alone vary by more than about 7/255 (chroma-only
// dither). Otherwise R and B complete the moments (12 gathers), and the 16
// bilinear spread taps run only where k can be non-zero.
#define UNDITHER_FLAT .02
float3 undither(float2 uv,float3 c,out float early){
    float2 p=render_metrics.xy,pixel=floor(uv/p);float3 luma=float3(.299,.587,.114);
    float2 at[4]={(pixel+float2(-1,-1))*p,(pixel+float2(1,-1))*p,(pixel+float2(-1,1))*p,(pixel+float2(1,1))*p};
    float4 g[4];float lo=1e9,hi=-1e9;
    [unroll]for(int i=0;i<4;++i){g[i]=source_image.GatherGreen(PointSampler,at[i]);lo=min(lo,min(min(g[i].x,g[i].y),min(g[i].z,g[i].w)));hi=max(hi,max(max(g[i].x,g[i].y),max(g[i].z,g[i].w)));}
    early=1;
    [branch]if(hi-lo<UNDITHER_FLAT)return c;
    float3 sum=0;float m1=0,m2=0;
    [unroll]for(int j=0;j<4;++j){float4 r=source_image.GatherRed(PointSampler,at[j]),b=source_image.GatherBlue(PointSampler,at[j]);
        float4 l=r*luma.x+g[j]*luma.y+b*luma.z;sum+=float3(dot(r,1),dot(g[j],1),dot(b,1));m1+=dot(l,1);m2+=dot(l,l);}
    float3 mean=sum/16;m1/=16;float sigma=sqrt(max(m2/16-m1*m1,0));
    early=.5;
    [branch]if(sigma<=.012)return c; // k == saturate((sigma-.012)*40)*.. == 0 exactly
    early=0;
    return undither_finish(pixel,c,mean,sigma);}
float3 undither(float2 uv,float3 c){float early;return undither(uv,c,early);}
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
// Test probe (scene_smaa_gpu_test only): rgb = new undither - 0.40.1 undither on t0, a = 1 / .5 / 0 for the early-out taken.
float4 UnditherProbe(Varying v) : SV_Target {
    float3 c=source_image.SampleLevel(PointSampler,v.uv,0).rgb;float early;
    return float4(undither(v.uv,c,early)-undither_reference(v.uv,c),early);
}

// Temporal AA. t0: this frame's SMAA result, t1: previous history (rgb +
// view distance in alpha), t2: scene depth, t3/t4: ocean and rim flags. Writes the new history.
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
uint taa_flags(){return asuint(options.x);}
// K05: the ocean writes no depth, so under ocean pixels the depth is the sea
// floor or the sky. There the surface is the sea plane (world y = 0) under the
// camera: the ray distance is height / -ray.y. Any surface above the sea is
// closer along the ray than the plane, so min() only changes ocean pixels.
float pixel_distance(float2 uv){
    float3 r=view_ray(uv);
    float d=min(view_distance(search_image.SampleLevel(PointSampler,uv,0).r)/max(dot(r,forward_axis.xyz),.05),forward_axis.w);
    // HLSL && does not short-circuit: the fetch sits inside the branch, so it runs only for downward rays with the ocean bound.
    [branch]if((taa_flags()&TAA_OCEAN)&&r.y<-.00001){if(ocean_image.SampleLevel(PointSampler,uv,0).y>0)d=min(d,options.y/-r.y);}
    return d;
}
float2 previous_uv(float3 relative,out float z){z=dot(relative,previous_forward.xyz);return float2(dot(relative,previous_right.xyz)/(max(z,.001)*previous_right.w),dot(relative,previous_up.xyz)/(max(z,.001)*previous_up.w))*float2(.5,-.5)+.5;}
// Clips toward the box centre instead of clamping each channel, so rejected
// history keeps its hue relationship rather than turning into a bright fringe.
float3 clip_box(float3 h,float3 low,float3 high){float3 c=(low+high)*.5,e=(high-low)*.5+.0001,d=h-c;float3 a=abs(d/e);float m=max(a.x,max(a.y,a.z));return m>1?c+d/m:h;}
// AA-8: 5-tap bilinear-optimised Catmull-Rom (corner taps dropped) for the
// history colour. Over- and undershoot are removed by clip_box afterwards.
float3 history_catmull_rom(float2 uv){
    float2 size=render_metrics.zw,position=uv*size,centre=floor(position-.5)+.5,f=position-centre;
    float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
    float2 w12=w1+w2,t0=(centre-1)*render_metrics.xy,t3=(centre+2)*render_metrics.xy,t12=(centre+w2/w12)*render_metrics.xy;
    float3 c=area_or_weights.SampleLevel(LinearSampler,float2(t12.x,t0.y),0).rgb*(w12.x*w0.y)
            +area_or_weights.SampleLevel(LinearSampler,float2(t0.x,t12.y),0).rgb*(w0.x*w12.y)
            +area_or_weights.SampleLevel(LinearSampler,t12,0).rgb*(w12.x*w12.y)
            +area_or_weights.SampleLevel(LinearSampler,float2(t3.x,t12.y),0).rgb*(w3.x*w12.y)
            +area_or_weights.SampleLevel(LinearSampler,float2(t12.x,t3.y),0).rgb*(w12.x*w3.y);
    float total=w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y;
    return max(c/max(total,.0001),0);
}
// AA-7: a pixel that keeps no history shows this frame's SMAA result moved
// back by the camera jitter (bilinear), so rejected edges stop vibrating.
float3 recentred(float2 uv,float3 c){float2 j=float2(previous_camera.w,previous_forward.w);return any(j!=0)?source_image.SampleLevel(LinearSampler,uv+j,0).rgb:c;}
float4 temporal(Varying v){
    float3 current=source_image.SampleLevel(PointSampler,v.uv,0).rgb; // AA-3: t0 is Resolve's output, already undithered
    float distance=pixel_distance(v.uv);
    if(smoothing.w<.5)return float4(recentred(v.uv,current),distance);
    uint flags=taa_flags();
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
    // somewhere in the reprojected 3x3 history footprint (four gathers).
    float2 half_pixel=.5*render_metrics.xy;float4 e=expected;
    float4 taps=min(min(abs(area_or_weights.GatherAlpha(PointSampler,uv-half_pixel)-e),abs(area_or_weights.GatherAlpha(PointSampler,uv+half_pixel)-e)),min(abs(area_or_weights.GatherAlpha(PointSampler,uv+float2(half_pixel.x,-half_pixel.y))-e),abs(area_or_weights.GatherAlpha(PointSampler,uv+float2(-half_pixel.x,half_pixel.y))-e)));
    float best=min(min(taps.x,taps.y),min(taps.z,taps.w));
    // Slope-aware tolerance: the smaller one-sided distance step on each axis
    // follows grazing ground (many % per pixel far away) but not a silhouette.
    float slope=max(min(abs(around[3]-distance),abs(around[5]-distance)),min(abs(around[1]-distance),abs(around[7]-distance)));
    float tolerance=max(.03*expected,.1)+2*slope;
    bool on_screen=all(uv>=0)&&all(uv<=1);
    bool moved_ok=z>.01&&own_z>.01&&on_screen&&best<tolerance;
    float3 moved=area_or_weights.SampleLevel(LinearSampler,uv,0).rgb;
    [branch]if(moved_ok&&(flags&TAA_CATMULL_ROM))moved=history_catmull_rom(uv);
    // Camera-locked content (your own vehicle, held items) stays on the same
    // pixel. Only nearby surfaces qualify, so distant foliage cannot pick up
    // whatever happened to be on that pixel last frame.
    float4 still=area_or_weights.SampleLevel(PointSampler,v.uv,0);
    float still_error=abs(still.a-distance),still_tolerance=max(.02*distance,.05);
    bool use_still;
    [branch]if(flags&TAA_STILL_V2){
        float rim=0;[branch]if(flags&TAA_RIM)rim=albedo_image.SampleLevel(PointSampler,v.uv,0).w;
        // aa-M2: same-pixel history only where the world-static reprojection stayed
        // on screen (vehicles excepted). AA-6: vehicles qualify up to 80 m.
        bool still_ok=distance<(rim>.5?80:30)&&(on_screen||rim>.5)&&still_error<still_tolerance;
        // AA-6: a tight camera-locked match beats a loose world match that is
        // further from what this pixel shows now.
        use_still=still_ok&&(!moved_ok||(best>.5*tolerance&&still_error<.25*still_tolerance&&length(to_ycocg(still.rgb)-mean)<length(to_ycocg(moved)-mean)));
    } else use_still=!moved_ok&&distance<30&&still_error<still_tolerance;
    float weight=moved_ok||use_still?smoothing.z:0;
    // Fast motion keeps a little more of the current frame to limit smearing.
    weight*=lerp(1,.85,saturate(length(motion/render_metrics.xy)/24));
    if(weight<=0)return float4(recentred(v.uv,current),distance);
    float3 history=from_ycocg(clip_box(to_ycocg(use_still?still.rgb:moved),low,high));
    // aa-M1: a thin bright feature that is new on this pixel (a local outlier the
    // 4 history texels around uv do not contain) keeps at least half of itself.
    bool rescue=false;
    [branch]if((flags&TAA_THIN_RESCUE)&&!use_still){
        float yc=to_ycocg(current).x;
        if(yc>=mean.x+sigma.x){float4 hy=.25*area_or_weights.GatherRed(PointSampler,uv)+.5*area_or_weights.GatherGreen(PointSampler,uv)+.25*area_or_weights.GatherBlue(PointSampler,uv);
            if(yc-max(max(hy.x,hy.y),max(hy.z,hy.w))>max(.5*(high.x-low.x),.08)){weight=min(weight,.5);rescue=true;}}
    }
    // Luminance weighting keeps bright specks from flickering through the blend.
    float wc=1-weight,wh=weight;
    if(!rescue){wc/=1+dot(current,float3(.299,.587,.114));wh/=1+dot(history,float3(.299,.587,.114));}
    return float4((current*wc+history*wh)/max(wc+wh,.0001),distance);
}
// FIN-4: static triangular dither of about options.z LSB before the 8-bit
// back buffer, scaled down to nothing at black and white. Never in the history.
float ign(float2 p){return frac(52.9829189*frac(dot(p,float2(.06711056,.00583715))));}
float3 dither8(float2 position,float3 c){
    [branch]if(options.z<=0)return c;
    float2 p=floor(position);float n=ign(p+float2(29,61))+ign(p+float2(83,7))-1;
    float3 room=saturate(c*255)*saturate((1-c)*255);
    return c+n*room*(options.z/255);
}
float4 Temporal(Varying v) : SV_Target { return temporal(v); }
// AA-15: sharpening off, so the output pass would be a plain copy. Write the
// history and the back buffer in one pass. The back buffer gets the colour the
// history stores (rounded to half like the RGBA16F target), so this is the
// same image the separate output pass would write.
struct TemporalTargets { float4 history : SV_Target0; float4 display : SV_Target1; };
TemporalTargets TemporalMRT(Varying v) {
    TemporalTargets t;t.history=temporal(v);
    t.display=float4(dither8(v.position.xy,f16tof32(f32tof16(t.history.rgb))),1);return t;
}
// Final copy to the scene target with optional contrast-adaptive sharpening
// (after AMD FidelityFX CAS): strongest on soft detail, limited near clipping.
float4 Output(Varying v) : SV_Target {
    float3 e=source_image.SampleLevel(PointSampler,v.uv,0).rgb;
    if(smoothing.y<=0)return float4(dither8(v.position.xy,e),1);
    float3 b=source_image.SampleLevel(PointSampler,v.uv-float2(0,render_metrics.y),0).rgb;
    float3 h=source_image.SampleLevel(PointSampler,v.uv+float2(0,render_metrics.y),0).rgb;
    float3 d=source_image.SampleLevel(PointSampler,v.uv-float2(render_metrics.x,0),0).rgb;
    float3 f=source_image.SampleLevel(PointSampler,v.uv+float2(render_metrics.x,0),0).rgb;
    float3 low=min(min(min(b,h),min(d,f)),e),high=max(max(max(b,h),max(d,f)),e);
    float3 amount=sqrt(saturate(min(low,1-high)/max(high,.0001)));
    float3 w=amount*(-1/lerp(8,5,saturate(smoothing.y)));
    return float4(dither8(v.position.xy,saturate((b*w+d*w+f*w+h*w+e)/(1+4*w))),1);
}
