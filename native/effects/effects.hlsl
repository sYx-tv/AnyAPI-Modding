// AnyAPI scene effects: HDR finishing after volumetric lighting, before the
// game's bloom, tone mapping and HUD (renderer._record_commands_additive).
//
// The game tone maps with a fixed chain (decoded from pipeline_postprocess):
//   display = saturate115(contrast112(gamma22(aces(0.85 * hdr))))
// Composite computes the display value we want, then writes the HDR value
// that the game's chain turns into exactly that. The game's vignette and
// screen fade still apply afterwards.
cbuffer Frame : register(b0) {
    float4 metrics;    // 1/width, 1/height, width, height of the full target
    float4 sun_screen; // sun uv.xy, glare on (1/0), sun height above horizon
    float4 sun_colour; // rgb light colour, glare strength
    float4 grading;    // tonemap, look, look strength, manual stops
    float4 exposure;   // eye adaptation (1/0), fast blend, slow blend, max stops
    float4 extras;     // bloom mix, vignette, pass texel size x, y
};
Texture2D image : register(t0);
Texture2D second : register(t1);
Texture2D adapted : register(t2); // 1x1: fast log2 luma, slow log2 luma, sun visibility, valid
Texture2D depth : register(t3);
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying Vertex(uint id : SV_VertexID) {
    Varying v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
static const float3 luma709 = float3(.2126, .7152, .0722);

// --- Exposure metering -------------------------------------------------------
// 64x36 grid of centre-weighted log luminance; rg = (weight * log2 L, weight).
float4 Luma(Varying v) : SV_Target {
    float3 c = 0; float2 o = extras.zw * .25;
    c += image.SampleLevel(linear_sampler, v.uv + float2(-o.x, -o.y), 0).rgb;
    c += image.SampleLevel(linear_sampler, v.uv + float2(o.x, -o.y), 0).rgb;
    c += image.SampleLevel(linear_sampler, v.uv + float2(-o.x, o.y), 0).rgb;
    c += image.SampleLevel(linear_sampler, v.uv + float2(o.x, o.y), 0).rgb;
    float l = dot(min(c * .25, 64), luma709);
    float2 d = v.uv - .5; float w = 1 - .6 * saturate(dot(d, d) * 4);
    return float4(w * log2(max(l, .0001)), w, 0, 1);
}
// One pixel: averages the grid, blends a fast and a slow adapted value and
// measures how much of the sun's disc is open sky (reversed depth: sky is 0).
float4 Adapt(Varying v) : SV_Target {
    float2 sum = 0;
    [loop] for (int y = 0; y < 36; ++y) [loop] for (int x = 0; x < 64; ++x) sum += image.Load(int3(x, y, 0)).rg;
    float l = sum.x / max(sum.y, .0001);
    float4 previous = second.Load(int3(0, 0, 0));
    float visibility = 0;
    if (sun_screen.z > .5) {
        [unroll] for (int i = 0; i < 16; ++i) {
            float a = i * 2.39996323, r = .004 + .012 * sqrt((i + .5) / 16);
            float2 uv = sun_screen.xy + float2(cos(a), sin(a)) * r * float2(metrics.w * metrics.x, 1);
            bool inside = all(uv >= 0) && all(uv <= 1);
            visibility += inside && depth.SampleLevel(point_sampler, uv, 0).r <= 0 ? 1. / 16 : 0;
        }
    }
    if (previous.w < .5) return float4(l, l, visibility, 1);
    return float4(lerp(previous.x, l, exposure.y), lerp(previous.y, l, exposure.z), lerp(previous.z, visibility, .35), 1);
}

// --- Bloom -------------------------------------------------------------------
// 13-tap downsample (Jimenez 2014). extras.x marks the first level, which
// uses a Karis average so a single very bright pixel (the sun disc is light
// colour x 200) cannot flicker.
float3 tap(float2 uv) { return min(image.SampleLevel(linear_sampler, uv, 0).rgb, 256); }
float karis(float3 c) { return 1 / (1 + dot(c, luma709) * .25); }
float4 Down(Varying v) : SV_Target {
    float2 t = extras.zw; float2 uv = v.uv;
    float3 a = tap(uv + t * float2(-2, -2)), b = tap(uv + t * float2(0, -2)), c = tap(uv + t * float2(2, -2));
    float3 d = tap(uv + t * float2(-2, 0)), e = tap(uv), f = tap(uv + t * float2(2, 0));
    float3 g = tap(uv + t * float2(-2, 2)), h = tap(uv + t * float2(0, 2)), i = tap(uv + t * float2(2, 2));
    float3 j = tap(uv + t * float2(-1, -1)), k = tap(uv + t * float2(1, -1)), l = tap(uv + t * float2(-1, 1)), m = tap(uv + t * float2(1, 1));
    if (extras.x > .5) {
        float3 q0 = (a + b + d + e) * .25, q1 = (b + c + e + f) * .25, q2 = (d + e + g + h) * .25, q3 = (e + f + h + i) * .25, q4 = (j + k + l + m) * .25;
        float w0 = karis(q0), w1 = karis(q1), w2 = karis(q2), w3 = karis(q3), w4 = karis(q4);
        return float4((q0 * w0 * .125 + q1 * w1 * .125 + q2 * w2 * .125 + q3 * w3 * .125 + q4 * w4 * .5) / (w0 * .125 + w1 * .125 + w2 * .125 + w3 * .125 + w4 * .5), 1);
    }
    return float4(e * .125 + (a + c + g + i) * .03125 + (b + d + f + h) * .0625 + (j + k + l + m) * .125, 1);
}
// 3x3 tent upsample of the smaller level, added onto this level by blending.
float4 Up(Varying v) : SV_Target {
    float2 t = extras.zw; float2 uv = v.uv;
    float3 s = image.SampleLevel(linear_sampler, uv, 0).rgb * 4;
    s += (image.SampleLevel(linear_sampler, uv + float2(-t.x, 0), 0).rgb + image.SampleLevel(linear_sampler, uv + float2(t.x, 0), 0).rgb
        + image.SampleLevel(linear_sampler, uv + float2(0, -t.y), 0).rgb + image.SampleLevel(linear_sampler, uv + float2(0, t.y), 0).rgb) * 2;
    s += image.SampleLevel(linear_sampler, uv + float2(-t.x, -t.y), 0).rgb + image.SampleLevel(linear_sampler, uv + float2(t.x, -t.y), 0).rgb
        + image.SampleLevel(linear_sampler, uv + float2(-t.x, t.y), 0).rgb + image.SampleLevel(linear_sampler, uv + float2(t.x, t.y), 0).rgb;
    return float4(s / 16, 1);
}

// --- Tone curves ---------------------------------------------------------------
float3 aces(float3 x) { return saturate(x * (2.51 * x + .03) / (x * (2.43 * x + .59) + .14)); }
// Exact inverse of the Narkowicz fit for outputs below its 1.0 asymptote.
float3 aces_inverse(float3 y) {
    y = clamp(y, 0, .984);
    float3 a = 2.51 - 2.43 * y, b = .03 - .59 * y, c = -.14 * y;
    return (-b + sqrt(max(b * b - 4 * a * c, 0))) / (2 * a);
}
// AgX (Troy Sobotka), polynomial fit by Benjamin Wrensch; returns display-
// encoded values. A gentle "punchy" look keeps the game's saturated palette.
float3 agx(float3 c) {
    const float3x3 inset = float3x3(.842479062253094, .0423282422610123, .0423756549057051,
                                    .0784335999999992, .878468636469772, .0784336,
                                    .0792237451477643, .0791661274605434, .879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -.0528968517574562, -.0529716355144438,
                                     -.0980208811401368, 1.15190312990417, -.0980434501171241,
                                     -.0990297440797205, -.0989611768448433, 1.15107367264116);
    const float low = -12.47393, high = 4.026069;
    float3 x = saturate((clamp(log2(max(mul(c, inset), 1e-10)), low, high) - low) / (high - low));
    float3 x2 = x * x, x4 = x2 * x2;
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + .4298 * x2 + .1191 * x - .00232;
    x = pow(saturate(x), 1.1); float l = dot(x, luma709); x = l + 1.18 * (x - l);
    return saturate(mul(x, outset));
}
// Khronos PBR Neutral: linear in, linear display out.
float3 neutral(float3 c) {
    float x = min(c.r, min(c.g, c.b)); float offset = x < .08 ? x - 6.25 * x * x : .04; c -= offset;
    float peak = max(c.r, max(c.g, c.b)); const float start = .76, desaturation = .15;
    if (peak < start) return c;
    float d = 1 - start, fresh = 1 - d * d / (peak + d - start); c *= fresh / peak;
    float g = 1 - 1 / (desaturation * (peak - fresh) + 1); return lerp(c, fresh.xxx, g);
}
// The game's post-ACES display steps (luma weights assumed Rec.709).
float3 game_display(float3 l) {
    float3 g = pow(saturate(l), 1 / 2.2); g = (g - .45) * 1.12 + .45;
    float y = dot(g, luma709); return y + (g - y) * 1.15;
}
float3 game_display_inverse(float3 d) {
    float y = dot(d, luma709); float3 g = y + (d - y) / 1.15; g = (g - .45) / 1.12 + .45;
    return pow(saturate(g), 2.2);
}

// --- Looks ---------------------------------------------------------------------
// None, Warm, Cool, Teal & orange, Moody, Vivid.
static const float3 white_balance[6] = {float3(1, 1, 1), float3(1.06, 1, .91), float3(.94, .99, 1.07), float3(1.02, 1, .97), float3(.97, .99, 1.02), float3(1.02, 1, .97)};
static const float saturation[6] = {1, 1.05, .98, 1.08, .82, 1.2};
static const float vibrance[6] = {0, .1, 0, .15, 0, .3};
static const float contrast[6] = {1, 1.02, 1.02, 1.06, 1.12, 1.06};
static const float3 shadow_tint[6] = {float3(0, 0, 0), float3(.012, .004, -.008), float3(-.01, 0, .016), float3(-.025, .01, .03), float3(-.012, .002, .016), float3(0, 0, 0)};
static const float3 highlight_tint[6] = {float3(0, 0, 0), float3(.02, .008, -.015), float3(-.012, 0, .015), float3(.04, .012, -.03), float3(.01, 0, -.01), float3(.01, .005, -.005)};
static const float lift[6] = {0, 0, 0, 0, -.012, 0};

float4 Composite(Varying v) : SV_Target {
    float4 source = image.Load(int3(v.position.xy, 0));
    float3 x = source.rgb;
    if (extras.x > 0) x = lerp(x, second.SampleLevel(linear_sampler, v.uv, 0).rgb / 6, extras.x);
    float4 a = adapted.Load(int3(0, 0, 0));
    // Sun glare: a soft halo, a bright core and six faint streaks, scaled by
    // how much of the sun is unobstructed sky.
    if (sun_screen.z > .5 && a.z > .001) {
        float2 d = (v.uv - sun_screen.xy) * float2(metrics.z * metrics.y, 1); float r = length(d);
        float angle = atan2(d.y, d.x);
        float streaks = pow(abs(cos(angle * 3 + .4)), 160) * exp(-r * 7) * .35;
        float glow = exp(-r * 6) * .18 + exp(-r * 30) * .5 + streaks;
        float horizon = smoothstep(-.02, .08, sun_screen.w);
        x += sun_colour.rgb * glow * a.z * sun_colour.w * horizon;
    }
    float stops = grading.w;
    if (exposure.x > .5 && a.w > .5) stops += clamp((a.y - a.x) * .75, -exposure.w, exposure.w);
    x *= exp2(stops);
    uint look = (uint)clamp(grading.y, 0, 5); float strength = grading.z;
    x *= lerp(1, white_balance[look], strength);
    float3 d;
    if (grading.x < .5) d = game_display(aces(.85 * x));
    else d = grading.x < 1.5 ? agx(x * 1.1) : pow(saturate(neutral(x * 1.2)), 1 / 2.2);
    // Display-domain grading.
    float y = dot(d, luma709);
    float sat = lerp(1, saturation[look], strength), vib = vibrance[look] * strength;
    float chroma = max(d.r, max(d.g, d.b)) - min(d.r, min(d.g, d.b));
    d = y + (d - y) * sat * (1 + vib * (1 - saturate(chroma * 2)));
    d = (d - .45) * lerp(1, contrast[look], strength) + .45 + lift[look] * strength * (1 - d);
    float shadows = 1 - smoothstep(0, .5, y), highlights = smoothstep(.5, 1, y);
    d += (shadow_tint[look] * shadows + highlight_tint[look] * highlights) * strength;
    if (extras.y > 0) { float2 c = v.uv - .5; d *= 1 - extras.y * .5 * smoothstep(.15, .75, dot(c, c) * 2); }
    // Write the HDR value the game's own tone mapping turns into d.
    return float4(aces_inverse(game_display_inverse(max(d, 0))) / .85, source.a);
}
