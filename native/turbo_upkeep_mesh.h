#pragma once
// Air filter and intercooler meshes, built at start-up like the valves' (turbo_valve_mesh.h), in the same stock look.
//
//   air filter, small: 1 cell, small pipe; large: 2 x 2 x 2 cells, large pipe. The neck goes down to the gas node
//     (direction 2), then a clamp band, a base cap, a pleated cone element (first colour) and a top cap (second
//     colour) with a bright badge.
//   intercooler: three tiles the game strings together along x (turbo_intercooler_<x>_0_0): tile 0 the left cell
//     (left end tank), tile 1 one cell of core (repeated per extra width), tile 2 the right cell (right end tank). Each
//     tile is one cell wide, 2 cells tall (y 0..1, which the game stretches) and one cell deep, modelled around its own
//     cell at x = 0, so the tanks keep their width however wide it gets. No ports: the player snaps gas ports on, as on
//     the radiator. Tanks take the first colour, their caps the second.
#include "turbo_valve_mesh.h"

namespace turbo {

static const uint8_t kCoreDark[3] = {30, 30, 32};

// A pleated cone along `axis`: `pleats` folds whose inner edges sit `depth` (fraction of the radius) in.
inline void pleated_cone(MeshBuilder& b, int axis, const double c[3], double r0, double r1, double w0, double w1, int pleats, double depth, const uint8_t col[3]) {
    const int n = pleats * 2;
    auto radius = [&](int k, double r) { return k % 2 ? r * (1 - depth) : r; };
    for (int k = 0; k < n; ++k) {
        double a0 = 2 * MeshBuilder::kPi * k / n, a1 = 2 * MeshBuilder::kPi * (k + 1) / n;
        double p[4][3];
        MeshBuilder::place(axis, c, radius(k, r0) * std::cos(a0), radius(k, r0) * std::sin(a0), w0, p[0]);
        MeshBuilder::place(axis, c, radius(k + 1, r0) * std::cos(a1), radius(k + 1, r0) * std::sin(a1), w0, p[1]);
        MeshBuilder::place(axis, c, radius(k + 1, r1) * std::cos(a1), radius(k + 1, r1) * std::sin(a1), w1, p[2]);
        MeshBuilder::place(axis, c, radius(k, r1) * std::cos(a0), radius(k, r1) * std::sin(a0), w1, p[3]);
        // the face's normal from its edges, turned outwards
        double u[3], v[3], nn[3];
        for (int i = 0; i < 3; ++i) { u[i] = p[1][i] - p[0][i]; v[i] = p[3][i] - p[0][i]; }
        nn[0] = u[1] * v[2] - u[2] * v[1]; nn[1] = u[2] * v[0] - u[0] * v[2]; nn[2] = u[0] * v[1] - u[1] * v[0];
        double mid[3], out[3];
        for (int i = 0; i < 3; ++i) mid[i] = (p[0][i] + p[1][i] + p[2][i] + p[3][i]) / 4;
        MeshBuilder::place(axis, c, 0, 0, (w0 + w1) / 2, out);
        double dot = 0, l = 0;
        for (int i = 0; i < 3; ++i) { dot += nn[i] * (mid[i] - out[i]); l += nn[i] * nn[i]; }
        l = std::sqrt(l) * (dot < 0 ? -1 : 1);
        if (l == 0) continue;
        uint32_t q[4];
        for (int j = 0; j < 4; ++j) q[j] = b.vertex(p[j][0], p[j][1], p[j][2], nn[0] / l, nn[1] / l, nn[2] / l, col);
        if (dot >= 0) { b.tri(q[0], q[1], q[3]); b.tri(q[1], q[2], q[3]); }     // counter-clockwise from outside
        else { b.tri(q[0], q[3], q[1]); b.tri(q[1], q[3], q[2]); }
    }
}

// Builds the small or large cone air filter.
inline std::vector<uint8_t> build_air_filter_mesh(const std::vector<uint8_t>& stock, bool large, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double u = large ? kCell : kCell / 2, c0 = large ? kCell / 2 : 0, rp = valve_pipe_radius(large);
    const double centre[3] = {c0, c0, c0};
    MeshBuilder b;
    // port frame under the part and the neck up into the filter
    oct(b, 1, centre, rp * 1.32, rp * 1.32, -u, -u + 0.14 * u, kValveIron);
    oct(b, 1, centre, rp * 0.72, rp * 0.72, -u - 0.002, -u + 0.001, kValveHole);
    oct(b, 1, centre, rp, rp, -u + 0.14 * u, -0.62 * u, kValveMid);
    oct(b, 1, centre, rp * 1.15, rp * 1.15, -0.62 * u, -0.5 * u, kValveAlloy);         // clamp band
    const double rb = 0.8 * u, rt = 0.6 * u;                                             // element: base and top radius
    b.frustum_phase(1, centre, rb * 1.04, rb * 1.04, -0.5 * u, -0.4 * u, kValvePaint1, 24, 0);   // base cap
    pleated_cone(b, 1, centre, rb, rt, -0.4 * u, 0.72 * u, 18, 0.12, kValvePaint0);
    b.frustum_phase(1, centre, rt * 0.95, rt * 0.95, -0.4 * u, 0.72 * u, kCoreDark, 18, 0);   // the dark inside, seen between pleats
    b.frustum_phase(1, centre, rt * 1.05, rt * 0.9, 0.72 * u, 0.86 * u, kValvePaint1, 24, 0);  // top cap
    b.frustum_phase(1, centre, rt * 0.42, rt * 0.42, 0.86 * u, 0.9 * u, kValveAlloy, 24, 0);    // badge
    return encode_valve_mesh(stock, m, b, new_name);
}

// Builds one intercooler tile (0 left end, 1 middle, 2 right end). Like a car's: an end tank on each side and the
// core between them, its tubes running across from tank to tank. No ports: gas ports snap onto its faces.
inline std::vector<uint8_t> build_intercooler_tile(const std::vector<uint8_t>& stock, int tile, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double h = kCell / 2, y0 = -h, y1 = h + kCell;      // the tile's own cell along x: -h..h; 2 cells tall
    const double td = 0.026, tank_w = 0.028, cd = 0.017;     // tank half depth and width, core half depth
    MeshBuilder b;
    double x0 = tile == 0 ? -h + tank_w : -h, x1 = tile == 2 ? h - tank_w : h;   // the core in this tile
    { double lo[3] = {x0, y0 + 0.008, -cd}, hi[3] = {x1, y1 - 0.008, cd}; b.box(lo, hi, kCoreDark); }
    for (int s = -1; s <= 1; s += 2) {                       // top and bottom rails
        double lo[3] = {x0, s < 0 ? y0 + 0.002 : y1 - 0.008, -cd - 0.002}, hi[3] = {x1, s < 0 ? y0 + 0.008 : y1 - 0.002, cd + 0.002};
        b.box(lo, hi, kValveIron);
    }
    for (int k = 0; k < 8; ++k) {                            // the tubes: bars across both faces, 4 per cell of height
        double y = y0 + 0.008 + (k + 0.5) * (y1 - y0 - 0.016) / 8;
        for (int s = -1; s <= 1; s += 2) {
            double lo[3] = {x0, y - 0.0025, s < 0 ? -cd - 0.0015 : cd}, hi[3] = {x1, y + 0.0025, s < 0 ? -cd : cd + 0.0015};
            b.box(lo, hi, kValveMid);
        }
    }
    if (tile == 0 || tile == 2) {                            // end tank, its caps top and bottom, a weld seam to the core
        double tx0 = tile == 0 ? -h : h - tank_w, tx1 = tile == 0 ? -h + tank_w : h;
        { double lo[3] = {tx0, y0 + 0.008, -td}, hi[3] = {tx1, y1 - 0.008, td}; b.box(lo, hi, kValvePaint0); }
        for (int s = -1; s <= 1; s += 2) {
            double lo[3] = {tx0 - (tile == 0 ? 0 : 0.002), s < 0 ? y0 : y1 - 0.008, -td - 0.002}, hi[3] = {tx1 + (tile == 2 ? 0 : 0.002), s < 0 ? y0 + 0.008 : y1, td + 0.002};
            b.box(lo, hi, kValvePaint1);
        }
        double seam = tile == 0 ? tx1 : tx0;
        double lo[3] = {seam - 0.0015, y0 + 0.008, -td - 0.001}, hi[3] = {seam + 0.0015, y1 - 0.008, td + 0.001};
        b.box(lo, hi, kValveIron);
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

}  // namespace turbo
