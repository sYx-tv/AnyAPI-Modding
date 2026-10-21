// AnyAPI sky: a sun disc, a drifting cloud layer, and at night stars, a milky
// way and a moon on sky pixels plus moonlight on surfaces, drawn onto the HDR
// scene after the game's sky, fog and ocean and before tone mapping. Blended
// as premultiplied colour over the scene: alpha is cloud cover, so with no
// clouds everything is purely added and with everything off nothing changes.
cbuffer Frame : register(b0) {
    float4 metrics;   // 1/width, 1/height, width, height
    float4 right_axis; // w: tangent of half horizontal field of view
    float4 up_axis;    // w: tangent of half vertical field of view
    float4 forward_axis;
    float4 sun;        // toward the sun; w: night factor 0..1
    float4 moon;       // toward the moon; w: above-horizon fade
    float4 celestial;  // cos, sin of the sky rotation; seconds
    float4 params;     // stars, moon brightness, moonlight, milky way
    float4 sun_light;  // game sun colour; w: sun disc strength
    float4 sky_light;  // game sky colour; w: cloud amount
    float4 clouds;     // x, z: camera in the cloud field (cells); y: cloud base above the camera (m); w: daylight 0..1
};
Texture2D depth : register(t0);   // reversed: sky is 0
Texture2D normals : register(t1); // gbuffer_1: world normal
Texture2D albedo : register(t2);  // gbuffer_0
Texture2D ocean : register(t3);   // gbuffer_ocean: x flag, y distance (ocean writes no depth)
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
float4 hash4(float3 p) {
    float4 q = frac(float4(p.xyzx) * float4(.1031, .1030, .0973, .1099));
    q += dot(q, q.wzxy + 33.33);
    return frac((q.xxyz + q.yzzw) * q.zywx);
}
float value_noise(float3 p) {
    float3 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    float a = hash4(i).x, b = hash4(i + float3(1, 0, 0)).x, c = hash4(i + float3(0, 1, 0)).x, d = hash4(i + float3(1, 1, 0)).x;
    float e = hash4(i + float3(0, 0, 1)).x, g = hash4(i + float3(1, 0, 1)).x, h = hash4(i + float3(0, 1, 1)).x, k = hash4(i + float3(1, 1, 1)).x;
    return lerp(lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y), lerp(lerp(e, g, f.x), lerp(h, k, f.x), f.y), f.z);
}
float3 stars(float3 d, float pixel) {
    float3 p = d * 150, cell = floor(p); float4 h = hash4(cell);
    if (h.w > .16) return 0;
    // Inset so a star never straddles its cell edge.
    float3 star = normalize(cell + .2 + .6 * h.xyz);
    float angle = length(d - star), radius = max(pixel * 1.1, .0004);
    float magnitude = pow(saturate(h.w / .16), 5);          // most stars faint, a few bright
    float brightness = lerp(2.5, .08, magnitude);
    float twinkle = .75 + .25 * sin(celestial.z * (1.5 + 3 * h.x) + h.y * 40);
    float3 tint = lerp(float3(1, .82, .62), float3(.72, .82, 1), h.z);
    return tint * brightness * twinkle * exp(-angle * angle / (radius * radius));
}
float3 milky_way(float3 d) {
    float band = exp(-pow(dot(d, normalize(float3(.3, .8, .5))) / .2, 2));
    float n = value_noise(d * 5) * .55 + value_noise(d * 11) * .3 + value_noise(d * 23) * .15;
    return float3(.55, .62, .85) * band * saturate(n * 1.6 - .35) * .05;
}
float3 moon_disc(float3 d, float pixel) {
    float3 m = normalize(moon.xyz); float3 offset = d - m; float angle = length(offset);
    const float radius = .022;
    float3 glow = float3(.6, .68, .85) * (exp(-angle * 28) * .06 + exp(-angle * 6) * .015);
    if (angle > radius + pixel * 2) return glow;
    float3 t = normalize(abs(m.y) > .99 ? float3(1, 0, 0) : cross(float3(0, 1, 0), m)), b = cross(m, t);
    float2 q = float2(dot(offset, t), dot(offset, b)) / radius; float r2 = dot(q, q);
    float3 n = normalize(q.x * t + q.y * b - sqrt(saturate(1 - r2)) * m);
    float lit = saturate(dot(n, normalize(sun.xyz)) * 1.2 + .1) + .03;   // phase, plus earthshine
    float maria = .72 + .28 * value_noise(float3(q * 3.5, 7.3)) - .12 * value_noise(float3(q * 9, 1.7));
    float edge = 1 - smoothstep(1 - pixel * 2 / radius, 1, sqrt(r2));
    return glow + float3(1, .97, .9) * 3 * lit * maria * edge;
}
float3 sun_disc(float3 d, float pixel) {
    float3 s = normalize(sun.xyz); float angle = length(d - s);
    const float radius = .0125;
    // Limb-darkened disc plus a tight halo, in the game's sun colour.
    float edge = 1 - smoothstep(radius - pixel, radius + pixel, angle);
    float mu = sqrt(saturate(1 - angle * angle / (radius * radius)));
    float3 disc = sun_light.rgb * 14 * (.4 + .6 * mu) * edge;
    float3 halo = sun_light.rgb * (exp(-angle * 60) * .5 + exp(-angle * 12) * .06);
    return min(disc + halo, 60) * smoothstep(-.03, .02, s.y);
}
// Cloud field noise. The hash wraps every 256 cells and every octave doubles the
// frequency with whole-cell offsets, so the whole field repeats every 256 cells and
// the CPU can wrap the camera's position there without a visible jump.
static const float cloud_period = 256, cloud_cell = 1400;
float cloud_hash(float2 i) {
    i = i - cloud_period * floor(i / cloud_period);
    float3 q = frac(float3(i.xyx) * float3(.1031, .1030, .0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float cloud_value(float2 p) {
    float2 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    return lerp(lerp(cloud_hash(i), cloud_hash(i + float2(1, 0)), f.x), lerp(cloud_hash(i + float2(0, 1)), cloud_hash(i + float2(1, 1)), f.x), f.y);
}
// footprint: noise cells covered by one pixel. Octaves finer than a pixel fade
// out instead of shimmering, so distant clouds stay soft rather than sparkling.
float cloud_noise(float2 p, float footprint) {
    // A gentle domain warp turns round blobs into streaky, wind-blown shapes.
    p += float2(cloud_value(p + float2(17, 3)), cloud_value(p + float2(5, 29))) * .35;
    float n = 0, a = .5, w = 0, cells = footprint;
    [unroll] for (int i = 0; i < 6; ++i) {
        float k = a * saturate(1.5 - cells * 2);
        n += k * cloud_value(p); w += a;
        p = p * 2 + float2(37, 11); a *= .5; cells *= 2;
    }
    return n / w;
}
// One cloud layer about 1.5 km up, fixed in the world: it scrolls past as the
// player travels and drifts with the wind. Lit by the sun by day and the moon by
// night, with the game's sky colour as ambient, so dawn and dusk tint it too.
// Far clouds thin out so the game's own horizon haze shows through them.
float4 cloud_layer(float3 d, float pixel) {
    float height = clouds.y;
    if (sky_light.w <= 0 || d.y <= .005 || height < 100) return 0;
    float distance = height / d.y;
    float fade = exp(-distance / 22000) * smoothstep(.005, .05, d.y) * saturate((height - 100) / 400);
    if (fade < .01) return 0;
    float2 p = clouds.xz + d.xz * distance / cloud_cell;
    float footprint = pixel * distance / (cloud_cell * max(d.y, .05));
    float threshold = .62 - .14 * sky_light.w;
    float n = cloud_noise(p, footprint);
    float density = smoothstep(0, 1, saturate((n - threshold) / .1));
    if (density <= 0) return 0;
    float daylight = clouds.w;
    float3 light = daylight > 0 ? normalize(sun.xyz) : normalize(moon.xyz);
    // Self-shadow: denser cloud toward the light means a darker base.
    float toward = saturate((cloud_noise(p + light.xz * .03, footprint) - threshold) / .1);
    float lit = lerp(1, .5, saturate(toward - density * .4));
    float edge = 1 - density;
    float forward = pow(saturate(dot(d, light)), 8) * edge * 2;   // silver lining
    float3 key = sun_light.rgb * daylight + float3(.55, .65, .9) * .15 * params.z * moon.w * sun.w;
    float3 colour = (key * (lit * .35 + forward * .3) + sky_light.rgb * (.3 + .15 * edge));
    float alpha = density * .85 * fade;
    return float4(colour * alpha, alpha);
}
float4 Sky(Varying v) : SV_Target {
    float night = sun.w; if (night <= 0 && sun_light.w <= 0 && sky_light.w <= 0) return 0;
    float2 ndc = v.uv * float2(2, -2) + float2(-1, 1);
    float3 d = normalize(forward_axis.xyz + right_axis.xyz * ndc.x * right_axis.w + up_axis.xyz * ndc.y * up_axis.w);
    float scene_depth = depth.SampleLevel(point_sampler, v.uv, 0).r;
    float2 sea = ocean.SampleLevel(point_sampler, v.uv, 0).xy;
    float3 colour = 0;
    if (scene_depth <= 0 && sea.x <= 0 && sea.y <= 0) {
        float pixel = 2 * up_axis.w * metrics.y;
        // The game's own clouds sit in front of everything added here.
        if (normals.SampleLevel(point_sampler, v.uv, 0).w > .5) return 0;
        float3 behind = 0;
        if (night > 0) {
            float3 c = float3(d.x, d.y * celestial.x + d.z * celestial.y, d.z * celestial.x - d.y * celestial.y);
            float horizon = smoothstep(-.02, .18, d.y);
            behind = ((stars(c, pixel) * params.x + milky_way(c) * params.w) * horizon + moon_disc(d, pixel) * params.y * moon.w) * night;
        }
        if (sun_light.w > 0) behind += sun_disc(d, pixel) * sun_light.w;
        float4 cloud = cloud_layer(d, pixel);
        return float4(cloud.rgb + behind * (1 - cloud.a), cloud.a);
    } else if (scene_depth > 0 && night > 0) {
        // Moonlight: unshadowed (the game has no shadow map for it).
        float3 n = normalize(normals.SampleLevel(point_sampler, v.uv, 0).xyz);
        float3 a = albedo.SampleLevel(point_sampler, v.uv, 0).rgb;
        // About twice the game's night ambient at strength 1, so the moon casts a clear direction.
        colour = a * float3(.55, .65, .9) * .15 * params.z * moon.w * saturate(dot(n, normalize(moon.xyz)));
    }
    return float4(colour * night, 0);
}
