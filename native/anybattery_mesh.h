#pragma once
// AnyBattery's Battery Pack mesh, generated in code (no game asset ships with the mod) and written into
// rom/meshes/components at start-up, like the other part mods.
//
// The pack stretches like the liquid tank ("stretch" on every axis, center_stretch 0.5): the game moves every vertex
// past the middle of the unstretched part (0.04 m on each axis, the part spanning -0.04 to 0.12) out by the added
// cells. So the case and lid grow with it, the negative terminal stays at the near corner, the positive one rides
// out with the far x end, and nothing sits on a middle plane.
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
constexpr Rgb kPaint0{153, 0, 0};          // paint slot 0: lid and band (G_COLOR_REPLACEMENTS[0])
constexpr Rgb kPaint1{153, 38, 0};         // paint slot 1: vent caps
constexpr Rgb kCase{38, 38, 42};
constexpr Rgb kLead{150, 150, 146};
constexpr Rgb kPlusRed{204, 30, 24};
constexpr Rgb kMinusBlack{18, 18, 20};

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
    // An upright octagonal post centred on (x, z), from y0 to y1, with a flat top.
    void post(double x, double z, double r, double y0, double y1, Rgb c) {
        Vec3 top{x, y1, z};
        for (int k = 0; k < 8; ++k) {
            double a0 = kPi / 8 + 2 * kPi * k / 8, a1 = kPi / 8 + 2 * kPi * (k + 1) / 8;
            Vec3 p0{x + r * std::cos(a0), y0, z + r * std::sin(a0)}, p1{x + r * std::cos(a1), y0, z + r * std::sin(a1)};
            Vec3 q0{p0.x, y1, p0.z}, q1{p1.x, y1, p1.z};
            quad(p0, q0, q1, p1, c);
            uint32_t t = vertex(top, {0, 1, 0}, c), b = vertex(q0, {0, 1, 0}, c), d = vertex(q1, {0, 1, 0}, c);
            tri(t, d, b);
        }
    }
};

// Case from the floor to just under the lid, a painted lid, a painted band low round the case, two terminals on
// the lid (black negative at the near corner, red positive at the far x end), two vent caps and corner ribs.
inline Mesh battery_pack() {
    Mesh m;
    const double lo = -0.0365, hi = 0.1165;                         // case sides, inside the part's -0.04..0.12
    m.box({lo, -0.04, lo}, {hi, 0.096, hi}, kCase);
    m.box({lo - 0.002, 0.096, lo - 0.002}, {hi + 0.002, 0.106, hi + 0.002}, kPaint0);   // lid
    m.box({lo - 0.001, -0.022, lo - 0.001}, {hi + 0.001, -0.012, hi + 0.001}, kPaint0); // band
    for (double x : {lo - 0.0015, hi - 0.0055})                      // corner ribs on the front and back faces
        for (double z : {lo - 0.0015, hi - 0.0055}) m.box({x, -0.04, z}, {x + 0.007, 0.096, z + 0.007}, kCase);
    // Terminals: a coloured collar and a lead post, both in the front half (z < 0.04) so they never stretch in z.
    m.post(-0.012, -0.012, 0.0105, 0.106, 0.110, kMinusBlack);
    m.post(-0.012, -0.012, 0.0065, 0.110, 0.1185, kLead);
    m.post(0.092, -0.012, 0.0105, 0.106, 0.110, kPlusRed);
    m.post(0.092, -0.012, 0.0065, 0.110, 0.1185, kLead);
    // Vent caps in the back half: they ride out with the far z end.
    for (double x : {0.0, 0.08}) m.post(x, 0.085, 0.006, 0.106, 0.1115, kPaint1);
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
