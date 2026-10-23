#pragma once
// AnyMoto part meshes, generated in code (no game asset ships with the mod) and written into rom/meshes/components
// at start-up, like the other part mods. Stock look: blocky 8-sided shapes, painted body (paint slot 0), painted trim
// (slot 1), dark grey iron, light grey bolts and chrome.
//
// Mesh file layout (as the GpsPart and Rotary meshes): "mesh" u32 5 u32 1 u32 name_length name, 88-byte attribute
// header, 6 x f64 bounds, u32 vertex bytes + 36-byte vertices (f32 pos[3], u8 rgba[4], f32 uv[2], f32 normal[3]),
// u32 index bytes + u32 triangle indices, 8-byte trailer.
#include "anymoto_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace anymoto {

struct MeshVertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(MeshVertex) == 36, "mesh vertex");

struct Rgb { uint8_t r, g, b; };
constexpr Rgb kPaint0{153, 0, 0};          // paint slot 0: body (G_COLOR_REPLACEMENTS[0])
constexpr Rgb kPaint1{153, 38, 0};         // paint slot 1: trim
constexpr Rgb kIron{51, 51, 51};
constexpr Rgb kMid{113, 113, 113};
constexpr Rgb kBolt{198, 198, 198};
constexpr Rgb kChrome{210, 212, 214};
constexpr Rgb kRubber{28, 28, 30};
constexpr Rgb kAmber{255, 176, 0};
constexpr Rgb kTeal{19, 102, 102};

struct Mesh {
    std::vector<MeshVertex> v;
    std::vector<uint32_t> i;
    uint32_t vertex(Vec3 p, Vec3 n, Rgb c) {
        MeshVertex x{};
        x.pos[0] = float(p.x); x.pos[1] = float(p.y); x.pos[2] = float(p.z);
        x.rgba[0] = c.r; x.rgba[1] = c.g; x.rgba[2] = c.b; x.rgba[3] = 255;
        x.normal[0] = float(n.x); x.normal[1] = float(n.y); x.normal[2] = float(n.z);
        v.push_back(x);
        return uint32_t(v.size() - 1);
    }
    // a, b, c counter-clockwise seen from outside. The game draws faces that are clockwise from the viewer, so they
    // are stored the other way round (as turbo_mesh.h does).
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.push_back(a); i.push_back(c); i.push_back(b); }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb col) {   // counter-clockwise from outside
        Vec3 n = normalized(cross(b - a, d - a));
        uint32_t ia = vertex(a, n, col), ib = vertex(b, n, col), ic = vertex(c, n, col), id = vertex(d, n, col);
        tri(ia, ib, ic); tri(ia, ic, id);
    }
    void box(Vec3 lo, Vec3 hi, Rgb c) {
        Vec3 p[8];
        for (int k = 0; k < 8; ++k) p[k] = {k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z};
        quad(p[0], p[2], p[3], p[1], c);   // -z
        quad(p[4], p[5], p[7], p[6], c);   // +z
        quad(p[0], p[4], p[6], p[2], c);   // -x
        quad(p[1], p[3], p[7], p[5], c);   // +x
        quad(p[0], p[1], p[5], p[4], c);   // -y
        quad(p[2], p[6], p[7], p[3], c);   // +y
    }
    // A closed prism with `sides` faces along `axis` (0 x, 1 y, 2 z) through `centre`, from w0 to w1.
    void prism(int axis, Vec3 centre, double r, double w0, double w1, Rgb c, int sides = 8) {
        auto at = [&](double u, double vv, double w) {
            Vec3 l = axis == 0 ? Vec3{w, u, vv} : axis == 1 ? Vec3{vv, w, u} : Vec3{u, vv, w};
            return centre + l;
        };
        double phase = kPi / sides;
        for (int k = 0; k < sides; ++k) {
            double a0 = phase + 2 * kPi * k / sides, a1 = phase + 2 * kPi * (k + 1) / sides;
            Vec3 p0 = at(r * std::cos(a0), r * std::sin(a0), w0), p1 = at(r * std::cos(a1), r * std::sin(a1), w0);
            Vec3 p2 = at(r * std::cos(a1), r * std::sin(a1), w1), p3 = at(r * std::cos(a0), r * std::sin(a0), w1);
            quad(p0, p1, p2, p3, c);
            Vec3 c0 = at(0, 0, w0), c1 = at(0, 0, w1);
            Vec3 n0 = normalized(c0 - c1), n1 = normalized(c1 - c0);
            uint32_t a = vertex(c0, n0, c), b = vertex(p1, n0, c), d = vertex(p0, n0, c);
            tri(a, b, d);
            a = vertex(c1, n1, c); b = vertex(p3, n1, c); d = vertex(p2, n1, c);
            tri(a, b, d);
        }
    }
};

// The 32-byte attribute header of the game's static part meshes (position, colour, uv, normal), then 56 zero bytes.
inline const uint8_t* attribute_header() {
    static const uint8_t h[88] = {3, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    return h;
}
inline std::vector<uint8_t> encode_mesh(const Mesh& m, const std::string& name) {
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& x : m.v) for (int j = 0; j < 3; ++j) { bounds[j] = std::min(bounds[j], double(x.pos[j])); bounds[3 + j] = std::max(bounds[3 + j], double(x.pos[j])); }
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* b = static_cast<const uint8_t*>(p); out.insert(out.end(), b, b + k); };
    uint32_t u = 5, one = 1;
    put("mesh", 4); put(&u, 4); put(&one, 4);
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(attribute_header(), 88);
    put(bounds, sizeof bounds);
    u = uint32_t(m.v.size() * sizeof(MeshVertex)); put(&u, 4); put(m.v.data(), m.v.size() * sizeof(MeshVertex));
    u = uint32_t(m.i.size() * 4); put(&u, 4); put(m.i.data(), m.i.size() * 4);
    uint8_t trailer[8]{};
    put(trailer, 8);
    return out;
}

// ------------------------------------------------------------------------------------------- fork
constexpr double kForkLegX = 0.135;        // tyre half width 0.08 + slider radius + 2 cm
// Static: the block that bolts to the frame's front face and the neck up to the steering head.
inline Mesh fork_base() {
    Mesh m;
    Vec3 h = fork_head();
    m.box({-0.07, -0.11, -0.04}, {0.07, 0.11, 0.10}, kPaint0);
    m.box({-0.05, 0.05, 0.00}, {0.05, h.y + 0.03, h.z - 0.035}, kPaint0);
    for (int s = -1; s <= 1; s += 2)
        for (int r = -1; r <= 1; r += 2) m.prism(2, {s * 0.045, r * 0.07, 0.10}, 0.009, 0, 0.008, kBolt, 6);
    return m;
}
// Upper fork: turns with the steering about the raked axis. Origin at the steering head (top yoke), the legs run
// down -y (fork_frames lines -y up with the axle): head tube, yokes, chrome stanchions.
inline Mesh fork_steer() {
    Mesh m;
    m.prism(1, {0, 0, 0}, 0.038, -0.16, 0.06, kIron);
    m.box({-kForkLegX - 0.035, 0.00, -0.05}, {kForkLegX + 0.035, 0.05, 0.05}, kPaint1);
    m.box({-kForkLegX - 0.035, -0.18, -0.05}, {kForkLegX + 0.035, -0.13, 0.05}, kPaint1);
    for (int s = -1; s <= 1; s += 2) {
        m.prism(1, {s * kForkLegX, 0, 0}, 0.022, -0.50, 0.06, kChrome, 12);
        m.prism(1, {s * kForkLegX, 0, 0}, 0.026, 0.05, 0.07, kIron, 8);    // top cap
    }
    m.prism(1, {0, 0, 0}, 0.02, 0.05, 0.08, kBolt, 6);
    return m;
}
// Lower fork: sliders, axle, caliper and mudguard. Origin at the axle, the legs run up +y along the raked line.
inline Mesh fork_lower() {
    Mesh m;
    for (int s = -1; s <= 1; s += 2) {
        m.prism(1, {s * kForkLegX, 0, 0}, 0.032, -0.045, 0.45, kPaint0, 12);
        m.prism(1, {s * kForkLegX, 0, 0}, 0.036, 0.43, 0.45, kIron, 12);           // dust seal
        m.box({s * kForkLegX - 0.03, -0.035, -0.035}, {s * kForkLegX + 0.03, 0.035, 0.035}, kIron);
        m.prism(0, {s * (kForkLegX + 0.032), 0, 0}, 0.014, -0.004, 0.004, kBolt, 6);
    }
    m.prism(0, {0, 0, 0}, 0.012, -kForkLegX - 0.03, kForkLegX + 0.03, kMid, 8);
    // Brake caliper behind the left leg and the mudguard over the tyre. This frame is tipped back by the rake, so
    // the mudguard's arc is turned forward by the same angle to sit over the top of the tyre.
    m.box({-kForkLegX + 0.015, 0.03, -0.11}, {-kForkLegX + 0.05, 0.09, -0.04}, kIron);
    const double r0 = kMotorcycleTyreRadius + 0.04, r1 = r0 + 0.012;
    for (int k = 0; k < 10; ++k) {
        double a0 = rad(-35 + kForkRakeDeg + 11 * k), a1 = rad(-35 + kForkRakeDeg + 11 * (k + 1));   // behind, over the top, to the front
        Vec3 o0{0, r0 * std::cos(a0), r0 * std::sin(a0)}, o1{0, r0 * std::cos(a1), r0 * std::sin(a1)};
        Vec3 p0{0, r1 * std::cos(a0), r1 * std::sin(a0)}, p1{0, r1 * std::cos(a1), r1 * std::sin(a1)};
        const double w = 0.065;
        m.quad(p0 + Vec3{-w, 0, 0}, p0 + Vec3{w, 0, 0}, p1 + Vec3{w, 0, 0}, p1 + Vec3{-w, 0, 0}, kPaint1);
        m.quad(o0 + Vec3{w, 0, 0}, o0 + Vec3{-w, 0, 0}, o1 + Vec3{-w, 0, 0}, o1 + Vec3{w, 0, 0}, kPaint1);
    }
    for (int s = -1; s <= 1; s += 2)   // stays from the sliders to the mudguard's edges
        m.box({s < 0 ? -kForkLegX + 0.03 : 0.06, r0 - 0.012, -0.012}, {s < 0 ? -0.06 : kForkLegX - 0.03, r0 + 0.004, 0.012}, kMid);
    return m;
}

// ------------------------------------------------------------------------------------------- swingarm
inline double swingarm_length() { return length(swingarm_tuning().tyre - kSwingarmPivot); }
// Static: the pivot bracket on the frame's back face and the shock's top bracket.
inline Mesh swingarm_base() {
    Mesh m;
    m.box({-0.12, -0.08, -0.04}, {0.12, 0.08, 0.06}, kPaint0);
    m.box({-0.035, 0.08, -0.04}, {0.035, kShockTop.y + 0.03, kShockTop.z + 0.03}, kPaint0);
    m.prism(0, kShockTop, 0.016, -0.05, 0.05, kBolt, 6);
    for (int s = -1; s <= 1; s += 2) m.prism(2, {s * 0.08, 0.05, 0.06}, 0.009, 0, 0.008, kBolt, 6);
    return m;
}
// Swings about the pivot (origin); +z runs to the axle.
inline Mesh swingarm_arm() {
    Mesh m;
    double len = swingarm_length();
    m.prism(0, {0, 0, 0}, 0.034, -0.13, 0.13, kIron);
    for (int s = -1; s <= 1; s += 2) {
        m.box({s * 0.10 - 0.016, -0.032, 0.0}, {s * 0.10 + 0.016, 0.032, len + 0.035}, kPaint0);
        m.box({s * 0.10 - 0.02, -0.02, len - 0.03}, {s * 0.10 + 0.02, 0.02, len + 0.04}, kIron);   // chain adjuster
    }
    m.box({-0.10, -0.026, 0.03}, {0.10, 0.026, 0.12}, kPaint0);
    m.box({-0.03, 0.026, len * kShockAlongArm - 0.03}, {0.03, 0.06, len * kShockAlongArm + 0.03}, kPaint0);   // shock lug
    m.prism(0, {0, 0, len}, 0.012, -0.13, 0.13, kMid, 8);
    return m;
}
// The shock, along +z from 0 (top mount) to 1 (arm lug): stretched to fit each frame, so the spring compresses.
inline Mesh swingarm_shock() {
    Mesh m;
    m.prism(2, {0, 0, 0}, 0.026, 0.0, 0.55, kIron, 10);
    m.prism(2, {0, 0, 0}, 0.011, 0.5, 1.0, kChrome, 8);
    m.box({-0.022, -0.022, 0.94}, {0.022, 0.022, 1.0}, kIron);
    for (int k = 0; k < 8; ++k) {
        double z = 0.08 + k * 0.11;
        m.prism(2, {0, 0, 0}, 0.04, z, z + 0.035, kPaint1, 10);
    }
    return m;
}

inline Mesh hidden_speck() { Mesh m; m.box({-0.0005, -0.0005, -0.0005}, {0.0005, 0.0005, 0.0005}, kIron); return m; }

// ------------------------------------------------------------------------------------------- lean assist
// One cell wide, two tall (the gyro sensor's zone): a gyro drum with an amber arrow on top pointing forward (+z).
inline Mesh lean_assist() {
    Mesh m;
    m.box({-0.04, -0.04, -0.04}, {0.04, 0.02, 0.04}, kIron);
    m.prism(1, {0, 0, 0}, 0.036, 0.02, 0.09, kPaint0, 12);
    m.prism(1, {0, 0, 0}, 0.039, 0.045, 0.06, kPaint1, 12);
    m.prism(1, {0, 0, 0}, 0.012, 0.09, 0.105, kBolt, 6);
    double y = 0.1052;
    Vec3 up{0, 1, 0};
    auto flat_tri = [&](Vec3 a, Vec3 b, Vec3 c) { m.tri(m.vertex(a, up, kAmber), m.vertex(b, up, kAmber), m.vertex(c, up, kAmber)); };
    flat_tri({0, y, 0.034}, {0.016, y, 0.014}, {-0.016, y, 0.014});
    m.quad({-0.006, y, 0.014}, {0.006, y, 0.014}, {0.006, y, -0.02}, {-0.006, y, -0.02}, kAmber);
    return m;
}

// ------------------------------------------------------------------------------------------- seat
// A saddle on a 3 x 2 x 7 cell base: pan, cushion with a raised back, a narrow nose at the front (+z).
inline Mesh seat() {
    Mesh m;
    m.box({-0.11, -0.12, -0.27}, {0.11, -0.07, 0.27}, kIron);
    m.box({-0.115, -0.07, -0.26}, {0.115, 0.03, 0.06}, kPaint0);
    m.box({-0.08, -0.07, 0.06}, {0.08, 0.015, 0.20}, kPaint0);
    m.box({-0.05, -0.07, 0.20}, {0.05, 0.0, 0.27}, kPaint0);
    m.box({-0.115, 0.03, -0.26}, {0.115, 0.08, -0.19}, kPaint1);
    for (int s = -1; s <= 1; s += 2) m.box({s * 0.116 - 0.002, -0.05, -0.25}, {s * 0.116 + 0.002, 0.02, 0.04}, kPaint1);   // piping
    return m;
}

// ------------------------------------------------------------------------------------------- handlebars
// Static: a riser from the seat's control face up to the steering head.
inline Mesh handlebars_base() {
    Mesh m;
    m.box({-0.035, -0.12, -0.04}, {0.035, -0.06, 0.04}, kPaint0);
    m.prism(1, {0, 0, kBarsPivot.z}, 0.022, -0.06, kBarsPivot.y - 0.03, kIron);
    m.box({-0.045, kBarsPivot.y - 0.07, kBarsPivot.z - 0.04}, {0.045, kBarsPivot.y - 0.025, kBarsPivot.z + 0.05}, kPaint1);   // headlight shell
    m.box({-0.03, kBarsPivot.y - 0.065, kBarsPivot.z + 0.05}, {0.03, kBarsPivot.y - 0.03, kBarsPivot.z + 0.055}, kAmber);
    return m;
}
// Turns about the raked steering axis (origin, +y): clamp, bar, grips, levers and a small dial.
inline Mesh handlebars_bars() {
    Mesh m;
    m.box({-0.045, -0.025, -0.035}, {0.045, 0.035, 0.035}, kIron);
    Vec3 g = kGripRight;
    m.prism(0, {0, g.y, g.z}, 0.011, -g.x + 0.07, g.x - 0.07, kChrome, 8);
    for (int s = -1; s <= 1; s += 2) {
        m.prism(0, {0, g.y, g.z}, 0.017, s < 0 ? -g.x - 0.055 : g.x - 0.07, s < 0 ? -g.x + 0.07 : g.x + 0.055, kRubber, 10);
        m.box({s * g.x - 0.04, g.y - 0.006, g.z + 0.03}, {s * g.x + 0.04, g.y + 0.004, g.z + 0.04}, kMid);   // lever
        double inner = s * (g.x - 0.07), block = s * (g.x - 0.035);   // switch block, inboard of the grip
        m.box({std::min(inner, block), g.y - 0.012, g.z - 0.01}, {std::max(inner, block), g.y + 0.02, g.z + 0.035}, kIron);
    }
    m.box({-0.035, 0.035, -0.03}, {0.035, 0.07, 0.03}, kIron);
    m.box({-0.028, 0.0705, -0.024}, {0.028, 0.072, 0.024}, kTeal);
    return m;
}

}  // namespace anymoto
