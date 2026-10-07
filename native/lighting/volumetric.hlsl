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
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);
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
float visibility(float3 position) {
    if(sunlight.w<.5)return 1;
    float4 clip=mul(float4(position,1),shadow_matrix);
    if(abs(clip.w)<.000001)return 1;
    float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1)||p.z<0||p.z>1)return 1;
    float stored=shadow_depth.SampleLevel(point_sampler,uv,0);
    return sunlight.w>1.5?(p.z>=stored-.0008?1:0):(p.z<=stored+.0008?1:0);
}
float4 Integrate(Varying v) : SV_Target {
    float3 ray=view_ray(v.uv);
    float distance=view_distance(scene_depth.SampleLevel(point_sampler,v.uv,0));
    distance=min(distance/max(abs(dot(ray,normalize(forward_axis.xyz))),.05),controls.y);
    uint count=(uint)clamp(controls.x,8,64);float step=distance/count;
    float g=clamp(medium.w,-.8,.8);float mu=dot(ray,normalize(sunlight.xyz));
    float phase=(1-g*g)/(12.5663706*pow(max(1+g*g-2*g*mu,.001),1.5));
    float transmittance=1;float3 scattered=0;
    [loop]for(uint i=0;i<count;++i) {
        float d=(i+.5)*step;float3 position=camera.xyz+ray*d;
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
