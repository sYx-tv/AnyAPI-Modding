// AnyAPI scene view: a second camera's gbuffer, drawn by the game's own gbuffer
// pass into AnyAPI's targets, lit here with the game's sun, sky and fog colours
// and drawn into a rectangle of the finished screen (before the HUD).
cbuffer Frame : register(b0) {
    row_major float4x4 inverse_view_projection; // world = M * ndc
    float4 camera;      // view position; w exposure
    float4 sun;         // toward the sun
    float4 sun_light;
    float4 sky_light;
    float4 back_light;
    float4 view_up;     // sky colour overhead
    float4 view_down;   // sky colour at the horizon and below
    float4 options;     // x far plane (fog reaches the sky colour there; 0 no fog), y 1 to gamma-encode
};
Texture2D depth : register(t0);   // reversed: sky is 0
Texture2D normals : register(t1); // gbuffer_1: world normal
Texture2D albedo : register(t2);  // gbuffer_0
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
float3 world(float2 uv, float d) {
    float4 p = mul(inverse_view_projection, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1));
    return p.xyz / p.w;
}
float3 aces(float3 x) { return saturate((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14)); }
float4 View(Varying v) : SV_Target {
    float d = depth.SampleLevel(point_sampler, v.uv, 0).r;
    // Reversed depth: 1 is the near plane, .5 twice as far, so the two give the ray.
    float3 ray = normalize(world(v.uv, .5) - world(v.uv, 1));
    float3 sky = lerp(view_down.rgb, view_up.rgb, saturate(ray.y * 1.5 + .2));
    float3 colour = sky;
    if (d > 0) {
        float3 p = world(v.uv, d);
        float dist = length(p - camera.xyz);
        float3 n = normalize(normals.SampleLevel(point_sampler, v.uv, 0).xyz);
        float3 a = albedo.SampleLevel(point_sampler, v.uv, 0).rgb;
        float lit = saturate(dot(n, normalize(sun.xyz)));
        float3 ambient = sky_light.rgb * (.6 + .4 * n.y) + back_light.rgb * .5 * saturate(-dot(n, normalize(sun.xyz)));
        colour = a * (sun_light.rgb * lit + ambient);
        // The game's base fog: towards the sky colour by (distance / far)^2.2.
        if (options.x > 0) colour = lerp(colour, sky, pow(saturate(dist / options.x), 2.2));
    }
    colour = aces(colour * camera.w);
    if (options.y > .5) colour = pow(colour, 1 / 2.2);
    return float4(colour, 1);
}
