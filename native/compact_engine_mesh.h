#pragma once
// Compact engines (Nate 2026-11-08): the stock engines with their oil, coolant and fuel ports built into the block,
// so no manifold parts hang off them. This draws the port pads that mark each built-in port. They go on top of the
// stock engine's own mesh (read from the player's rom at start-up, never shipped).
//   kind: 0 oil, 1 coolant, 2 fuel. Each kind has its own colour, and a count of 1 / 2 / 3 ticks, so it reads even
//   without the colour.
//   style: 0 ring pad (stock-style octagon frame), 1 raised boss, 2 bolted plate with an arrow.
// A pad sits on a face whose normal is `axis` (0 x, 1 y, 2 z) times `sign`, centred at (u, w) in the other two axes
// (prism order: (y, z) for x, (x, z) for y, (x, y) for z), with the face at `a`.
#include "turbo_pd_mesh.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace compactmesh {
using pdmesh::Mesh;
using pdmesh::Rgb;
using pdmesh::V3;
using pdmesh::kPi;

enum Kind { kOil = 0, kCoolant = 1, kFuel = 2 };
// The look the Compact engines ship with, until Nate picks one of the three (2026-11-08 previews).
constexpr int kPadStyle = 0;
constexpr Rgb kKindColour[3] = {{214, 150, 28}, {38, 118, 214}, {196, 40, 36}};
constexpr Rgb kFrame{51, 51, 51}, kBoltGrey{198, 198, 198}, kBoreDark{18, 18, 20}, kMid{113, 113, 113};

inline std::vector<std::pair<double, double>> octagon(double cu, double cw, double r) {
    std::vector<std::pair<double, double>> o;
    for (int k = 0; k < 8; ++k) { double t = kPi / 8 + kPi / 4 * k; o.push_back({cu + r * std::cos(t), cw + r * std::sin(t)}); }
    return o;
}
// A slab on the face from `a` out by `h` (or in, for sign < 0), cross-section `ring`.
inline void slab(Mesh& m, int axis, double sign, double a, double h0, double h1, const std::vector<std::pair<double, double>>& ring, Rgb c, bool smooth = false) {
    double p = a + sign * h0, q = a + sign * h1;
    m.prism(axis, ring, std::min(p, q), std::max(p, q), c, smooth);
}
// Kind ticks: 1-3 short bars beside the pad, along u.
inline void ticks(Mesh& m, int axis, double sign, double a, double u, double w, int kind, Rgb c, double h) {
    for (int k = 0; k <= kind; ++k) {
        double du = u - 0.008 + 0.008 * k - 0.008 * (kind - 1) * 0.5 - 0.004 * (kind == 0 ? -1 : 0);
        slab(m, axis, sign, a, 0, h, pdmesh::rounded(du - 0.0022, w - 0.007, du + 0.0022, w + 0.007, 0.001, 1), c);
    }
}

inline void pad(Mesh& m, int style, int axis, double sign, double u, double w, double a, int kind) {
    const Rgb col = kKindColour[kind < 0 || kind > 2 ? 0 : kind];
    switch (style) {
    case 0: {   // ring pad: dark octagon frame flush with the face, a coloured ring, the bore; ticks above
        slab(m, axis, sign, a, 0, 0.004, octagon(u, w, 0.034), kFrame);
        slab(m, axis, sign, a, 0.004, 0.006, pdmesh::circle(u, w, 0.024, 32), col, true);
        slab(m, axis, sign, a, 0.006, 0.0068, pdmesh::circle(u, w, 0.016, 32), kBoreDark, true);
        ticks(m, axis, sign, a + sign * 0.004, u, w + 0.028, kind, col, 0.002);
        break;
    }
    case 1: {   // raised boss in the kind's colour with a polished lip; ticks on the boss's shoulder
        slab(m, axis, sign, a, 0, 0.010, pdmesh::circle(u, w, 0.027, 32), col, true);
        slab(m, axis, sign, a, 0.010, 0.013, pdmesh::circle(u, w, 0.020, 32), kBoltGrey, true);
        slab(m, axis, sign, a, 0.013, 0.0138, pdmesh::circle(u, w, 0.014, 32), kBoreDark, true);
        ticks(m, axis, sign, a, u, w - 0.035, kind, col, 0.004);
        break;
    }
    default: {  // bolted plate: dark square plate with a coloured border, four bolts, an arrow pointing in
        slab(m, axis, sign, a, 0, 0.003, pdmesh::rounded(u - 0.036, w - 0.036, u + 0.036, w + 0.036, 0.006, 3), col);
        slab(m, axis, sign, a, 0.003, 0.005, pdmesh::rounded(u - 0.030, w - 0.030, u + 0.030, w + 0.030, 0.004, 3), kMid);
        slab(m, axis, sign, a, 0.005, 0.0058, pdmesh::circle(u, w, 0.015, 28), kBoreDark, true);
        for (double du : {-0.024, 0.024})
            for (double dw : {-0.024, 0.024}) pdmesh::bolt(m, axis, u + du, w + dw, a + sign * 0.005, sign, 0.0032, kBoltGrey);
        // arrow: a chevron below the bore, pointing at it
        std::vector<std::pair<double, double>> arrow = {{u, w - 0.017}, {u + 0.009, w - 0.026}, {u + 0.004, w - 0.026}, {u + 0.004, w - 0.029},
                                                        {u - 0.004, w - 0.029}, {u - 0.004, w - 0.026}, {u - 0.009, w - 0.026}};
        slab(m, axis, sign, a, 0.005, 0.0062, arrow, col);
        ticks(m, axis, sign, a + sign * 0.005, u + 0.018, w + 0.0, kind, col, 0.0012);
        break;
    }
    }
}

}  // namespace compactmesh

namespace compactmesh {
// A game part mesh file (version 5, 36-byte vertices) read back into a Mesh, or false when it is not one.
inline bool decode(const std::vector<uint8_t>& b, Mesh& m) {
    auto u32 = [&](size_t at, uint32_t& out) { if (at + 4 > b.size()) return false; std::memcpy(&out, b.data() + at, 4); return true; };
    uint32_t version = 0, name = 0, vbytes = 0, ibytes = 0;
    if (b.size() < 16 || std::memcmp(b.data(), "mesh", 4) != 0 || !u32(4, version) || version != 5 || !u32(12, name) || name > 256) return false;
    size_t vb = 16 + size_t(name) + 88 + 48;
    if (!u32(vb, vbytes) || vbytes % sizeof(pdmesh::Vertex) || !vbytes) return false;
    size_t ib = vb + 4 + vbytes;
    if (!u32(ib, ibytes) || ibytes % 12 || ib + 4 + ibytes + 8 != b.size()) return false;
    m.v.resize(vbytes / sizeof(pdmesh::Vertex));
    std::memcpy(m.v.data(), b.data() + vb + 4, vbytes);
    m.i.resize(ibytes / 4);
    std::memcpy(m.i.data(), b.data() + ib + 4, ibytes);
    for (uint32_t x : m.i) if (x >= m.v.size()) return false;
    return true;
}
}  // namespace compactmesh
