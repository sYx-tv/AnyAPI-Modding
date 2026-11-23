#pragma once
// AnyAeronautics part meshes: the straight Cone Nozzle and Fighter Nozzle Nate picked on 2026-11-23 (options 1 and 3
// of the three shown), built from simple shapes at start-up. The file header (format version, attribute layout,
// trailer) is copied from the game's own jet_exhaust_a.mesh, so no game asset ships with the mod (as AnyLaser does).
//
// Mesh file layout (anylaser_mesh.h): "mesh" u32 version(5) u32 (1) u32 name_length, name, 32-byte attribute header,
// 56 zero bytes, 6 x f64 bounds, u32 vertex_bytes + 36-byte vertices (f32 pos[3], u8 rgba[4], f32 uv[2], f32 n[3]),
// u32 index_bytes + u32 indices, 8-byte trailer.
//
// Both nozzles sit where the stock exhaust sits: the engine face is the +Z end (z = 0.28, an octagon of radius 0.28
// like the stock part) and the outlet points straight back along -Z, ending at z = -0.41 (stock: -0.40). Every face is
// written on both sides, so the winding the game culls by can never hide one.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace anyaeronautics {

struct MeshVertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(MeshVertex) == 36, "mesh vertex");
struct MeshLayout { size_t bounds = 0, vertex_bytes = 0, index_bytes = 0, trailer = 0; };

inline bool read_u32(const std::vector<uint8_t>& b, size_t at, uint32_t& out) {
    if (at + 4 > b.size()) return false;
    std::memcpy(&out, b.data() + at, 4); return true;
}
inline bool parse_mesh(const std::vector<uint8_t>& b, MeshLayout& m) {
    uint32_t version = 0, name = 0, vbytes = 0, ibytes = 0;
    if (b.size() < 16 || std::memcmp(b.data(), "mesh", 4) != 0 || !read_u32(b, 4, version) || version != 5 || !read_u32(b, 12, name) || name > 256) return false;
    m.bounds = 16 + size_t(name) + 32 + 56;
    m.vertex_bytes = m.bounds + 48;
    if (!read_u32(b, m.vertex_bytes, vbytes) || vbytes % sizeof(MeshVertex) || vbytes == 0) return false;
    m.index_bytes = m.vertex_bytes + 4 + vbytes;
    if (!read_u32(b, m.index_bytes, ibytes) || ibytes % 12) return false;
    m.trailer = m.index_bytes + 4 + ibytes;
    return m.trailer + 8 == b.size();
}

struct P3 { double x, y, z; };
struct Rgb { uint8_t r, g, b; };

struct MeshBuilder {
    std::vector<MeshVertex> verts;
    std::vector<uint32_t> idx;
    uint32_t vertex(P3 p, P3 n, Rgb c) {
        MeshVertex v{};
        v.pos[0] = float(p.x); v.pos[1] = float(p.y); v.pos[2] = float(p.z);
        v.rgba[0] = c.r; v.rgba[1] = c.g; v.rgba[2] = c.b; v.rgba[3] = 255;
        v.normal[0] = float(n.x); v.normal[1] = float(n.y); v.normal[2] = float(n.z);
        verts.push_back(v);
        return uint32_t(verts.size() - 1);
    }
    // One flat triangle, written on both sides with matching normals.
    void tri(P3 a, P3 b, P3 c, Rgb col) {
        P3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
        P3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (!(l > 1e-12)) return;
        n = {n.x / l, n.y / l, n.z / l};
        P3 m{-n.x, -n.y, -n.z};
        uint32_t a0 = vertex(a, n, col), b0 = vertex(b, n, col), c0 = vertex(c, n, col);
        idx.insert(idx.end(), {a0, c0, b0});
        uint32_t a1 = vertex(a, m, col), b1 = vertex(b, m, col), c1 = vertex(c, m, col);
        idx.insert(idx.end(), {a1, b1, c1});
    }
    void quad(P3 a, P3 b, P3 c, P3 d, Rgb col) { tri(a, b, c, col); tri(a, c, d, col); }
};

constexpr double kPi = 3.14159265358979;
inline std::vector<P3> ring(double r, double z, int n, double phase) {
    std::vector<P3> out;
    for (int k = 0; k < n; ++k) { double a = 2 * kPi * (k + phase) / n; out.push_back({r * std::cos(a), r * std::sin(a), z}); }
    return out;
}
// A surface of revolution through (radius, z) stations; colours[i] paints the band from station i to i + 1.
struct Station { double r, z; };
inline std::vector<P3> profile(MeshBuilder& b, const std::vector<Station>& s, int n, const std::vector<Rgb>& colours, double phase) {
    std::vector<P3> prev = ring(s[0].r, s[0].z, n, phase), first = prev;
    for (size_t i = 1; i < s.size(); ++i) {
        std::vector<P3> next = ring(s[i].r, s[i].z, n, phase);
        for (int k = 0; k < n; ++k) b.quad(prev[k], next[k], next[(k + 1) % n], prev[(k + 1) % n], colours[i - 1]);
        prev = next;
    }
    return prev;
}
inline void cap(MeshBuilder& b, const std::vector<P3>& r, Rgb c) {
    P3 centre{0, 0, r[0].z};
    for (size_t k = 0; k < r.size(); ++k) b.tri(centre, r[k], r[(k + 1) % r.size()], c);
}

constexpr Rgb kSteel{150, 154, 160}, kSteelDark{48, 50, 54}, kSoot{30, 30, 32}, kGun{88, 92, 98}, kGunLight{110, 114, 120}, kHot{60, 40, 30};

enum class Look { Cone, Fighter };

// Option 1: a grey octagonal cone tapering from the engine face to a round dark outlet.
inline void build_cone(MeshBuilder& b) {
    std::vector<P3> front = ring(0.28, 0.28, 8, 0.5);
    cap(b, front, kSteel);
    profile(b, {{0.28, 0.28}, {0.28, 0.16}, {0.27, 0.10}, {0.19, -0.40}}, 8, {kSteel, kSteelDark, kSteel}, 0.5);
    std::vector<P3> inner = profile(b, {{0.19, -0.40}, {0.16, -0.38}, {0.14, -0.05}}, 24, {kSteelDark, kSoot}, 0);
    cap(b, inner, kSoot);
}
// Option 3: a dark gunmetal body with a ring of twelve overlapping petals and a saw-tooth outlet.
inline void build_fighter(MeshBuilder& b) {
    std::vector<P3> front = ring(0.28, 0.28, 24, 0.5);
    cap(b, front, kGun);
    profile(b, {{0.28, 0.28}, {0.28, 0.12}, {0.26, 0.10}, {0.26, -0.10}}, 24, {kGun, kSteelDark, kGun}, 0.5);
    constexpr int kPetals = 12;
    for (int k = 0; k < kPetals; ++k) {
        double a0 = 2 * kPi * k / kPetals, a1 = 2 * kPi * (k + 1) / kPetals, am = (a0 + a1) / 2;
        P3 p0{0.26 * std::cos(a0), 0.26 * std::sin(a0), -0.10}, p1{0.26 * std::cos(a1), 0.26 * std::sin(a1), -0.10};
        P3 q0{0.215 * std::cos(a0 + 0.06), 0.215 * std::sin(a0 + 0.06), -0.36}, q1{0.215 * std::cos(a1 - 0.06), 0.215 * std::sin(a1 - 0.06), -0.36};
        P3 tip{0.205 * std::cos(am), 0.205 * std::sin(am), -0.41};
        Rgb c = k % 2 ? kGun : kGunLight;
        b.quad(p0, q0, q1, p1, c);
        b.tri(q0, tip, q1, c);
    }
    std::vector<P3> inner = profile(b, {{0.20, -0.36}, {0.17, -0.30}}, 24, {kSoot}, 0);
    cap(b, inner, kHot);
}

inline std::vector<uint8_t> encode_mesh(const std::vector<uint8_t>& stock, const MeshBuilder& b, const std::string& name) {
    MeshLayout m;
    if (!parse_mesh(stock, m) || b.verts.empty()) return {};
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : b.verts) for (int j = 0; j < 3; ++j) { bounds[j] = std::fmin(bounds[j], v.pos[j]); bounds[3 + j] = std::fmax(bounds[3 + j], v.pos[j]); }
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + k); };
    uint32_t u;
    put(stock.data(), 12);                                   // magic, version, (1)
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);                   // attribute header + reserved bytes
    put(bounds, sizeof bounds);
    u = uint32_t(b.verts.size() * sizeof(MeshVertex)); put(&u, 4); put(b.verts.data(), b.verts.size() * sizeof(MeshVertex));
    u = uint32_t(b.idx.size() * 4); put(&u, 4); put(b.idx.data(), b.idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

inline std::vector<uint8_t> build_nozzle_mesh(const std::vector<uint8_t>& stock, Look look, const std::string& name) {
    MeshBuilder b;
    if (look == Look::Cone) build_cone(b); else build_fighter(b);
    return encode_mesh(stock, b, name);
}

}  // namespace anyaeronautics
