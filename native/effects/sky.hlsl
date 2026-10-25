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
    float4 model;      // x: MODEL_* flags; y: moon elongation from the sun (radians, clock sky); z: weather cloud cover 0..1; w: moon opacity 0..1
};
// model.x bits (sky_frame.h keeps the same values).
static const uint MODEL_CLOCK = 1, MODEL_CLOUDS_V2 = 2, MODEL_STAR_SEARCH = 4;
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
// One star cell. V2: 16% of cells hold a star, mostly bright. Clock sky (SKY-6): 5% of cells,
// mostly faint with a few bright ones.
float3 star_cell(float3 cell, float3 d, float pixel, bool sparse) {
    float4 h = hash4(cell); float limit = sparse ? .05 : .16;
    if (h.w > limit) return 0;
    // Inset so a star never straddles its cell edge.
    float3 star = normalize(cell + .2 + .6 * h.xyz);
    float angle = length(d - star), radius = max(pixel * 1.1, .0004);
    float magnitude = pow(saturate(h.w / limit), 5);
    float brightness = sparse ? lerp(.08, 2.5, magnitude) : lerp(2.5, .08, magnitude);
    float twinkle = .75 + .25 * sin(celestial.z * (1.5 + 3 * h.x) + h.y * 40);
    float3 tint = lerp(float3(1, .82, .62), float3(.72, .82, 1), h.z);
    return tint * brightness * twinkle * exp(-angle * angle / (radius * radius));
}
float3 stars(float3 d, float pixel, uint flags) {
    float3 p = d * 150;
    if (!(flags & MODEL_STAR_SEARCH)) return star_cell(floor(p), d, pixel, (flags & MODEL_CLOCK) != 0);
    // SKY-11: the 2x2x2 cells nearest the pixel, so a star near a cell edge is not cut off.
    float3 base = floor(p - .5), best = 0;
    [unroll] for (int i = 0; i < 8; ++i) best = max(best, star_cell(base + float3(i & 1, (i >> 1) & 1, i >> 2), d, pixel, true));
    return best;
}
float3 milky_way(float3 d) {
    float band = exp(-pow(dot(d, normalize(float3(.3, .8, .5))) / .2, 2));
    [branch] if (band < 1e-4) return 0;   // SKY-4: about 40% of the sphere; at most 3e-6 HDR
    float n = value_noise(d * 5) * .55 + value_noise(d * 11) * .3 + value_noise(d * 23) * .15;
    return float3(.55, .62, .85) * band * saturate(n * 1.6 - .35) * .05;
}
// Returns the moon's colour; coverage: how much of the pixel the disc covers (0 outside it).
float3 moon_disc(float3 d, float pixel, uint flags, out float coverage) {
    float3 m = normalize(moon.xyz); float3 offset = d - m; float angle = length(offset);
    const float radius = .022; coverage = 0;
    float3 glow = float3(.6, .68, .85) * (exp(-angle * 28) * .06 + exp(-angle * 6) * .015);
    // Clock sky (SKY-16): the light comes from the phase angle model.y, on the side of the true sun,
    // which is the moon turned half a day about world z: s = (-m.x, -m.y, m.z).
    float3 light = normalize(sun.xyz);
    if (flags & MODEL_CLOCK) {
        glow *= max(.5 - .5 * cos(model.y), .35);
        float3 s = float3(-m.x, -m.y, m.z), u = s - dot(s, m) * m;
        u = dot(u, u) > 1e-6 ? normalize(u) : float3(1, 0, 0);
        light = cos(model.y) * m + sin(model.y) * u;
    }
    if (angle > radius + pixel * 2) return glow;
    float3 t = normalize(abs(m.y) > .99 ? float3(1, 0, 0) : cross(float3(0, 1, 0), m)), b = cross(m, t);
    float2 q = float2(dot(offset, t), dot(offset, b)) / radius; float r2 = dot(q, q);
    float3 n = normalize(q.x * t + q.y * b - sqrt(saturate(1 - r2)) * m);
    float lit = saturate(dot(n, light) * 1.2 + .1) + .03;   // phase, plus earthshine
    float maria = .72 + .28 * value_noise(float3(q * 3.5, 7.3)) - .12 * value_noise(float3(q * 9, 1.7));
    float edge = 1 - smoothstep(1 - pixel * 2 / radius, 1, sqrt(r2));
    coverage = edge;
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
// v2 (mean_fill): a faded octave contributes its mean .5 instead of 0 (SKY-3), and lost returns the
// standard deviation that the fade removed, relative to the total weight.
float cloud_noise(float2 p, float footprint, bool mean_fill, out float lost) {
    // A gentle domain warp turns round blobs into streaky, wind-blown shapes.
    p += float2(cloud_value(p + float2(17, 3)), cloud_value(p + float2(5, 29))) * .35;
    float n = 0, a = .5, w = 0, cells = footprint; lost = 0;
    [unroll] for (int i = 0; i < 6; ++i) {
        float k = a * saturate(1.5 - cells * 2);
        [branch] if (k > 0) n += k * cloud_value(p);   // SKY-4: adding k*value with k == 0 changes nothing
        if (mean_fill) { n += (a - k) * .5; lost += (a - k) * (a - k); }
        w += a;
        p = p * 2 + float2(37, 11); a *= .5; cells *= 2;
    }
    lost = sqrt(lost) / w;
    return n / w;
}
// Two coarse octaves, no warp: the v2 self-shadow estimate (SKY-4).
float cloud_coarse(float2 p) {
    return (cloud_value(p) * .5 + cloud_value(p * 2 + float2(37, 11)) * .25) / .75;
}
// One cloud layer about 1.5 km up, fixed in the world: it scrolls past as the
// player travels and drifts with the wind. Lit by the sun by day and the moon by
// night, with the game's sky colour as ambient, so dawn and dusk tint it too.
// Far clouds thin out so the game's own horizon haze shows through them.
float4 cloud_layer(float3 d, float pixel, uint flags) {
    float height = clouds.y;
    if (sky_light.w <= 0 || d.y <= .005 || height < 100) return 0;
    float distance = height / d.y;
    float fade = exp(-distance / 22000) * smoothstep(.005, .05, d.y) * saturate((height - 100) / 400);
    if (fade < .01) return 0;
    float2 p = clouds.xz + d.xz * distance / cloud_cell;
    float footprint = pixel * distance / (cloud_cell * max(d.y, .05));
    float threshold = .62 - .14 * sky_light.w, lost;
    bool v2 = (flags & MODEL_CLOUDS_V2) != 0;
    float n = cloud_noise(p, footprint, v2, lost);
    // v2 (SKY-3): the band widens by the variance the fade removed, so far cover tends to the expected fraction.
    float density = smoothstep(0, 1, saturate((n - threshold) / (v2 ? .1 + .6 * lost : .1)));
    if (density <= 0) return 0;
    float daylight = clouds.w;
    [branch] if (v2) {
        // SKY-12: separate sun and moon keys, each with a two-octave self-shadow (SKY-4); the moon key is skipped by day.
        float edge = 1 - density, base = 1 - .5 * model.z, moonkey = .15 * params.z * moon.w * sun.w;
        float3 s = normalize(sun.xyz), m = normalize(moon.xyz), colour = 0;
        [branch] if (daylight > 0) {
            float ts = saturate((cloud_coarse(p + s.xz * .25) - threshold) / .1);
            colour = sun_light.rgb * daylight * (lerp(1, .5, saturate(ts - density * .4)) * base * .35 + pow(saturate(dot(d, s)), 8) * edge * .6);
        }
        [branch] if (moonkey > 0) {
            float tm = saturate((cloud_coarse(p + m.xz * .25) - threshold) / .1);
            colour += float3(.55, .65, .9) * moonkey * (lerp(1, .5, saturate(tm - density * .4)) * base * .35 + pow(saturate(dot(d, m)), 8) * edge * .6);
        }
        colour += sky_light.rgb * (.3 + .15 * edge);
        // SKY-8: dense cloud reaches about .97-.99 and hides the sun; the zenith fade no longer caps it.
        // The floor keeps the cloud-base fade, so a camera near the base still sees the deck thin out (no pop at 100 m).
        fade = max(fade, .97 * smoothstep(.3, .6, d.y) * saturate((height - 100) / 400));
        float alpha = fade * (1 - exp(-4 * density)) / (1 - exp(-4));
        return float4(colour * alpha, alpha);
    }
    float3 light = daylight > 0 ? normalize(sun.xyz) : normalize(moon.xyz);
    // Self-shadow: denser cloud toward the light means a darker base.
    float unused, toward = saturate((cloud_noise(p + light.xz * .03, footprint, false, unused) - threshold) / .1);
    float lit = lerp(1, .5, saturate(toward - density * .4));
    float edge = 1 - density;
    float forward = pow(saturate(dot(d, light)), 8) * edge * 2;   // silver lining
    float3 key = sun_light.rgb * daylight + float3(.55, .65, .9) * .15 * params.z * moon.w * sun.w;
    float3 colour = (key * (lit * .35 + forward * .3) + sky_light.rgb * (.3 + .15 * edge));
    float alpha = density * .85 * fade;
    return float4(colour * alpha, alpha);
}
float4 Sky(Varying v) : SV_Target {
    float night = sun.w; if (night <= 0 && sun_light.w <= 0 && sky_light.w <= 0) discard;
    uint flags = (uint)model.x;
    float2 ndc = v.uv * float2(2, -2) + float2(-1, 1);
    float3 d = normalize(forward_axis.xyz + right_axis.xyz * ndc.x * right_axis.w + up_axis.xyz * ndc.y * up_axis.w);
    float scene_depth = depth.SampleLevel(point_sampler, v.uv, 0).r;
    float2 sea = ocean.SampleLevel(point_sampler, v.uv, 0).xy;
    float3 colour = 0;
    if (scene_depth <= 0 && sea.x <= 0 && sea.y <= 0) {
        float pixel = 2 * up_axis.w * metrics.y;
        // The game's own clouds sit in front of everything added here.
        if (normals.SampleLevel(point_sampler, v.uv, 0).w > .5) discard;
        float3 behind = 0; float cover = 0;
        if (night > 0) {
            // V2 turns the star field about x; the clock sky turns it about world z, the axis of the game's sun path.
            float3 c = (flags & MODEL_CLOCK) ? float3(d.x * celestial.x + d.y * celestial.y, d.y * celestial.x - d.x * celestial.y, d.z)
                                             : float3(d.x, d.y * celestial.x + d.z * celestial.y, d.z * celestial.x - d.y * celestial.y);
            float horizon = smoothstep(-.02, .18, d.y), coverage;
            float3 field = (stars(c, pixel, flags) * params.x + milky_way(c) * params.w) * horizon;
            float3 moonlit = moon_disc(d, pixel, flags, coverage) * params.y * moon.w;
            // K49: an opaque moon hides the stars and whatever the game drew behind it.
            cover = coverage * moon.w * model.w * night;
            behind = (field * (1 - cover) + moonlit) * night;
        }
        if (sun_light.w > 0) behind += sun_disc(d, pixel) * sun_light.w;
        float4 cloud = cloud_layer(d, pixel, flags);
        float alpha = cloud.a + (1 - cloud.a) * cover;
        // SKY-14: nothing to add here, so leave the pixel alone without a read-modify-write.
        if (alpha <= 0 && !any(behind)) discard;
        return float4(cloud.rgb + behind * (1 - cloud.a), alpha);
    } else if (scene_depth > 0 && sea.x <= 0 && sea.y <= 0 && night > 0 && params.z > 0) {   // SKY-5: never on the seabed under the ocean
        // Moonlight: unshadowed (the game has no shadow map for it).
        float3 n = normalize(normals.SampleLevel(point_sampler, v.uv, 0).xyz);
        float3 a = albedo.SampleLevel(point_sampler, v.uv, 0).rgb;
        // About twice the game's night ambient at strength 1, so the moon casts a clear direction.
        colour = a * float3(.55, .65, .9) * .15 * params.z * moon.w * saturate(dot(n, normalize(moon.xyz)));
        return float4(colour * night, 0);
    }
    discard;   // SKY-14: geometry by day, ocean, and the seabed under it
    return 0;
}
