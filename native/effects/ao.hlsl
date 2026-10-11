// AnyAPI ambient occlusion: replaces the output of the game's 8-sample SSAO
// inside renderer._record_commands_composite, before the composite reads it.
// The game multiplies only ambient and back light by this value, so direct
// sunlight is never darkened.
//
// Estimator: Alchemy AO (McGuire et al. 2011) with 12 spiral samples, rotated
// per pixel and per frame, a range falloff and a depth-aware 4x4 blur. Output
// is visibility with a multi-bounce fit (Jimenez et al. 2016) on the albedo, so
// bright surfaces do not go grey.
cbuffer Frame : register(b0) {
    float4 metrics;    // 1/width, 1/height, width, height
    float4 right_axis; // w: tangent of half horizontal field of view
    float4 up_axis;    // w: tangent of half vertical field of view
    float4 forward_axis; // w: maximum distance
    float4 projection; // A B C D: depth = (A*z + B) / (C*z + D)
    float4 params;     // radius (m), strength, frame index, unused
};
Texture2D depth : register(t0);
Texture2D normals : register(t1);  // gbuffer_1: world normal, w > 0.5 = clouds
Texture2D albedo : register(t2);   // gbuffer_0
Texture2D raw : register(t3);      // AO pass output: visibility, view distance
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
float view_z(float d) {
    float denominator = d * projection.z - projection.x;
    float z = abs(denominator) > .000001 ? (projection.y - d * projection.w) / denominator : forward_axis.w;
    return clamp(abs(z), 0, forward_axis.w);
}
// Camera-relative world position of a pixel (reversed depth: 0 is sky).
float3 position(float2 uv, out bool sky) {
    float d = depth.SampleLevel(point_sampler, uv, 0).r; sky = d <= 0;
    float2 ndc = uv * float2(2, -2) + float2(-1, 1);
    float3 ray = forward_axis.xyz + right_axis.xyz * ndc.x * right_axis.w + up_axis.xyz * ndc.y * up_axis.w;
    return ray * view_z(d);
}
float noise(float2 p) { return frac(52.9829189 * frac(dot(p, float2(.06711056, .00583715)))); }

float4 Occlusion(Varying v) : SV_Target {
    bool sky; float3 p = position(v.uv, sky);
    float4 n4 = normals.SampleLevel(point_sampler, v.uv, 0);
    float z = dot(p, forward_axis.xyz);
    if (sky || n4.w > .5 || z >= forward_axis.w * .99) return float4(1, z, 0, 1);
    float3 n = normalize(n4.xyz);
    float radius = params.x, pixels = radius * metrics.w * .5 / max(z * up_axis.w, .001);
    if (pixels < 1.5) return float4(1, z, 0, 1);
    pixels = min(pixels, 160);
    float angle = noise(v.position.xy + params.z * 5.588238) * 6.2831853, jitter = noise(v.position.yx * 1.7 + params.z * 3.1);
    float sum = 0; const int count = 12;
    [loop] for (int i = 0; i < count; ++i) {
        float t = (i + jitter) / count, a = angle + i * 2.39996323;
        float2 offset = float2(cos(a), sin(a)) * t * pixels * metrics.xy;
        bool s; float3 q = position(v.uv + offset, s);
        if (s) continue;
        float3 d = q - p; float dd = dot(d, d);
        float falloff = saturate(1 - dd / (radius * radius * 4));
        sum += max(0, dot(d, n) - .002 * z) / (dd + .01) * falloff;
    }
    float visibility = saturate(1 - 2 * params.y * radius * .5 * sum / count);
    return float4(visibility, z, 0, 1);
}
// Depth-aware 4x4 blur, then the multi-bounce fit; writes the game's SSAO target.
float4 Resolve(Varying v) : SV_Target {
    float2 centre = raw.SampleLevel(point_sampler, v.uv, 0).rg;
    float sum = 0, weight = 0;
    [unroll] for (int y = -2; y < 2; ++y) [unroll] for (int x = -2; x < 2; ++x) {
        float2 s = raw.SampleLevel(point_sampler, v.uv + (float2(x, y) + .5) * metrics.xy, 0).rg;
        float w = saturate(1 - abs(s.y - centre.y) / max(centre.y * .05, .05));
        sum += s.x * w; weight += w;
    }
    float visibility = weight > 0 ? sum / weight : centre.x;
    float a = saturate(dot(albedo.SampleLevel(point_sampler, v.uv, 0).rgb, float3(.2126, .7152, .0722)));
    float fa = 2.0404 * a - .3324, fb = -4.7951 * a + .6417, fc = 2.7552 * a + .6903;
    visibility = max(visibility, ((visibility * fa + fb) * visibility + fc) * visibility);
    return float4(saturate(visibility).xxx, 1);
}
