#pragma once
// Rotary engine meshes, generated at start-up (no game asset ships with the mod): the three tiles the game strings
// together for a stretched engine, rotary_engine_0_0_0 (front cell), _0_0_1 (two middle cells, repeated per extra
// rotor) and _0_0_2 (back cell). The game places tile 0 on the first cell, tile 2 on the last and tile 1 on every
// 2 cells between, like the small engine's engine_block_a tiles.
//
// The engine is laid out along z in cells from its front face (e = 0) to its back face (e = 2 x rotors). Rotor k
// fills e = 2k..2k+2: an oval rotor housing between side plates, with two raised ribs and two spark plugs on the left.
// A flat deck runs over the housings and the covers are full blocks above the shaft, so every mounting face is flat
// and flush (manifolds of any size fit, as on the small engine). Front and rear covers carry the small engine's
// octagonal output socket, and an oil pan runs along the whole engine.
//   x: -0.12..0.12 m (3 cells), y: -0.12..0.36 m (6 cells), the output shaft at x = y = 0, like the small engine.
// Mesh file layout (Anymaker 0.1.24, as GpsPart and Turbo read it):
//   "mesh" u32 version(5) u32 (1) u32 name_length, name bytes
//   32-byte attribute header, 56 bytes (zero), 6 x f64 bounds (min xyz, max xyz)
//   u32 vertex_bytes, vertices of 36 bytes: f32 position[3], u8 rgba[4], f32 uv[2], f32 normal[3]
//   u32 index_bytes, u32 indices (triangle list, clockwise seen from outside), 8-byte trailer
#include "rotary_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rotary {

constexpr double kCell = 0.08;

struct MeshVertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(MeshVertex) == 36, "mesh vertex");

struct Rgb { uint8_t r, g, b; };
// The stock engine's palette, so the rotary sits with the game's parts.
constexpr Rgb kHousing{153, 0, 0}, kRib{153, 37, 0}, kIron{51, 51, 51}, kAlloy{198, 198, 198}, kPad{19, 102, 102};

struct Mesh {
    std::vector<MeshVertex> v;
    std::vector<uint32_t> i;
    uint32_t add(double x, double y, double z, double nx, double ny, double nz, Rgb c) {
        MeshVertex m{};
        m.pos[0] = float(x); m.pos[1] = float(y); m.pos[2] = float(z);
        m.rgba[0] = c.r; m.rgba[1] = c.g; m.rgba[2] = c.b; m.rgba[3] = 255;
        m.normal[0] = float(nx); m.normal[1] = float(ny); m.normal[2] = float(nz);
        v.push_back(m);
        return uint32_t(v.size() - 1);
    }
    // a, b, c counter-clockwise seen from outside; the game wants them clockwise.
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.push_back(a); i.push_back(c); i.push_back(b); }
};

// ------------------------------------------------------------------------------------------- outlines
struct P2 { double x, y; };
// The housing's outline: a rounded oval (superellipse) a bit pinched at the waist, centred above the shaft.
constexpr double kOvalA = 0.115, kOvalB = 0.166, kOvalY = 0.052;
constexpr int kOvalPoints = 16;              // faceted like the stock parts
inline std::vector<P2> oval(double grow) {
    std::vector<P2> out;
    const double a = kOvalA + grow, b = kOvalB + grow, p = 2.4;
    for (int k = 0; k < kOvalPoints; ++k) {
        double t = 6.283185307179586 * k / kOvalPoints, c = std::cos(t), s = std::sin(t);
        double x = a * (c < 0 ? -1 : 1) * std::pow(std::abs(c), 2.0 / p);
        double y = b * (s < 0 ? -1 : 1) * std::pow(std::abs(s), 2.0 / p);
        x *= 1.0 - 0.05 * (1.0 - (y / b) * (y / b));
        out.push_back({x, kOvalY + y});
    }
    return out;
}
inline std::vector<P2> circle(double cx, double cy, double r, int n = 32) {
    std::vector<P2> out;
    for (int k = 0; k < n; ++k) { double t = 6.283185307179586 * k / n; out.push_back({cx + r * std::cos(t), cy + r * std::sin(t)}); }
    return out;
}
// Octagon of corner radius r with a corner at `first_deg` and the rest mirrored about both axes (the stock engine's
// torque socket uses an irregular one, corners at 18 and 72 degrees, for its rim).
inline std::vector<P2> octagon(double r, double first_deg) {
    const double d = first_deg * 3.141592653589793 / 180.0, h = 3.141592653589793;
    const double t[8] = {d, h / 2 - d, h / 2 + d, h - d, h + d, 1.5 * h - d, 1.5 * h + d, 2 * h - d};
    std::vector<P2> out;
    for (double a : t) out.push_back({r * std::cos(a), r * std::sin(a)});
    return out;
}
// Half width of the housing (grow 0) at height y, for placing parts on its side.
inline double oval_half_width(double y) {
    double best = 0;
    auto o = oval(0);
    for (size_t k = 0; k < o.size(); ++k) {
        const P2 &p = o[k], &q = o[(k + 1) % o.size()];
        if ((p.y - y) * (q.y - y) <= 0 && p.y != q.y) {
            double x = p.x + (q.x - p.x) * (y - p.y) / (q.y - p.y);
            best = std::max(best, std::abs(x));
        }
    }
    return best;
}

// ------------------------------------------------------------------------------------------- primitives
// Window of the engine this tile shows, in cells (e), and where its first cell sits in the tile's own z.
struct Window { double e0, e1, origin; double z(double e) const { return (e - origin) * kCell - kCell * 0.5; } };

// Prism along z through outline `o` (counter-clockwise seen from +z), over e0..e1 clipped to the window. Ends cut
// by the window (seams to the next tile) or marked closed=false get no cap.
inline void prism(Mesh& m, const Window& w, const std::vector<P2>& o, double e0, double e1, Rgb c, bool cap0 = true, bool cap1 = true) {
    double a = std::max(e0, w.e0), b = std::min(e1, w.e1);
    if (b - a < 1e-9) return;
    cap0 = cap0 && e0 >= w.e0 - 1e-9;
    cap1 = cap1 && e1 <= w.e1 + 1e-9;
    double z0 = w.z(a), z1 = w.z(b);
    size_t n = o.size();
    uint32_t base = uint32_t(m.v.size());
    for (size_t k = 0; k < n; ++k) {
        const P2 &prev = o[(k + n - 1) % n], &next = o[(k + 1) % n];
        double tx = next.x - prev.x, ty = next.y - prev.y, l = std::sqrt(tx * tx + ty * ty);
        double nx = l > 0 ? ty / l : 0, ny = l > 0 ? -tx / l : 0;   // outward for a counter-clockwise outline
        m.add(o[k].x, o[k].y, z0, nx, ny, 0, c);
        m.add(o[k].x, o[k].y, z1, nx, ny, 0, c);
    }
    for (size_t k = 0; k < n; ++k) {
        uint32_t p0 = base + uint32_t(2 * k), p1 = p0 + 1, q0 = base + uint32_t(2 * ((k + 1) % n)), q1 = q0 + 1;
        m.tri(p0, q0, q1); m.tri(p0, q1, p1);
    }
    double cx = 0, cy = 0;
    for (auto& p : o) { cx += p.x; cy += p.y; }
    cx /= double(n); cy /= double(n);
    for (int end = 0; end < 2; ++end) {
        if (end == 0 ? !cap0 : !cap1) continue;
        double z = end == 0 ? z0 : z1, nz = end == 0 ? -1 : 1;
        uint32_t centre = m.add(cx, cy, z, 0, 0, nz, c), first = uint32_t(m.v.size());
        for (auto& p : o) m.add(p.x, p.y, z, 0, 0, nz, c);
        for (size_t k = 0; k < n; ++k) {
            uint32_t p = first + uint32_t(k), q = first + uint32_t((k + 1) % n);
            if (end == 1) m.tri(centre, p, q); else m.tri(centre, q, p);
        }
    }
}

// Axis-aligned box over x0..x1, y0..y1 and engine cells e0..e1, clipped to the window like prism().
inline void box(Mesh& m, const Window& w, double x0, double x1, double y0, double y1, double e0, double e1, Rgb c) {
    double a = std::max(e0, w.e0), b = std::min(e1, w.e1);
    if (b - a < 1e-9) return;
    bool cap0 = e0 >= w.e0 - 1e-9, cap1 = e1 <= w.e1 + 1e-9;
    double z0 = w.z(a), z1 = w.z(b);
    struct Face { double n[3]; double p[4][3]; bool on; };
    const Face faces[6] = {
        {{-1, 0, 0}, {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}, true},
        {{1, 0, 0}, {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}, true},
        {{0, -1, 0}, {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}, true},
        {{0, 1, 0}, {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}, true},
        {{0, 0, -1}, {{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}, cap0},
        {{0, 0, 1}, {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, cap1},
    };
    for (auto& f : faces) {
        if (!f.on) continue;
        uint32_t q[4];
        for (int k = 0; k < 4; ++k) q[k] = m.add(f.p[k][0], f.p[k][1], f.p[k][2], f.n[0], f.n[1], f.n[2], c);
        m.tri(q[0], q[1], q[2]); m.tri(q[0], q[2], q[3]);
    }
}

// ------------------------------------------------------------------------------------------- the engine
// The mounting faces stay flat and flush with the engine's cells, so stock manifolds of any size sit on it the way they
// sit on the small engine: the top (y = 0.36) across the whole width and length, both sides (x = +-0.12) from y 0.20
// up (the side ports), the covers across x +-0.12 from y 0.12 up, flush pads on both sides at y 0 (the low side
// ports) and the oil pan along the bottom (the bottom port).
constexpr double kTop = 0.36, kSide = 0.12, kCover = 0.16;

// The lower half of the oval (left middle, through the bottom, to right middle) closed by a full-width block with
// chamfered top corners up to `top`: one convex outline whose upper part fills the mounting faces.
inline std::vector<P2> oval_with_block(double grow, double top = kTop, double ch = 0.008, double join = 0.10) {
    auto o = oval(grow);
    std::vector<P2> out;
    for (int k = kOvalPoints / 2; k <= kOvalPoints; ++k) out.push_back(o[k % kOvalPoints]);
    out.push_back({kSide, join}); out.push_back({kSide, top - ch}); out.push_back({kSide - ch, top});
    out.push_back({-kSide + ch, top}); out.push_back({-kSide, top - ch}); out.push_back({-kSide, join});
    return out;
}
// A flat slab across the top: y0..top, x +-side, chamfered top corners.
inline std::vector<P2> slab(double y0, double side = kSide, double top = kTop, double ch = 0.008) {
    return {{-side, y0}, {side, y0}, {side, top - ch}, {side - ch, top}, {-side + ch, top}, {-side, top - ch}};
}

// Builds what an engine of `rotors` rotors shows inside the window. The stock engines' look: painted housings (first
// colour), the covers, plates and deck in the second colour, dark iron ribs, gasket line and port pads, light bolt heads.
inline void build_engine(Mesh& m, const Window& w, int rotors) {
    const double length = 2.0 * rotors;
    // The output faces, front and back: the small engine's octagonal torque socket, ring for ring (alloy rim, iron
    // face, teal band, alloy collar, iron boss and the shaft stub into the 0.02 m lip), so gearboxes and engine wheels
    // look as they do on the stock engines. The torque mating surfaces are the stock engine's (rotary_logic.h).
    for (int end = 0; end < 2; ++end) {
        const double face = end == 0 ? 0.0 : length, out = end == 0 ? -1.0 : 1.0;
        struct Ring { std::vector<P2> o; double depth; Rgb c; };
        const Ring rings[] = {
            {octagon(0.1157, 18.07), 0.02, kAlloy},
            {octagon(0.0947, 17.0), 0.04, kIron},
            {octagon(0.0400, 22.5), 0.06, kPad},
            {octagon(0.0346, 22.5), 0.08, kAlloy},
            {octagon(0.0184, 22.5), 0.10, kIron},
            {octagon(0.01265, 22.3), 0.25, kIron},
        };
        for (const Ring& r : rings) {
            double a = face + out * r.depth;
            prism(m, w, r.o, std::min(face, a), std::max(face, a), r.c, end == 0, end == 1);
        }
    }
    // Front and rear covers full height, an intermediate plate between rotors up to the deck.
    const auto cover = oval_with_block(0.002), plate = oval_with_block(0.003, 0.19);
    prism(m, w, cover, 0.0, kCover, kRib);
    prism(m, w, cover, length - kCover, length, kRib);
    for (int k = 1; k < rotors; ++k) prism(m, w, plate, 2.0 * k - 0.12, 2.0 * k + 0.12, kRib);
    // Flat deck over the housings, an iron gasket line under it; flush bolt heads on the covers, clear of the socket.
    prism(m, w, slab(0.20), kCover, length - kCover, kRib, false, false);
    prism(m, w, slab(0.188, 0.116, 0.20, 0.0), kCover, length - kCover, kIron, false, false);
    for (double x : {-0.095, 0.095}) for (double y : {0.16, 0.33}) {
        box(m, w, x - 0.007, x + 0.007, y - 0.007, y + 0.007, -0.01, 0.0, kAlloy);
        box(m, w, x - 0.007, x + 0.007, y - 0.007, y + 0.007, length, length + 0.01, kAlloy);
    }
    // Oil pan underneath (the bottom port).
    box(m, w, -0.072, 0.072, -0.12, -0.085, kCover, length - kCover, kIron);
    const auto housing = oval(0), rib = oval(0.0025);
    const double in = oval_half_width(0.0) - 0.012;
    for (int k = 0; k < rotors; ++k) {
        double e0 = 2.0 * k, e1 = e0 + 2.0, c = e0 + 1.0;
        // Rotor housing between the side plates, with two raised ribs.
        prism(m, w, housing, e0 + (k == 0 ? kCover : 0.12), e1 - (k == rotors - 1 ? kCover : 0.12), kHousing, false, false);
        prism(m, w, rib, c - 0.57, c - 0.43, kIron);
        prism(m, w, rib, c + 0.43, c + 0.57, kIron);
        // Flush pads for the low side ports, both sides.
        box(m, w, -kSide, -in, -0.036, 0.036, c - 0.8, c + 0.8, kIron);
        box(m, w, in, kSide, -0.036, 0.036, c - 0.8, c + 0.8, kIron);
        // Leading and trailing spark plugs on the left side.
        for (double y : {-0.06, -0.09}) {
            double x = -oval_half_width(y);
            box(m, w, x - 0.008, x + 0.004, y - 0.008, y + 0.008, c - 0.1, c + 0.1, kIron);
            box(m, w, std::max(-kSide, x - 0.013), x - 0.008, y - 0.005, y + 0.005, c - 0.06, c + 0.06, kAlloy);
        }
    }
}

// Tile 0: the front cell; tile 1: two middle cells; tile 2: the back cell.
inline Mesh build_tile(int tile) {
    Mesh m;
    if (tile == 0) build_engine(m, {-1.0, 1.0, 0.0}, 2);
    else if (tile == 1) build_engine(m, {1.0, 3.0, 1.0}, 3);
    else build_engine(m, {1.0, 3.0, 1.0}, 1);
    return m;
}

// The 32-byte attribute header of the game's static part meshes (position, colour, uv, normal), then 56 zero bytes.
inline const uint8_t* attribute_header() {
    static const uint8_t h[88] = {3, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    return h;
}

inline std::vector<uint8_t> encode_mesh(const Mesh& m, const std::string& name, const uint8_t* header88 = nullptr) {
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : m.v) for (int j = 0; j < 3; ++j) { bounds[j] = std::min(bounds[j], double(v.pos[j])); bounds[3 + j] = std::max(bounds[3 + j], double(v.pos[j])); }
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* b = static_cast<const uint8_t*>(p); out.insert(out.end(), b, b + k); };
    uint32_t u = 5, one = 1;
    put("mesh", 4); put(&u, 4); put(&one, 4);
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(header88 ? header88 : attribute_header(), 88);
    put(bounds, sizeof bounds);
    u = uint32_t(m.v.size() * sizeof(MeshVertex)); put(&u, 4); put(m.v.data(), m.v.size() * sizeof(MeshVertex));
    u = uint32_t(m.i.size() * 4); put(&u, 4); put(m.i.data(), m.i.size() * 4);
    uint8_t trailer[8]{};
    put(trailer, 8);
    return out;
}

inline std::string tile_name(int tile) { return std::string(kMeshBase) + "_0_0_" + char('0' + tile); }

}  // namespace rotary
