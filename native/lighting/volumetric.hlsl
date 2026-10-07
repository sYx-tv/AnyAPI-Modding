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
    default:return shadow7.SampleLevel(point_sampler,uv,0);
    }
}

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
float4 Integrate(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=view_distance(scene_depth.SampleLevel(point_sampler,v.uv,0));
    distance=min(distance/max(abs(dot(ray,normalize(forward_axis.xyz))),.05),controls.y);
    uint count=(uint)clamp(controls.x,8,64);
    float g=clamp(medium.w,-.8,.8);float mu=dot(ray,normalize(sunlight.xyz));
    float phase=(1-g*g)/(12.5663706*pow(max(1+g*g-2*g*mu,.001),1.5));
    float transmittance=1;float3 scattered=0;
    [loop]for(uint i=0;i<count;++i) {
        // Quadratic spacing resolves nearby branch shadows without more samples.
        float a=float(i)/count,b=float(i+1)/count;
        float begin=distance*a*a,end=distance*b*b;
        float step=end-begin,d=(begin+end)*.5;float3 position=camera.xyz+ray*d;
        float world_height=camera.w+ray.y*d;
        float density=medium.x*exp(-max(world_height-medium.y,0)*medium.z);
        float segment=exp(-density*step);float weight=transmittance*(1-segment);
        float3 illumination=fog_colour.rgb*controls.w+sun_colour.rgb*controls.z*phase*visibility(position);
        scattered+=illumination*weight;transmittance*=segment;
        if(transmittance<.001)break;
    }
    return float4(scattered,transmittance);
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
        accumulated+=volume.SampleLevel(point_sampler,uv,0)*weight;total+=weight;
    }
    float4 lighting=total>.00001?accumulated/total:volume.SampleLevel(point_sampler,v.uv,0);
    float4 original=scene_colour.SampleLevel(linear_sampler,v.uv,0);
    return float4(original.rgb*lighting.a+lighting.rgb,original.a);
}
