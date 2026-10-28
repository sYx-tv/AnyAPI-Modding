#pragma once
// AnyBattery's Battery Pack mesh, generated in code (no game asset ships with the mod) and written into
// rom/meshes/components at start-up, like the other part mods.
//
// The look is the stock Battery (battery_a) in its cradle, scaled up by stretching. The pack stretches like the liquid
// tank ("stretch" on every axis, center_stretch 0.5): the game moves every vertex past the middle of the unstretched
// part (0.04 m on each axis, the part spanning -0.04 to 0.12) out by the added cells. So the block and tray grow
// with it, the red terminal and one strap stay at the near x end, the blue terminal and the other strap ride out with
// the far end, and nothing sits on a middle plane.
//
// Mesh file layout (as the GpsPart, Rotary and AnyMoto meshes): "mesh" u32 5 u32 1 u32 name_length name, 88-byte
// attribute header, 6 x f64 bounds, u32 vertex bytes + 36-byte vertices (f32 pos[3], u8 rgba[4], f32 uv[2],
// f32 normal[3]), u32 index bytes + u32 triangle indices, 8-byte trailer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace anybattery {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCell = 0.08;
constexpr double kMiddle = 0.04;           // the stretch plane on each axis
struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalized(Vec3 a) {
    double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 0 ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0, 1, 0};
}

struct MeshVertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(MeshVertex) == 36, "mesh vertex");

struct Rgb { uint8_t r, g, b; };
// The stock battery's colours (rom/meshes/components/battery_a.mesh and battery_cradle_a.mesh): a dark grey block,
// a rust red and a slate blue terminal, and hold-down straps in paint slot 1 like the stock cradle's.
constexpr Rgb kDark{51, 51, 51};
constexpr Rgb kTerminalRed{145, 30, 21};
constexpr Rgb kTerminalBlue{58, 80, 103};
constexpr Rgb kPaint1{153, 37, 0};         // paint slot 1 (within 2 of G_COLOR_REPLACEMENTS[1], as the stock cradle)

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
    // a, b, c counter-clockwise seen from outside; stored the other way round, as the game draws clockwise faces.
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.push_back(a); i.push_back(c); i.push_back(b); }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb col) {
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
    // An upright block over the rectangle [x0, x1] x [z0, z1], from y0 to y1, its vertical edges cut by c and its top
    // (and bottom, if asked) edges bevelled by c, like the stock battery's.
    void chamfer_block(double x0, double x1, double z0, double z1, double y0, double y1, double c, Rgb col, bool bevel_bottom = false) {
        auto ring = [&](double y, double in) {
            double a0 = x0 + in, a1 = x1 - in, b0 = z0 + in, b1 = z1 - in, k = c;
            return std::vector<Vec3>{{a0 + k, y, b0}, {a1 - k, y, b0}, {a1, y, b0 + k}, {a1, y, b1 - k},
                                     {a1 - k, y, b1}, {a0 + k, y, b1}, {a0, y, b1 - k}, {a0, y, b0 + k}};
        };
        std::vector<std::vector<Vec3>> rings;
        if (bevel_bottom) rings.push_back(ring(y0, c));
        rings.push_back(ring(bevel_bottom ? y0 + c : y0, 0));
        rings.push_back(ring(y1 - c, 0));
        rings.push_back(ring(y1, c));
        Vec3 centre{(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2};
        for (size_t r = 0; r + 1 < rings.size(); ++r)
            for (int k = 0; k < 8; ++k) quad_out(rings[r][k], rings[r][(k + 1) % 8], rings[r + 1][(k + 1) % 8], rings[r + 1][k], centre, col);
        cap(rings.back(), {0, 1, 0}, col);
        cap(rings.front(), {0, -1, 0}, col);
    }
    // A quad whose winding is chosen so its normal points away from `centre`.
    void quad_out(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 centre, Rgb col) {
        Vec3 n = cross(b - a, d - a);
        Vec3 mid{(a.x + c.x) / 2 - centre.x, (a.y + c.y) / 2 - centre.y, (a.z + c.z) / 2 - centre.z};
        if (n.x * mid.x + n.y * mid.y + n.z * mid.z < 0) quad(a, d, c, b, col);
        else quad(a, b, c, d, col);
    }
    // A flat convex polygon facing `normal`, fanned from its first corner (a centre vertex could land on a stretch
    // plane).
    void cap(const std::vector<Vec3>& pts, Vec3 normal, Rgb col) {
        for (size_t k = 1; k + 1 < pts.size(); ++k) {
            const Vec3 &a = pts[0], &p = pts[k], &q = pts[k + 1];
            Vec3 n = cross(p - a, q - a);
            if (n.x * n.x + n.y * n.y + n.z * n.z < 1e-18) continue;   // a corner cut of 0 repeats points
            if (n.x * normal.x + n.y * normal.y + n.z * normal.z < 0) quad_tri(a, q, p, normal, col);
            else quad_tri(a, p, q, normal, col);
        }
    }
    void quad_tri(Vec3 a, Vec3 b, Vec3 c, Vec3 n, Rgb col) { tri(vertex(a, n, col), vertex(b, n, col), vertex(c, n, col)); }
};

// The stock battery, cell for cell, sitting in the stock cradle's tray under two painted hold-down straps: a dark
// block with cut vertical edges and a bevelled top, a channel across the front of the top with a square red terminal
// at the near end and a slate blue one at the far x end, and straps over the top near each x end. Changes from
// stock, so it stretches cleanly: the tray top, the terminals and the channel sit off the middle planes, and the
// terminals sit beside the straps rather than under them.
inline Mesh battery_pack() {
    Mesh m;
    m.chamfer_block(-0.04, 0.12, -0.04, 0.12, -0.04, 0.035, 0.01, kDark, true);           // tray
    m.chamfer_block(-0.03, 0.11, -0.03, 0.11, -0.03, 0.10, 0.01, kDark);                   // block
    m.chamfer_block(-0.025, 0.105, -0.025, 0.0, 0.095, 0.11, 0.004, kDark);               // top, in front of the channel
    m.chamfer_block(-0.025, 0.105, 0.03, 0.105, 0.095, 0.11, 0.004, kDark);                 // top, behind it
    // Square terminals in the channel (z 0 to 0.03): a base plate and a post, as the stock ones.
    for (auto [x, col] : {std::pair<double, Rgb>{0.0125, kTerminalRed}, {0.0675, kTerminalBlue}}) {
        m.box({x - 0.0125, 0.10, 0.0025}, {x + 0.0125, 0.103, 0.0275}, col);
        m.box({x - 0.0097, 0.103, 0.0053}, {x + 0.0097, 0.115, 0.0247}, col);
    }
    // Hold-down straps (paint slot 1) at each x end: up the front and back from the tray and over the top.
    for (double x0 : {-0.0305, 0.0855}) {
        double x1 = x0 + 0.025;
        m.box({x0, 0.035, -0.04}, {x1, 0.11, -0.03}, kPaint1);
        m.box({x0, 0.035, 0.11}, {x1, 0.11, 0.12}, kPaint1);
        m.box({x0, 0.11, -0.04}, {x1, 0.12, 0.12}, kPaint1);
    }
    return m;
}

// Every vertex of the mesh, with which side of the stretch plane it is on, for the test: nothing may sit on it.
inline bool clear_of_middle(const Mesh& m, double margin = 0.0005) {
    for (auto& x : m.v)
        for (int k = 0; k < 3; ++k)
            if (std::fabs(double(x.pos[k]) - kMiddle) < margin) return false;
    return true;
}

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

}  // namespace anybattery
