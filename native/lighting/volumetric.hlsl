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
// gbuffer_ocean (renderer+0x208), or a 1x1 zero texture. .y>0 marks an ocean pixel: the ocean writes no
// depth, so scene_depth there holds the sea floor or the sky.
Texture2D<float4> ocean : register(t18);
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
// The four texels around uv: a 2x2 PCF footprint for one gather, so foliage
// shadows give a coverage fraction instead of binary speckle.
float4 shadow_gather(uint cascade,float2 uv) {
    switch(cascade) {
    case 0:return shadow_depth.Gather(point_sampler,uv);
    case 1:return shadow1.Gather(point_sampler,uv);
    case 2:return shadow2.Gather(point_sampler,uv);
    case 3:return shadow3.Gather(point_sampler,uv);
    case 4:return shadow4.Gather(point_sampler,uv);
    case 5:return shadow5.Gather(point_sampler,uv);
    case 6:return shadow6.Gather(point_sampler,uv);
    default:return shadow7.Gather(point_sampler,uv);
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
// Row 21, AnyAPI 0.40.2 (all zero keeps the earlier output). temporal_controls().z: lamp scattering scale
// (0 or 1 = clear air). atmosphere(): x sea mode (0 off, 1 lamp light stops at the sea surface, 2 the whole
// ray does), y sea level (world height, m), z 1 = cap the sun phase at 4 without clarity. Texel 3: rgb glow
// added to fogged air while lightning flashes.
float4 atmosphere(){return matrices.Load(int3(2,21,0));}
float3 flash_glow(){return matrices.Load(int3(3,21,0)).rgb;}

// Some GPUs (AMD here) can produce a NaN or infinity where others quietly give
// zero. One such pixel poisons bloom and eye adaptation and blows out the
// whole screen. FXC may fold isnan away, so test the exponent bits. Finite values
// pass through unchanged, so a frame without a bad value is bit-identical.
// The mask form compiles under FXC and DXC alike: bad=1 clears every bit (+0), bad=0 keeps v bit for bit.
float3 finite3(float3 v){uint3 bad=(asuint(v)&0x7f800000)==0x7f800000;return asfloat(asuint(v)&(bad-1));}
float4 finite4(float4 v){uint bad=(asuint(v.a)&0x7f800000)==0x7f800000;return float4(finite3(v.rgb),bad?1:v.a);}
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
// Along-ray distance to the sea plane on an ocean pixel seen from above it; 1e30 when there is no limit.
float sea_distance(float2 uv,float3 ray,float4 atm) {
    [branch]if(atm.x<.5)return 1e30;   // uniform: legacy frames never sample the ocean
    float h=camera.w-atm.y;
    return ocean.SampleLevel(point_sampler,uv,0).y>0&&ray.y<-.0001&&h>0?h/-ray.y:1e30;
}
// View-space depth, ending at the sea plane on ocean pixels in sea mode 2. The unnormalised ray has
// dot(r,forward)=1, so h/-r.y is already view z.
float surface_z(float2 uv,float4 atm) {
    float z=view_distance(scene_depth.SampleLevel(point_sampler,uv,0));
    [branch]if(atm.x>1.5){float2 ndc=uv*float2(2,-2)+float2(-1,1);float ry=forward_axis.y+right_axis.y*ndc.x*right_axis.w+up_axis.y*ndc.y*up_axis.w;float h=camera.w-atm.y;
        if(ocean.SampleLevel(point_sampler,uv,0).y>0&&ry<-.0001&&h>0)z=min(z,h/-ry);}
    return z;
}
// x: lit fraction (1 where no cascade covers); y: covered; z: beam weight, which
// fades to 0 over the last cascade's outer edge. Air past every cascade has no
// shadow information; counted as lit it formed a wall and glowed behind hills.
float3 shadow_result(float3 position) {
    if(sunlight.w<.5)return float3(1,0,1);
    uint cascades=(uint)forward_axis.w;
    [loop]for(uint cascade=0;cascade<cascades;++cascade) {
        row_major float4x4 transform=float4x4(matrices.Load(int3(0,cascade,0)),matrices.Load(int3(1,cascade,0)),matrices.Load(int3(2,cascade,0)),matrices.Load(int3(3,cascade,0)));
        float4 clip=mul(float4(position,1),transform);
        if(abs(clip.w)<.000001)continue;
        float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
        // Leave an edge margin so a farther cascade takes over before clamping.
        if(any(uv<.002)||any(uv>.998)||p.z<0||p.z>1)continue;
        float4 stored=shadow_gather(cascade,uv);
        // Reversed orthographic depth spans kilometres: a large fixed bias
        // erases branch shadows. Use a small normalized-depth bias instead.
        float4 lit=sunlight.w>1.5?step(stored-.00002,p.z):step(p.z,stored+.00002);
        float2 edge=min(uv,1-uv);
        return float3(dot(lit,.25),1,cascade+1<cascades?1:smoothstep(.002,.08,min(edge.x,edge.y)));
    }
    return float3(1,0,0);
}
// One-shot low-resolution diagnostic, never composited into the scene.
float4 Probe(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=min(view_distance(scene_depth.SampleLevel(point_sampler,v.uv,0))/max(abs(dot(ray,forward_axis.xyz)),.05),controls.y);
    float2 result=0;
    [loop]for(uint i=0;i<32;++i){float a=float(i)/32,b=float(i+1)/32;result+=shadow_result(camera.xyz+ray*distance*(a*a+b*b)*.5).xy;}
    return float4(result/32,distance/controls.y,1);
}
float3 local_illumination(float3 position,float3 ray) {
    float3 result=0;
    [loop]for(uint i=0;i<(uint)fog_colour.w;++i){
        float4 location=matrices.Load(int3(0,12+i,0));
        // Beams reach 40% past the light's own range so they fade out softly
        // instead of ending where the lit surface does.
        float reach=location.w*1.4;
        float3 delta=location.xyz-position;float distance=length(delta);
        if(distance>=reach)continue;
        float3 toward_light=delta/max(distance,.001);
        float4 direction=matrices.Load(int3(1,12+i,0));
        float4 colour=matrices.Load(int3(2,12+i,0));
        float4 properties=matrices.Load(int3(3,12+i,0));
        float attenuation=pow(saturate(1-distance/reach),2);
        if(colour.w>.5)attenuation*=smoothstep(direction.w,min(.9999,direction.w+.08),dot(direction.xyz,-toward_light));
        if(attenuation<=.00001)continue;
        float visible=1;
        if(properties.x>=8){uint cascade=(uint)properties.x;row_major float4x4 transform=float4x4(matrices.Load(int3(0,cascade,0)),matrices.Load(int3(1,cascade,0)),matrices.Load(int3(2,cascade,0)),matrices.Load(int3(3,cascade,0)));
            float4 clip=mul(float4(position,1),transform);if(clip.w<=.00001)continue;
            float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
            if(any(uv<0)||any(uv>1)||p.z<0||p.z>1)continue;
            float stored=shadow_sample(cascade,uv);visible=properties.y>1.5?(p.z>=stored-.00002?1:0):(p.z<=stored+.00002?1:0);
        }
        // Looking straight into a lamp, a strong forward peak (up to ~19x with
        // Focused) whited out the screen. Lights use a gentler lobe, capped.
        float g=min(medium.w,.5),mu=dot(ray,toward_light);float phase=min((1-g*g)/pow(max(1+g*g-2*g*mu,.001),1.5),3);
        result+=finite3(colour.rgb*attenuation*visible*phase*sun_colour.w);
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
    float4 atm=atmosphere();float sea=sea_distance(v.uv,ray,atm);if(atm.x>1.5)distance=min(distance,sea);
    float lamp_k=max(temporal_controls().z,1);
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
    if(shape.x>0||atm.z>0)phase=min(phase,4);
    // Interleaved gradient noise, rotated each frame, moves the sample
    // positions so temporal blending removes banding instead of freezing it.
    bool temporal=shape.w>.5;
    float jitter=temporal?frac(52.9829189*frac(dot(v.position.xy,float2(.06711056,.00583715)))+matrices.Load(int3(3,20,0)).w*.61803399):.5;
    // Fog owns extinction and ambient haze. Beams use a fixed, clear-air
    // scattering medium so their brightness does not depend on the fog tier.
    float transmittance=1,beam_transmittance=1,local_transmittance=1;float3 scattered=0,lamps=0;
    // Clarity state: a sample's beam light waits one step so the next sample
    // can tell whether it borders a lit/shadow change.
    float clarity=saturate(shape.x),beams=0,pending_light=0,pending_edge=0,edge_memory=0,previous_lit=1;
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
        // Clear air for lamp beams: denser air turned every lamp into a glowing fog ball.
        float local_segment=exp(-.006*(step*lamp_k));
        float local_weight=transmittance*local_transmittance*(1-local_segment);
        float3 shadow=shadow_result(position);float lit=shadow.x;
        // Clarity weights each sample by the lit/shadow change around it (this
        // step, the next, and a few decaying steps back), never above the
        // un-gated light. The old whole-ray shadow fraction let one pole or
        // jitter noise brighten an entire ray 4.7x and pump with camera motion.
        float change=i>0?abs(lit-previous_lit):0;
        beams+=pending_light*lerp(1-clarity,1,max(pending_edge,change));
        edge_memory=max(edge_memory*.6,change);pending_light=lit*beam_weight*shadow.z;pending_edge=edge_memory;previous_lit=lit;
        // V3 lights the fog itself: shadowed air (under a canopy, behind a
        // hill) is darker and sunlit air brighter, at the same average level.
        // A flat fog colour read as an evenly milky wall.
        // Without cascade cover it returns to the neutral level, not to sunlit.
        float fog_light=shape.x>0?lerp(1,lerp(.45,1.35,lit),shadow.z):1;
        scattered+=fog_colour.rgb*fog_weight*fog_light;lamps+=local_illumination(position,ray)*local_weight*(d<sea?1.:0.);
        transmittance*=segment;beam_transmittance*=beam_segment;local_transmittance*=local_segment;
        if(transmittance<.001)break;
    }
    // Clarity keeps beams where the ray crosses shadow edges (gaps in trees,
    // windows, terrain) and thins the glow of air that is lit all around,
    // which is what read as fog. 0 keeps the original uniform scattering.
    beams+=pending_light*lerp(1-clarity,1,pending_edge);
    // Lamp glow rolls off softly instead of blowing out, and fades in daylight
    // the way real headlight beams do (the sun washes them out).
    float lamp_level=dot(lamps,float3(.2126,.7152,.0722));lamps/=1+lamp_level/.35;
    lamps*=1-.75*smoothstep(.02,.2,dot(sun_colour.rgb,float3(.2126,.7152,.0722)));
    scattered+=lamps;
    // Sun-height response: full beams at sunrise and sunset, a third at noon,
    // none below the horizon, and a warm tint while the sun is low.
    float3 sun=sun_colour.rgb;
    if(shape.z>.5){float elevation=normalize(sunlight.xyz).y;sun*=lerp(1,.35,smoothstep(.35,.9,elevation))*smoothstep(-.04,.06,elevation)*lerp(float3(1,.78,.55),float3(1,1,1),smoothstep(.06,.45,elevation));}
    scattered+=sun*controls.z*phase*beams;
    return finite4(float4(scattered,transmittance));
}
// AMD path (AnyAPI 0.39.2 test). On an RX 6800 the lamp loop above broke the whole frame: it reads each light from the
// matrix texture with Load inside a dynamic loop with early exits, a pattern AMD's compiler has been seen to miscompile
// (gamedev.net topic 701827). Here the lights come from a constant buffer and the loop is unrolled with a guard, so
// there is no texture read and no early exit. Lamp shadows are not used on this path. Integrate stays as it was.
// AnyAPI 0.40.2: LAMP_SLOTS 0/2/4/8 builds (D3D_SHADER_MACRO in lighting_shader_test). The record picks the
// smallest one that holds this frame's lamps, so slots that can only add 0 are not evaluated. Same math, no new
// flow control. LAMP_BRANCH 1 (hidden amd_path 2 experiment) adds uniform branches on the lamp count instead.
#ifndef LAMP_SLOTS
#define LAMP_SLOTS 8
#endif
#ifndef LAMP_BRANCH
#define LAMP_BRANCH 0
#endif
cbuffer Lamps : register(b1) { float4 lamp_rows[32]; };
float3 local_illumination_constants(float3 position,float3 ray) {
    float3 result=0;
#if LAMP_SLOTS>0
    uint count=min((uint)fog_colour.w,(uint)LAMP_SLOTS);
#if LAMP_BRANCH
    [branch]if(count==0)return result;
#endif
    [unroll]for(uint i=0;i<LAMP_SLOTS;++i){
#if LAMP_BRANCH
        [branch]if(i<count){
#endif
        float4 location=lamp_rows[i*4],direction=lamp_rows[i*4+1],colour=lamp_rows[i*4+2];
        float reach=max(location.w*1.4,.001);float3 delta=location.xyz-position;float distance=length(delta);
        float3 toward_light=delta/max(distance,.001);
        float attenuation=i<count?pow(saturate(1-distance/reach),2):0;
        float cone=smoothstep(direction.w,min(.9999,direction.w+.08),dot(direction.xyz,-toward_light));
        attenuation*=colour.w>.5?cone:1;
        float g=min(medium.w,.5),mu=dot(ray,toward_light);float phase=min((1-g*g)/pow(max(1+g*g-2*g*mu,.001),1.5),3);
        result+=finite3(clamp(colour.rgb,0,1e6)*attenuation*phase*sun_colour.w);
#if LAMP_BRANCH
        }
#endif
    }
#endif
    return result;
}
float4 IntegrateConstants(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=scene_distance(v.uv,ray);
    float4 atm=atmosphere();float sea=sea_distance(v.uv,ray,atm);if(atm.x>1.5)distance=min(distance,sea);
    float lamp_k=max(temporal_controls().z,1);
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
    if(shape.x>0||atm.z>0)phase=min(phase,4);
    // Interleaved gradient noise, rotated each frame, moves the sample
    // positions so temporal blending removes banding instead of freezing it.
    bool temporal=shape.w>.5;
    float jitter=temporal?frac(52.9829189*frac(dot(v.position.xy,float2(.06711056,.00583715)))+matrices.Load(int3(3,20,0)).w*.61803399):.5;
    // Fog owns extinction and ambient haze. Beams use a fixed, clear-air
    // scattering medium so their brightness does not depend on the fog tier.
    float transmittance=1,beam_transmittance=1,local_transmittance=1;float3 scattered=0,lamps=0;
    // Clarity state: a sample's beam light waits one step so the next sample
    // can tell whether it borders a lit/shadow change.
    float clarity=saturate(shape.x),beams=0,pending_light=0,pending_edge=0,edge_memory=0,previous_lit=1;
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
        // Clear air for lamp beams: denser air turned every lamp into a glowing fog ball.
        float local_segment=exp(-.006*(step*lamp_k));
        float local_weight=transmittance*local_transmittance*(1-local_segment);
        float3 shadow=shadow_result(position);float lit=shadow.x;
        // Clarity weights each sample by the lit/shadow change around it (this
        // step, the next, and a few decaying steps back), never above the
        // un-gated light. The old whole-ray shadow fraction let one pole or
        // jitter noise brighten an entire ray 4.7x and pump with camera motion.
        float change=i>0?abs(lit-previous_lit):0;
        beams+=pending_light*lerp(1-clarity,1,max(pending_edge,change));
        edge_memory=max(edge_memory*.6,change);pending_light=lit*beam_weight*shadow.z;pending_edge=edge_memory;previous_lit=lit;
        // V3 lights the fog itself: shadowed air (under a canopy, behind a
        // hill) is darker and sunlit air brighter, at the same average level.
        // A flat fog colour read as an evenly milky wall.
        // Without cascade cover it returns to the neutral level, not to sunlit.
        float fog_light=shape.x>0?lerp(1,lerp(.45,1.35,lit),shadow.z):1;
        scattered+=fog_colour.rgb*fog_weight*fog_light;lamps+=local_illumination_constants(position,ray)*local_weight*(d<sea?1.:0.);
        transmittance*=segment;beam_transmittance*=beam_segment;local_transmittance*=local_segment;
        if(transmittance<.001)break;
    }
    // Clarity keeps beams where the ray crosses shadow edges (gaps in trees,
    // windows, terrain) and thins the glow of air that is lit all around,
    // which is what read as fog. 0 keeps the original uniform scattering.
    beams+=pending_light*lerp(1-clarity,1,pending_edge);
    // Lamp glow rolls off softly instead of blowing out, and fades in daylight
    // the way real headlight beams do (the sun washes them out).
    float lamp_level=dot(lamps,float3(.2126,.7152,.0722));lamps/=1+lamp_level/.35;
    lamps*=1-.75*smoothstep(.02,.2,dot(sun_colour.rgb,float3(.2126,.7152,.0722)));
    scattered+=lamps;
    // Sun-height response: full beams at sunrise and sunset, a third at noon,
    // none below the horizon, and a warm tint while the sun is low.
    float3 sun=sun_colour.rgb;
    if(shape.z>.5){float elevation=normalize(sunlight.xyz).y;sun*=lerp(1,.35,smoothstep(.35,.9,elevation))*smoothstep(-.04,.06,elevation)*lerp(float3(1,.78,.55),float3(1,1,1),smoothstep(.06,.45,elevation));}
    scattered+=sun*controls.z*phase*beams;
    float4 result=finite4(float4(scattered,transmittance));return float4(clamp(result.rgb,0,65000),result.a);
}
// Blends this frame's noisy half-resolution result with reprojected history.
// The camera reprojection uses scene depth; the 3x3 neighbourhood clamp
// rejects history that no longer matches (moving objects, disocclusion).
float4 Accumulate(Varying v) : SV_Target {
    float4 current=volume.SampleLevel(point_sampler,v.uv,0);
    float4 previous_camera=matrices.Load(int3(0,20,0)),control=temporal_controls();
    if(previous_camera.w<.5)return current;
    float4 previous_right=matrices.Load(int3(1,20,0)),previous_up=matrices.Load(int3(2,20,0)),previous_forward=matrices.Load(int3(3,20,0));
    float4 atm=atmosphere();float3 ray=view_ray(v.uv);float travel=scene_distance(v.uv,ray);if(atm.x>1.5)travel=min(travel,sea_distance(v.uv,ray,atm));
    float3 relative=camera.xyz+ray*travel-previous_camera.xyz;
    float z=dot(relative,previous_forward.xyz);if(z<=.01)return current;
    float2 uv=float2(dot(relative,previous_right.xyz)/(z*previous_right.w),dot(relative,previous_up.xyz)/(z*previous_up.w))*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1))return current;
    // A ?: would sample both histories, one of them the render target of this draw (GPU-based validation
    // flags it). The selector is uniform, so the branch costs nothing and the value is the same.
    float4 history;[branch]if(control.x>.5)history=history_b.SampleLevel(linear_sampler,uv,0);else history=history_a.SampleLevel(linear_sampler,uv,0);
    // Only neighbours at a similar depth bound the history: at a silhouette the
    // sky's dense fog must not widen the box for the object in front of it.
    uint w,h;volume.GetDimensions(w,h);float2 texel=1/float2(w,h);float4 low=current,high=current;
    float centre=surface_z(v.uv,atm),tolerance=max(.5,centre*.05);
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){float2 uv_n=v.uv+float2(x,y)*texel;if(abs(surface_z(uv_n,atm)-centre)>tolerance)continue;float4 s=volume.SampleLevel(point_sampler,uv_n,0);low=min(low,s);high=max(high,s);}
    return finite4(lerp(current,clamp(finite4(history),low,high),saturate(control.y)));
}
float4 resolved_lighting(float2 uv) {
    if(shaping().w<.5)return volume.SampleLevel(point_sampler,uv,0);
    // The selector names the history that was read, so the other one holds this frame.
    return temporal_controls().x>.5?history_a.SampleLevel(point_sampler,uv,0):history_b.SampleLevel(point_sampler,uv,0);
}
// Drawn with fixed-function blending (ONE, SRC_ALPHA, RGB write mask): target.rgb = rgb + target.rgb * a.
// The HDR target is never copied, and its alpha (the game's own value) is never written.
float4 Composite(Varying v) : SV_Target {
    float4 atm=atmosphere();
    uint w,h;volume.GetDimensions(w,h);float2 pixel=v.uv*float2(w,h)-.5;
    float2 base=floor(pixel),fraction=frac(pixel);
    float center_distance=surface_z(v.uv,atm);
    float4 accumulated=0;float total=0;float neighbour[4];
    [unroll]for(uint n=0;n<4;++n)neighbour[n]=surface_z((base+float2(n&1,n>>1)+.5)/float2(w,h),atm);
    // Plane fit: on any flat surface 1/depth is affine in screen space, so the
    // four texels have no bilinear twist and their interpolation predicts this
    // pixel. Grazing ground then interpolates plainly; the 2% depth tolerance
    // alone rejected it and the nearest-depth fallback made flickering blocks.
    // Real edges miss the prediction and keep the depth-weighted filter.
    float4 inverse=1/max(float4(neighbour[0],neighbour[1],neighbour[2],neighbour[3]),.01);
    float planar=lerp(lerp(inverse.x,inverse.y,fraction.x),lerp(inverse.z,inverse.w,fraction.x),fraction.y);
    float plane=saturate(2-max(abs(1-planar*center_distance),abs(inverse.x+inverse.w-inverse.y-inverse.z)*center_distance)/.01);
    [unroll]for(uint y=0;y<2;++y)[unroll]for(uint x=0;x<2;++x) {
        float2 uv=(base+float2(x,y)+.5)/float2(w,h);
        float bilinear=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
        float weight=bilinear*lerp(exp(-abs(center_distance-neighbour[y*2+x])/max(.1,center_distance*.02)),1,plane);
        accumulated+=resolved_lighting(uv)*weight;total+=weight;
    }
    float4 lighting=accumulated/max(total,.00001);
    // No half-resolution neighbour matches this pixel's depth (a thin branch or a
    // silhouette against the sky): take the nearest-depth texel from a wider
    // window instead of the sky's fog, which would leave a bright outline.
    if(total<.1){float best=1e30;
        [unroll]for(int y=-1;y<3;++y)[unroll]for(int x=-1;x<3;++x){float2 uv=(base+float2(x,y)+.5)/float2(w,h);
            float difference=abs(surface_z(uv,atm)-center_distance);
            if(difference<best){best=difference;lighting=resolved_lighting(uv);}}}
    lighting=finite4(lighting);float transmittance=saturate(lighting.a);
    // Never add negative light (every vendor). Lightning lights the fogged part of the view.
    return float4(max(lighting.rgb+flash_glow()*(1-transmittance),0),transmittance);
}
