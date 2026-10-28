// AnyAPI water: screen-space reflections and a sun glint on the ocean, added
// to the HDR scene after the game's ocean and before volumetric lighting.
// The game's water already reflects its sky gradient, so only the difference
// between what a reflected ray finds on screen and an estimate of that sky is
// added, scaled by Fresnel: a ray that finds nothing changes nothing. March
// runs at half resolution; Composite upsamples it, adds the glint, and never
// writes a pixel that is not ocean.
cbuffer Frame : register(b0) {
    float4 metrics;      // 1/width, 1/height, width, height (full resolution)
    float4 right_axis;   // w: tangent of half horizontal field of view
    float4 up_axis;      // w: tangent of half vertical field of view
    float4 forward_axis; // w: longest reflected ray (m)
    float4 projection;   // A B C D: depth = (A*z + B) / (C*z + D)
    float4 camera;       // x: height above sea level (m); y, z: world x, z in the ripple tile; w: frame index
    float4 ripple;       // xy, zw: offsets (m) of the two ripple layers in the tile
    float4 sun;          // toward the sun; w: above-horizon fade
    float4 sun_light;    // game sun colour; w: glint strength
    float4 view_up;      // game sky colours (view up/down, light up/down);
    float4 view_down;    //   w: reflection strength, fog distance (m),
    float4 light_up;     //   ripple strength, glint roughness
    float4 light_down;
    float4 params;       // x: ray steps, y: hit thickness (m), zw: this frame's TAA jitter in uv (0 without TAA)
};
Texture2D colour : register(t0);     // copy of the HDR scene
Texture2D depth : register(t1);      // reversed: sky is 0. The ocean writes none, so under ocean pixels it holds what lies below or behind the water
Texture2D ocean : register(t2);      // gbuffer_ocean: y > 0 = ocean (only used as a mask)
Texture2D reflection : register(t3); // half resolution: rgb reflection minus sky estimate; a 0 = not ocean, else 1 + march confidence
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);
static const float tile = 512;
static const float cells[4] = {8, 4, 2, 1}, weights[4] = {.06, .045, .03, .02};

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
// View ray through a uv, with a forward component of 1 (so distance along it is view z). The image is
// rendered with the TAA jitter (params.zw), so a pixel sees the unjittered ray through uv - jitter (WATER-15).
float3 ray(float2 uv) {
    float2 ndc = (uv - params.zw) * float2(2, -2) + float2(-1, 1);
    return forward_axis.xyz + right_axis.xyz * ndc.x * right_axis.w + up_axis.xyz * ndc.y * up_axis.w;
}
float2 screen(float3 p, float z) {
    return float2(dot(p, right_axis.xyz) / (z * right_axis.w), dot(p, up_axis.xyz) / (z * up_axis.w)) * float2(.5, -.5) + .5 + params.zw;
}
float view_z(float d) {
    float denominator = d * projection.z - projection.x;
    float z = abs(denominator) > .000001 ? (projection.y - d * projection.w) / denominator : 1000000;
    return clamp(abs(z), 0, 1000000);
}
// What the game's sky shows in a direction (its own gradient); the game's water already reflects about this.
float3 sky(float3 d) {
    float s = dot(d, normalize(sun.xyz));
    return lerp(view_down.rgb, view_up.rgb, .5 + .5 * d.y) + lerp(light_down.rgb, light_up.rgb, .5 + .5 * s) + .1 * sun_light.rgb * pow(saturate(s), 8);
}
// Ripple noise: value noise and its gradient, repeating every n cells.
float hash(float2 i, float n) {
    i -= n * floor(i / n);
    float3 q = frac(float3(i.xyx) * float3(.1031, .1030, .0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float2 gradient(float2 p, float n) {
    float2 i = floor(p), f = frac(p), u = f * f * (3 - 2 * f), du = 6 * f * (1 - f);
    float a = hash(i, n), b = hash(i + float2(1, 0), n), c = hash(i + float2(0, 1), n), d = hash(i + float2(1, 1), n), k = a - b - c + d;
    return du * (float2(b - a, c - a) + k * u.yx);
}
// Slope (dh/dx, dh/dz) of small wind ripples, fixed in the world. Octaves finer
// than a pixel fade out and become roughness instead (lost), so distant water
// turns into a calm mirror with a wider glint rather than sparkling.
float2 slope(float2 xz, float footprint, out float lost) {
    float2 s = 0; lost = 0;
    [unroll] for (int i = 0; i < 4; ++i) {
        bool across = (i & 1) != 0;
        float2 q = xz + (across ? ripple.zw : ripple.xy); q = across ? q.yx : q;
        float k = saturate(1.5 - 2 * footprint / cells[i]);
        float2 g = gradient(q / cells[i], tile / cells[i]) * weights[i] * k;
        s += across ? g.yx : g;
        lost += weights[i] * weights[i] * (1 - k * k);
    }
    return s * light_up.w;
}
struct Surface { float3 p, n, v; float z, lost; };
// The sea surface under a pixel: the view ray cut at sea level (y = 0), or at the
// fog distance past the horizon. scale: pixel size in full-resolution pixels.
// View z of the sea surface (or the fog distance) under a uv.
float sea_z(float3 r) { return r.y < -.00001 ? min(camera.x / -r.y, view_down.w) : view_down.w; }
Surface surface(float2 uv, float scale) {
    Surface s; float3 r = ray(uv);
    s.z = sea_z(r);
    s.p = r * s.z; float3 d = normalize(r); s.v = -d;
    float footprint = s.z * 2 * up_axis.w * metrics.y * scale / max(-d.y, .05);
    float2 g = slope(camera.yz + s.p.xz, footprint, s.lost);
    s.n = normalize(float3(-g.x, 1, -g.y)); return s;
}
float noise(float2 p) { return frac(52.9829189 * frac(dot(p, float2(.06711056, .00583715)))); }

// Half resolution: march the reflected ray across the screen; rgb is what it
// found minus the sky estimate (weighted by confidence), a the water distance.
float4 March(Varying v) : SV_Target {
    if (ocean.SampleLevel(point_sampler, v.uv, 0).y <= 0 || view_up.w <= 0) return 0;
    Surface s = surface(v.uv, 2);
    float4 result = float4(0, 0, 0, 1);   // ocean, nothing found
    if (s.z >= view_down.w) return result;
    float3 r = reflect(-s.v, s.n); r = normalize(float3(r.x, max(r.y, .01), r.z));
    // Stop rays that turn back toward the camera before they pass behind it.
    float reach = forward_axis.w, rz = dot(r, forward_axis.xyz);
    if (rz < 0) reach = min(reach, (s.z - .5) / -rz);
    if (reach < 1 || rz < -.6) return result;
    float3 e = s.p + r * reach; float ze = dot(e, forward_axis.xyz);
    float2 s0 = screen(s.p, s.z), s1 = screen(e, ze), delta = s1 - s0;
    // Clip to the screen: 1/z is linear along the projected ray.
    float end = 1;
    if (delta.x > 0) end = min(end, (1 - s0.x) / delta.x); else if (delta.x < 0) end = min(end, -s0.x / delta.x);
    if (delta.y > 0) end = min(end, (1 - s0.y) / delta.y); else if (delta.y < 0) end = min(end, -s0.y / delta.y);
    if (end <= 0 || length(delta * metrics.zw) * end < 3) return result;
    float k0 = 1 / s.z, k1 = 1 / ze, steps = params.x, jitter = noise(v.position.xy + camera.w * 5.588238);
    float previous = 0, hit = -1;
    [loop] for (float i = 0; i < steps; ++i) {
        float u = end * (i + jitter) / steps, zr = 1 / lerp(k0, k1, u);
        float d = depth.SampleLevel(point_sampler, s0 + delta * u, 0).r;
        if (d > 0) {
            // Behind a surface by less than its assumed thickness (or one step), and only above water.
            float zs = view_z(d), stride = abs(zr - 1 / lerp(k0, k1, previous));
            if (zr > zs && zr - zs < max(params.y + zs * .02, stride)) { hit = u; break; }
        }
        previous = u;
    }
    if (hit < 0) return result;
    // WATER-4: the refinement only moves onto points that pass the same thickness window as the hit, so a
    // thin object in front of the true hit cannot capture it.
    float low = previous, high = hit, last = abs(1 / lerp(k0, k1, hit) - 1 / lerp(k0, k1, previous));
    [unroll] for (int j = 0; j < 4; ++j) {
        float m = .5 * (low + high), d = depth.SampleLevel(point_sampler, s0 + delta * m, 0).r, zm = 1 / lerp(k0, k1, m);
        bool behind = false;
        if (d > 0) { float zs = view_z(d); behind = zm > zs && zm - zs < max(params.y + zs * .02, last); }
        if (behind) high = m; else low = m;
    }
    float2 uv = s0 + delta * high, edge = min(uv, 1 - uv);
    // A hit on the water itself is the game's own reflection: keep it.
    if (ocean.SampleLevel(point_sampler, uv, 0).y > 0) return result;
    // WATER-1: no fade along the screen-space ray (its last 20% covered most of the distance); the game's fog in
    // the colour copy already takes far hits toward its sky.
    float confidence = smoothstep(0, .06, min(edge.x, edge.y)) * saturate((rz + .6) * 2);
    result.rgb = (min(colour.SampleLevel(point_sampler, uv, 0).rgb, 64) - sky(r)) * confidence;
    result.a = 1 + confidence;
    return result;
}
// Full resolution, ocean pixels only: Fresnel-weighted reflection from the half
// resolution march (ocean texels at a similar distance only), plus a GGX sun
// glint, both faded like the game's own distance fog.
float4 Composite(Varying v) : SV_Target {
    if (ocean.SampleLevel(point_sampler, v.uv, 0).y <= 0) discard;
    float3 scene = colour.SampleLevel(point_sampler, v.uv, 0).rgb;
    Surface s = surface(v.uv, 1);
    // WATER-6: the game's base fog curve runs on distance, not view z.
    float fog = 1 - pow(saturate(length(s.p) / view_down.w), 2.2);
    float3 lit = scene;
    float blocked = 0;
    if (view_up.w > 0) {
        uint w, h; reflection.GetDimensions(w, h);
        float2 size = float2(w, h), texel = v.uv * size - .5, base = floor(texel), f = texel - base;
        float4 sum = 0; float hits = 0;
        [unroll] for (int i = 0; i < 4; ++i) {
            float2 o = float2(i & 1, i >> 1), at = clamp(base + o, 0, size - 1);
            float4 r = reflection.Load(int3(at, 0));
            // The texel's water distance is closed form (the march stored its confidence in a instead).
            float z = sea_z(ray((at + .5) / size));
            float weight = (o.x > 0 ? f.x : 1 - f.x) * (o.y > 0 ? f.y : 1 - f.y);
            weight *= r.a > .5 ? 1 / (1 + 8 * abs(z - s.z) / max(s.z, 1)) : 0;
            sum += float4(r.rgb, 1) * weight; hits += saturate(r.a - 1) * weight;
        }
        float fresnel = .02 + .98 * pow(1 - saturate(dot(s.n, s.v)), 5);
        if (sum.w > .0001) {
            // WATER-5: the sky estimate may over-subtract; never darken below the scene minus its full reflection share.
            lit = max(scene + sum.rgb / sum.w * fresnel * view_up.w * fog, scene * (1 - fresnel * view_up.w * fog));
            blocked = hits / sum.w;
        }
    }
    float3 added = 0;
    if (sun_light.w > 0 && sun.w > 0) {
        float3 l = normalize(sun.xyz), hv = normalize(s.v + l);
        float nl = saturate(dot(s.n, l)), nv = max(dot(s.n, s.v), .001), nh = saturate(dot(s.n, hv)), vh = saturate(dot(s.v, hv));
        // K48: light_down.w and the lost ripple slope add in GGX alpha space; the result is the alpha (not squared again).
        float a = clamp(sqrt(light_down.w * light_down.w + 2 * s.lost * light_up.w * light_up.w), .03, .5), a2 = a * a, q = nh * nh * (a2 - 1) + 1, k = a * .5;
        float distribution = a2 / (3.14159265 * q * q), visibility = 1 / ((nl * (1 - k) + k) * (nv * (1 - k) + k));
        float f = .02 + .98 * pow(1 - vh, 5);
        // The game lights albedo 1 with sun x 0.4 (a Lambert surface under 0.4 pi x sun), so the glint uses the same
        // scale. Capped so the core blooms like the sun disc instead of turning into a flat white band.
        // WATER-16: capped by the brightest channel, so the core keeps the sun's hue.
        float3 g = sun_light.rgb * (distribution * visibility * f * .25 * nl * 1.2566371);
        g *= min(1, 32 / max(max(g.r, g.g), max(g.b, .0001)));
        // WATER-2 (first step): where the reflected ray hit something on screen, the sun's mirror image is blocked.
        added += g * sun.w * sun_light.w * (1 - blocked);
    }
    return float4(max(lit + added * fog, 0), 0);
}
