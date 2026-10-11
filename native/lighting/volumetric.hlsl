// Experimental scene lighting. Input is HDR scene colour and native depth,
// before bloom, tone mapping and HUD. No colour-grading or screen filter controls.
cbuffer Frame : register(b0) {
    row_major float4x4 shadow_matrix;
    float4 camera;       // xyz: graphics-relative position; w: world height
    float4 right_axis;   // w: horizontal tangent of half field of view
    float4 up_axis;      // w: vertical tangent of half field of view
    float4 forward_axis;
    float4 projection;   // A B C D: depth=(A*z+B)/(C*z+D)
    float4 sunlight;     // xyz: direction towards the light; w: 0 off, 1 standard depth, 2 reversed depth
    float4 sun_colour;
    float4 medium;       // extinction, base height, height falloff, anisotropy
    float4 controls;     // sample count, maximum distance, shafts, fog strength
    float4 fog_colour;
};
Texture2D<float4> scene_colour : register(t0);
Texture2D<float> scene_depth : register(t1);
Texture2D<float> shadow_depth : register(t2);
Texture2D<float4> volume : register(t3);
Texture2D<float4> matrices : register(t4);
Texture2D<float> shadow1 : register(t5);
Texture2D<float> shadow2 : register(t6);
Texture2D<float> shadow3 : register(t7);
Texture2D<float> shadow4 : register(t8);
Texture2D<float> shadow5 : register(t9);
Texture2D<float> shadow6 : register(t10);
Texture2D<float> shadow7 : register(t11);
Texture2D<float> shadow8 : register(t12);
Texture2D<float> shadow9 : register(t13);
Texture2D<float> shadow10 : register(t14);
Texture2D<float> shadow11 : register(t15);
// Temporal history, ping-ponged at half resolution. Row 21 of the matrix
// texture says which one was written this frame.
Texture2D<float4> history_a : register(t16);
Texture2D<float4> history_b : register(t17);
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);
// Drawn once per cascade. Matrix rows travel in immutable command-list constants.
float4 Matrices(float4 position : SV_Position) : SV_Target {
    return shadow_matrix[(uint)position.x];
}
float shadow_sample(uint cascade,float2 uv) {
    switch(cascade) {
    case 0:return shadow_depth.SampleLevel(point_sampler,uv,0);
    case 1:return shadow1.SampleLevel(point_sampler,uv,0);
    case 2:return shadow2.SampleLevel(point_sampler,uv,0);
    case 3:return shadow3.SampleLevel(point_sampler,uv,0);
    case 4:return shadow4.SampleLevel(point_sampler,uv,0);
    case 5:return shadow5.SampleLevel(point_sampler,uv,0);
    case 6:return shadow6.SampleLevel(point_sampler,uv,0);
    case 7:return shadow7.SampleLevel(point_sampler,uv,0);
    case 8:return shadow8.SampleLevel(point_sampler,uv,0);
    case 9:return shadow9.SampleLevel(point_sampler,uv,0);
    case 10:return shadow10.SampleLevel(point_sampler,uv,0);
    default:return shadow11.SampleLevel(point_sampler,uv,0);
    }
}

// Matrix texture rows 20-21 carry per-frame data that no longer fits in the
// 56 root constants. All zero means the original 0.29 behaviour.
// Row 20: previous camera position (relative to this frame's origin) and
// history validity, previous right/up axes with half-FOV tangents, previous
// forward axis and the frame index.
// Row 21: clarity, beam reach (m, 0 = unlimited), sun-height response,
// temporal on; then history read selector and history weight.
float4 shaping(){return matrices.Load(int3(0,21,0));}
float4 temporal_controls(){return matrices.Load(int3(1,21,0));}

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v;v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float view_distance(float depth) {
    float denominator=depth*projection.z-projection.x;
    float z=abs(denominator)>.000001?(projection.y-depth*projection.w)/denominator:controls.y;
    return clamp(abs(z),0,controls.y);
}
float3 view_ray(float2 uv) {
    float2 ndc=uv*float2(2,-2)+float2(-1,1);
    return normalize(forward_axis.xyz+right_axis.xyz*ndc.x*right_axis.w+up_axis.xyz*ndc.y*up_axis.w);
}
float2 shadow_result(float3 position) {
    if(sunlight.w<.5)return float2(1,0);
    [loop]for(uint cascade=0;cascade<(uint)forward_axis.w;++cascade) {
        row_major float4x4 transform=float4x4(matrices.Load(int3(0,cascade,0)),matrices.Load(int3(1,cascade,0)),matrices.Load(int3(2,cascade,0)),matrices.Load(int3(3,cascade,0)));
        float4 clip=mul(float4(position,1),transform);
        if(abs(clip.w)<.000001)continue;
        float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
        // Leave an edge margin so a farther cascade takes over before clamping.
        if(any(uv<.002)||any(uv>.998)||p.z<0||p.z>1)continue;
        float stored=shadow_sample(cascade,uv);
        // Reversed orthographic depth spans kilometres: a large fixed bias
        // erases branch shadows. Use a small normalized-depth bias instead.
        return float2(sunlight.w>1.5?(p.z>=stored-.00002?1:0):(p.z<=stored+.00002?1:0),1);
    }
    return float2(1,0);
}
float visibility(float3 position) {return shadow_result(position).x;}
// One-shot low-resolution diagnostic, never composited into the scene.
float4 Probe(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=min(view_distance(scene_depth.SampleLevel(point_sampler,v.uv,0))/max(abs(dot(ray,forward_axis.xyz)),.05),controls.y);
    float2 result=0;
    [loop]for(uint i=0;i<32;++i){float a=float(i)/32,b=float(i+1)/32;result+=shadow_result(camera.xyz+ray*distance*(a*a+b*b)*.5);}
    return float4(result/32,distance/controls.y,1);
}
float3 local_illumination(float3 position,float3 ray) {
    float3 result=0;
    [loop]for(uint i=0;i<(uint)fog_colour.w;++i){
        float4 location=matrices.Load(int3(0,12+i,0));
        float3 delta=location.xyz-position;float distance=length(delta);
        if(distance>=location.w)continue;
        float3 toward_light=delta/max(distance,.001);
        float4 direction=matrices.Load(int3(1,12+i,0));
        float4 colour=matrices.Load(int3(2,12+i,0));
        float4 properties=matrices.Load(int3(3,12+i,0));
        float attenuation=pow(saturate(1-distance/location.w),2);
        if(colour.w>.5)attenuation*=smoothstep(direction.w,min(.9999,direction.w+.08),dot(direction.xyz,-toward_light));
        if(attenuation<=.00001)continue;
        float visible=1;
        if(properties.x>=8){uint cascade=(uint)properties.x;row_major float4x4 transform=float4x4(matrices.Load(int3(0,cascade,0)),matrices.Load(int3(1,cascade,0)),matrices.Load(int3(2,cascade,0)),matrices.Load(int3(3,cascade,0)));
            float4 clip=mul(float4(position,1),transform);if(clip.w<=.00001)continue;
            float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
            if(any(uv<0)||any(uv>1)||p.z<0||p.z>1)continue;
            float stored=shadow_sample(cascade,uv);visible=properties.y>1.5?(p.z>=stored-.00002?1:0):(p.z<=stored+.00002?1:0);
        }
        float g=medium.w,mu=dot(ray,toward_light);float phase=(1-g*g)/pow(max(1+g*g-2*g*mu,.001),1.5);
        result+=colour.rgb*attenuation*visible*phase*sun_colour.w;
    }
    return result;
}
float scene_distance(float2 uv,float3 ray) {
    float distance=view_distance(scene_depth.SampleLevel(point_sampler,uv,0));
    return min(distance/max(abs(dot(ray,normalize(forward_axis.xyz))),.05),controls.y);
}
float4 Integrate(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=scene_distance(v.uv,ray);
    uint count=(uint)clamp(controls.x,8,64);
    float4 shape=shaping();
    float g=clamp(medium.w,-.8,.8);float mu=dot(ray,normalize(sunlight.xyz));
    // Native sun_colour is the renderer's artistic direct-light value, not
    // radiometric intensity per steradian. Use an isotropic-relative phase
    // (g=0 -> 1) so shaft strength shares the native lighting scale. The old
    // 1/(4*pi) factor suppressed already small game sun values by 12.57x.
    float phase=(1-g*g)/pow(max(1+g*g-2*g*mu,.001),1.5);
    // Clarity mode caps the forward peak: Strong focus reached ~19x looking
    // into the sun, which turned the whole view white.
    if(shape.x>0)phase=min(phase,4);
    // Interleaved gradient noise, rotated each frame, moves the sample
    // positions so temporal blending removes banding instead of freezing it.
    bool temporal=shape.w>.5;
    float jitter=temporal?frac(52.9829189*frac(dot(v.position.xy,float2(.06711056,.00583715)))+matrices.Load(int3(3,20,0)).w*.61803399):.5;
    // Fog owns extinction and ambient haze. Beams use a fixed, clear-air
    // scattering medium so their brightness does not depend on the fog tier.
    float transmittance=1,beam_transmittance=1,local_transmittance=1;float3 scattered=0;
    float beam_lit=0,beam_total=0;
    [loop]for(uint i=0;i<count;++i) {
        // Quadratic spacing resolves nearby branch shadows without more samples.
        float a=float(i)/count,b=float(i+1)/count;
        float begin=distance*a*a,end=distance*b*b;
        float step=end-begin,d=temporal?distance*pow((i+jitter)/count,2):(begin+end)*.5;float3 position=camera.xyz+ray*d;
        float world_height=camera.w+ray.y*d;
        float density=medium.x*exp(-max(world_height-medium.y,0)*medium.z);
        float segment=exp(-density*controls.w*step);
        float fog_weight=transmittance*(1-segment);
        float beam_segment=exp(-.002*step);
        float beam_weight=transmittance*beam_transmittance*(1-beam_segment);
        // Beam reach fades distant clear air, which otherwise piles up into
        // a uniform veil over everything far away.
        if(shape.y>0)beam_weight*=exp(-d/shape.y);
        float local_segment=exp(-.02*step);
        float local_weight=transmittance*local_transmittance*(1-local_segment);
        float lit=visibility(position);
        beam_lit+=lit*beam_weight;beam_total+=beam_weight;
        // V3 lights the fog itself: shadowed air (under a canopy, behind a
        // hill) is darker and sunlit air brighter, at the same average level.
        // A flat fog colour read as an evenly milky wall.
        float fog_light=shape.x>0?lerp(.45,1.35,lit):1;
        scattered+=fog_colour.rgb*fog_weight*fog_light+local_illumination(position,ray)*local_weight;
        transmittance*=segment;beam_transmittance*=beam_segment;local_transmittance*=local_segment;
        if(transmittance<.001)break;
    }
    // Clarity keeps beams where the ray crosses shadow edges (gaps in trees,
    // windows, terrain) and thins the glow of rays that are lit end to end,
    // which is what read as fog. 0 keeps the original uniform scattering.
    float shadowed=1-beam_lit/max(beam_total,.000001);
    float beams=beam_lit*lerp(1-saturate(shape.x),1,smoothstep(.02,.3,shadowed));
    // Sun-height response: full beams at sunrise and sunset, a third at noon,
    // none below the horizon, and a warm tint while the sun is low.
    float3 sun=sun_colour.rgb;
    if(shape.z>.5){float elevation=normalize(sunlight.xyz).y;sun*=lerp(1,.35,smoothstep(.35,.9,elevation))*smoothstep(-.04,.06,elevation)*lerp(float3(1,.78,.55),float3(1,1,1),smoothstep(.06,.45,elevation));}
    scattered+=sun*controls.z*phase*beams;
    return float4(scattered,transmittance);
}
// Blends this frame's noisy half-resolution result with reprojected history.
// The camera reprojection uses scene depth; the 3x3 neighbourhood clamp
// rejects history that no longer matches (moving objects, disocclusion).
float4 Accumulate(Varying v) : SV_Target {
    float4 current=volume.SampleLevel(point_sampler,v.uv,0);
    float4 previous_camera=matrices.Load(int3(0,20,0)),control=temporal_controls();
    if(previous_camera.w<.5)return current;
    float4 previous_right=matrices.Load(int3(1,20,0)),previous_up=matrices.Load(int3(2,20,0)),previous_forward=matrices.Load(int3(3,20,0));
    float3 ray=view_ray(v.uv);float3 relative=camera.xyz+ray*scene_distance(v.uv,ray)-previous_camera.xyz;
    float z=dot(relative,previous_forward.xyz);if(z<=.01)return current;
    float2 uv=float2(dot(relative,previous_right.xyz)/(z*previous_right.w),dot(relative,previous_up.xyz)/(z*previous_up.w))*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1))return current;
    float4 history=control.x>.5?history_b.SampleLevel(linear_sampler,uv,0):history_a.SampleLevel(linear_sampler,uv,0);
    uint w,h;volume.GetDimensions(w,h);float2 texel=1/float2(w,h);float4 low=current,high=current;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){float4 s=volume.SampleLevel(point_sampler,v.uv+float2(x,y)*texel,0);low=min(low,s);high=max(high,s);}
    return lerp(current,clamp(history,low,high),saturate(control.y));
}
float4 resolved_lighting(float2 uv) {
    if(shaping().w<.5)return volume.SampleLevel(point_sampler,uv,0);
    // The selector names the history that was read, so the other one holds this frame.
    return temporal_controls().x>.5?history_a.SampleLevel(point_sampler,uv,0):history_b.SampleLevel(point_sampler,uv,0);
}
float4 Composite(Varying v) : SV_Target {
    uint w,h;volume.GetDimensions(w,h);float2 pixel=v.uv*float2(w,h)-.5;
    float2 base=floor(pixel),fraction=frac(pixel);
    float center_distance=view_distance(scene_depth.SampleLevel(point_sampler,v.uv,0));
    float4 accumulated=0;float total=0;
    [unroll]for(uint y=0;y<2;++y)[unroll]for(uint x=0;x<2;++x) {
        float2 uv=(base+float2(x,y)+.5)/float2(w,h);
        float neighbour=view_distance(scene_depth.SampleLevel(point_sampler,uv,0));
        float bilinear=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
        float weight=bilinear*exp(-abs(center_distance-neighbour)/max(.1,center_distance*.02));
        accumulated+=resolved_lighting(uv)*weight;total+=weight;
    }
    float4 lighting=total>.00001?accumulated/total:resolved_lighting(v.uv);
    float4 original=scene_colour.SampleLevel(linear_sampler,v.uv,0);
    return float4(original.rgb*lighting.a+lighting.rgb,original.a);
}
