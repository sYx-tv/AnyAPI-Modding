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
    float4 params;     // radius (m), strength, frame index (0..15, 0 without TAA), shading revision (0 V2, 1)
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
// 4x4 interleaved rotations matched to the 4x4 blur in Resolve: every blur window holds each
// rotation exactly once, so the pattern cancels instead of leaving grain (CSG-9).
static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float4 Occlusion(Varying v) : SV_Target {
    bool sky; float3 p = position(v.uv, sky);
    float4 n4 = normals.SampleLevel(point_sampler, v.uv, 0);
    float z = dot(p, forward_axis.xyz);
    if (sky || n4.w > .5 || z >= forward_axis.w * .99) return float4(1, z, 0, 1);
    float3 n = normalize(n4.xyz);
    bool revised = params.w > .5;
    // Revision 1: the radius grows with distance (like the game's 0.04 x z) and the strength
    // shrinks with it, so far AO is broad and soft (CSG-14).
    float r0 = params.x, radius = revised ? clamp(.03 * z, r0, 4) : r0;
    float strength = params.y * (revised ? r0 / radius : 1);
    float pixels = radius * metrics.w * .5 / max(z * up_axis.w, .001);
    if (pixels < 1.5) return float4(1, z, 0, 1);
    // Revision 1 caps the footprint at a share of the screen and shrinks the world radius with it (CSG-1).
    float cap = revised ? max(metrics.w * .075, 32) : 80;
    float reach = revised ? radius * min(1, cap / pixels) : r0;
    pixels = min(pixels, cap);
    // Fixed per pixel; offset by the frame only while TAA resolves it (params.z is 0 otherwise).
    uint2 cell = uint2(v.position.xy) & 3; uint k = (cell.x + 4 * cell.y + uint(params.z)) & 15;
    float angle = (bayer[k] + .5) / 16 * 6.2831853, jitter = (bayer[(k * 5 + 3) & 15] + .5) / 16;
    float sum = 0; const int count = 12; int on_screen = 0;
    [loop] for (int i = 0; i < count; ++i) {
        float t = (i + jitter) / count, a = angle + i * 2.39996323;
        float2 offset = float2(cos(a), sin(a)) * t * pixels * metrics.xy, uv = v.uv + offset;
        // Off-screen samples would read the clamped edge texel and smear dark
        // bands along the screen border; they count as open instead.
        if (any(uv < 0) || any(uv > 1)) continue;
        ++on_screen;
        bool s; float3 q = position(uv, s);
        if (s) continue;
        float3 d = q - p; float dd = dot(d, d);
        float falloff, weight = 1;
        if (revised) {
            // Reach r (not 2r), and a thickness ramp: full weight up to .5r in front of the pixel,
            // none past 1.5r, so objects standing in front cast no halo (CSG-8).
            float front = dot(p - q, normalize(p));
            falloff = saturate(1 - dd / (reach * reach)) * saturate((1.5 * reach - front) / reach);
            // Sky weighting: occluders above block more of the game's sky ambient than those below (CSG-14).
            weight = lerp(.35, 1.4, saturate(.5 + .5 * d.y * rsqrt(dd + 1e-6)));
        } else falloff = saturate(1 - dd / (r0 * r0 * 4));
        sum += max(0, dot(d, n) - .002 * z) / (dd + .01) * falloff * weight;
    }
    // Near the screen edge fewer samples land, so the shading fades out instead of jumping.
    float visibility = revised ? saturate(1 - strength * reach * sum / count) : saturate(1 - 2 * params.y * r0 * .5 * sum / count);
    visibility = lerp(1, visibility, float(on_screen) / count);
    return float4(visibility, z, 0, 1);
}
// Depth-aware 4x4 blur, then the multi-bounce fit; writes the game's SSAO target.
float4 Resolve(Varying v) : SV_Target {
    int2 at = int2(v.position.xy);
    float2 centre = raw.Load(int3(at, 0)).rg;
    float sum = 0, weight = 0;
    // Texel-exact 4x4 window, clamped to the target, matching the 4x4 interleave. Offsets -1..2:
    // the texels 0.40.1's point taps at uv + (-1.5..1.5) texels landed on, so the AO is not shifted.
    int2 last = int2(metrics.zw) - 1;
    [unroll] for (int y = -1; y < 3; ++y) [unroll] for (int x = -1; x < 3; ++x) {
        float2 s = raw.Load(int3(clamp(at + int2(x, y), 0, last), 0)).rg;
        float w = saturate(1 - abs(s.y - centre.y) / max(centre.y * .05, .05));
        sum += s.x * w; weight += w;
    }
    float visibility = weight > 0 ? sum / weight : centre.x;
    float a = saturate(dot(albedo.SampleLevel(point_sampler, v.uv, 0).rgb, float3(.2126, .7152, .0722)));
    float fa = 2.0404 * a - .3324, fb = -4.7951 * a + .6417, fc = 2.7552 * a + .6903;
    visibility = max(visibility, ((visibility * fa + fb) * visibility + fc) * visibility);
    return float4(saturate(visibility).xxx, 1);
}
