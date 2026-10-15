#pragma once
// Air filter and intercooler meshes, built at start-up like the valves' (turbo_valve_mesh.h), in the same stock look.
//
//   air filter, small: 3 x 3 x 3 cells, small pipe; large: 4 x 4 x 4 cells, large pipe, centred. The neck goes down to the gas node
//     (direction 2), then a clamp band, a base cap, a pleated cone element (first colour) and a top cap (second
//     colour) with a bright badge.
//   intercooler: nine tiles the game lays out along x and y (turbo_intercooler_<x>_<y>_0): x tile 0 the left cell (left
//     end tank), 1 one cell of core (repeated per extra width), 2 the right cell (right end tank); y tile 0 the bottom
//     cell, 1 a middle cell (repeated per extra height), 2 the top cell. Each tile is one cell wide and tall and two
//     cells deep, modelled around its own cell at x = y = 0, so nothing on it stretches however big it gets. No ports:
//     the player snaps gas ports on, as on the radiator. Tanks take the first colour, their caps the second.
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

// Cells along each side of an air filter: the small (small pipe) is 3 x 3 x 3, the large (large pipe) 4 x 4 x 4, so
// each pipe sits in the middle of the bottom face (a 1-cell pipe needs an odd width, a 2 x 2 pipe an even one).
inline int air_filter_cells(bool large) { return large ? 4 : 3; }

// Builds the small or large cone air filter. The part's cells run 0..n-1 from its origin cell and the gas node sits
// under the middle of the bottom face (air_filter_entry), so the neck rises straight up the filter's axis.
inline std::vector<uint8_t> build_air_filter_mesh(const std::vector<uint8_t>& stock, bool large, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const int n = air_filter_cells(large);
    const double u = n * kCell / 2, c0 = (n - 1) * kCell / 2, rp = valve_pipe_radius(large);
    const double centre[3] = {c0, c0, c0};
    MeshBuilder b;
    // port frame under the part and the neck up into the filter
    oct(b, 1, centre, rp * 1.32, rp * 1.32, -u, -u + 0.05 * n * kCell, kValveIron);
    oct(b, 1, centre, rp * 0.72, rp * 0.72, -u - 0.002, -u + 0.001, kValveHole);
    oct(b, 1, centre, rp, rp, -u + 0.05 * n * kCell, -0.62 * u, kValveMid);
    oct(b, 1, centre, rp * 1.15, rp * 1.15, -0.62 * u, -0.5 * u, kValveAlloy);         // clamp band
    const double rb = 0.9 * u, rt = 0.66 * u;                                            // element: base and top radius
    b.frustum_phase(1, centre, rb * 1.04, rb * 1.04, -0.5 * u, -0.4 * u, kValvePaint1, 24, 0);   // base cap
    pleated_cone(b, 1, centre, rb, rt, -0.4 * u, 0.72 * u, large ? 28 : 24, 0.12, kValvePaint0);
    b.frustum_phase(1, centre, rt * 0.95, rt * 0.95, -0.4 * u, 0.72 * u, kCoreDark, 18, 0);   // the dark inside, seen between pleats
    b.frustum_phase(1, centre, rt * 1.05, rt * 0.9, 0.72 * u, 0.86 * u, kValvePaint1, 24, 0);  // top cap
    b.frustum_phase(1, centre, rt * 0.42, rt * 0.42, 0.86 * u, 0.9 * u, kValveAlloy, 24, 0);    // badge
    return encode_valve_mesh(stock, m, b, new_name);
}

// Intercooler looks (style): 0 the black race core, 1 the round-tank front mount. Nothing stretches: the game repeats the
// middle tiles along x and y, so every detail that must keep its shape (inlet stubs, rails, caps, brackets) sits in an
// edge tile and the fins run the full height of every tile.
constexpr int kIntercoolerStyleCount = 2;
constexpr int kIntercoolerStyle = 0, kIntercoolerRoundStyle = 1;   // Nate's picks: the race core and the round-tank part
static const uint8_t kCoreSilver[3] = {168, 170, 172}, kCoreBlack[3] = {22, 22, 24};

// Builds one intercooler tile: tx 0 left end, 1 middle, 2 right end; ty 0 bottom, 1 middle, 2 top. Like a car's: an
// end tank on each side and the core between them, two cells deep so a large gas port sits fully on any face. No
// ports: gas ports snap onto its faces.
inline std::vector<uint8_t> build_intercooler_tile(const std::vector<uint8_t>& stock, int tx, int ty, const char* new_name, int style = kIntercoolerStyle) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double h = kCell / 2, zc = kCell / 2;              // the tile's own cell along x and y: -h..h; z: -h..kCell + h
    const double tank_w = 0.028, td = 0.066, cd = 0.054;     // tank width, tank and core half depth (around zc)
    const bool end = tx == 0 || tx == 2, bottom = ty == 0, top = ty == 2;
    const double x0 = tx == 0 ? -h + tank_w : -h, x1 = tx == 2 ? h - tank_w : h;   // the core in this tile
    const double tx0 = tx == 0 ? -h : h - tank_w, tx1 = tx == 0 ? -h + tank_w : h;   // the end tank's x span
    const double e = 0.006;                                  // the top and bottom rails' height
    const double y0 = bottom ? -h + e : -h, y1 = top ? h - e : h;   // the core and fins between the rails
    MeshBuilder b;
    auto box = [&](double ax, double ay, double az, double bx, double by, double bz, const uint8_t c[3]) {
        double lo[3] = {ax, ay, az}, hi[3] = {bx, by, bz}; b.box(lo, hi, c);
    };
    // Vertical fins on both faces across [fx0, fx1]: `count` per cell width, `w` wide, standing `out` proud.
    auto fins_up = [&](double fx0, double fx1, double depth, int count, double w, double out, const uint8_t c[3]) {
        double pitch = kCell / count;
        for (double x = -h + pitch / 2; x < h; x += pitch) {
            if (x - w / 2 < fx0 - 1e-9 || x + w / 2 > fx1 + 1e-9) continue;
            box(x - w / 2, y0, zc - depth - out, x + w / 2, y1, zc - depth, c);
            box(x - w / 2, y0, zc + depth, x + w / 2, y1, zc + depth + out, c);
        }
    };
    // The rail along the bottom of the bottom tiles and the top of the top tiles, `lip` proud of the core.
    auto rails = [&](double depth, double lip, const uint8_t c[3]) {
        if (bottom) box(x0, -h, zc - depth - lip, x1, -h + e, zc + depth + lip, c);
        if (top) box(x0, h - e, zc - depth - lip, x1, h, zc + depth + lip, c);
    };
    if (style == kIntercoolerStyle) {                        // black race core: black core, bright fins, polished lips, inlet stubs
        box(x0, y0, zc - cd, x1, y1, zc + cd, kCoreBlack);
        fins_up(x0, x1, cd, 16, 0.0012, 0.0006, kValveAlloy);
        rails(cd, 0.003, kValveAlloy);
        if (end) {
            box(tx0, -h, zc - td, tx1, h, zc + td, kValvePaint0);
            if (bottom) box(tx0 - 0.001, -h - 0.002, zc - td - 0.002, tx1 + 0.001, -h + 0.005, zc + td + 0.002, kValvePaint1);
            if (top) box(tx0 - 0.001, h - 0.005, zc - td - 0.002, tx1 + 0.001, h + 0.002, zc + td + 0.002, kValvePaint1);
            if (bottom) {                                    // an inlet stub on each tank's front, in the bottom cell
                double c[3] = {(tx0 + tx1) / 2, -h + kCell * 0.45, zc};
                b.cylinder(2, c, 0.009, td, td + 0.012, kValveAlloy, 16);
                b.cylinder(2, c, 0.0105, td + 0.008, td + 0.010, kValveIron, 16);   // hose bead
            }
        }
    } else {                                                 // round-tank front mount: rounded end tanks, brackets, silver core
        const double cr = 0.046;                             // a slimmer core, so the tanks stand proud of it
        box(x0, y0, zc - cr, x1, y1, zc + cr, kCoreSilver);
        fins_up(x0, x1, cr, 12, 0.0018, 0.0007, kValveIron);
        rails(cr, 0.002, kValveIron);
        if (end) {                                           // a pill-shaped tank: a box between two half-round ends
            const double r = tank_w / 2, cx = (tx0 + tx1) / 2, ya = bottom ? -h + 0.008 : -h, yb = top ? h - 0.008 : h;
            box(tx0, ya, zc - td + r, tx1, yb, zc + td - r, kValvePaint0);
            for (int s = -1; s <= 1; s += 2) {
                double c[3] = {cx, 0, zc + s * (td - r)};
                b.frustum_phase(1, c, r, r, ya, yb, kValvePaint0, 16, 0);
                if (bottom) b.frustum_phase(1, c, r * 0.6, r * 0.82, -h, ya, kValvePaint1, 16, 0);   // domed end caps
                if (top) b.frustum_phase(1, c, r * 0.82, r * 0.6, yb, h, kValvePaint1, 16, 0);
            }
            if (bottom || top) {                             // a cap plate across the tank's ends, and a bracket out the back
                double yc = bottom ? -h + 0.004 : h - 0.004;
                box(tx0 + 0.002, yc - 0.004, zc - td + r, tx1 - 0.002, yc + 0.004, zc + td - r, kValvePaint1);
                double yk = bottom ? -h + kCell * 0.3 : h - kCell * 0.3;
                box(cx - 0.004, yk - 0.004, zc - td - 0.010, cx + 0.004, yk + 0.004, zc - td + r, kValveIron);
            }
        }
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

}  // namespace turbo
