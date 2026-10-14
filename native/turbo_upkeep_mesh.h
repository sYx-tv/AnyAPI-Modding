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

// Intercooler looks (style): 0 the first look (dark core, horizontal tubes), 1 silver bar-and-plate, 2 black race core,
// 3 framed tube-and-fin, 4 round-tank front mount. The game stretches the tiles' height and repeats the middle tile
// along x, so styles 1-4 run their fins up the full height (they stretch cleanly) and keep every detail that must not
// stretch (inlets, brackets) within the bottom or top cell.
constexpr int kIntercoolerStyleCount = 5;
constexpr int kIntercoolerStyle = 2, kIntercoolerRoundStyle = 4;   // Nate's picks: the race core and the round-tank part
static const uint8_t kCoreSilver[3] = {168, 170, 172}, kFinDark[3] = {58, 60, 64}, kCoreBlack[3] = {22, 22, 24};

// Builds one intercooler tile (0 left end, 1 middle, 2 right end). Like a car's: an end tank on each side and the
// core between them. No ports: gas ports snap onto its faces.
inline std::vector<uint8_t> build_intercooler_tile(const std::vector<uint8_t>& stock, int tile, const char* new_name, int style = kIntercoolerStyle) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double h = kCell / 2, y0 = -h, y1 = h + kCell;      // the tile's own cell along x: -h..h; 2 cells tall
    const double td = 0.026, tank_w = 0.028, cd = 0.017;     // tank half depth and width, core half depth
    MeshBuilder b;
    double x0 = tile == 0 ? -h + tank_w : -h, x1 = tile == 2 ? h - tank_w : h;   // the core in this tile
    const bool end = tile == 0 || tile == 2;
    const double tx0 = tile == 0 ? -h : h - tank_w, tx1 = tile == 0 ? -h + tank_w : h;   // the end tank's x span
    auto box = [&](double ax, double ay, double az, double bx, double by, double bz, const uint8_t c[3]) {
        double lo[3] = {ax, ay, az}, hi[3] = {bx, by, bz}; b.box(lo, hi, c);
    };
    // Vertical fins on both faces across [fx0, fx1]: `count` per cell width, `w` wide, standing `out` proud.
    auto fins_up = [&](double fx0, double fx1, double ya, double yb, double depth, int count, double w, double out, const uint8_t c[3]) {
        double pitch = kCell / count;
        for (double x = -h + pitch / 2; x < h; x += pitch) {
            if (x - w / 2 < fx0 - 1e-9 || x + w / 2 > fx1 + 1e-9) continue;
            box(x - w / 2, ya, -depth - out, x + w / 2, yb, -depth, c);
            box(x - w / 2, ya, depth, x + w / 2, yb, depth + out, c);
        }
    };
    if (style <= 0 || style >= kIntercoolerStyleCount) {
        box(x0, y0 + 0.008, -cd, x1, y1 - 0.008, cd, kCoreDark);
        for (int s = -1; s <= 1; s += 2)                     // top and bottom rails
            box(x0, s < 0 ? y0 + 0.002 : y1 - 0.008, -cd - 0.002, x1, s < 0 ? y0 + 0.008 : y1 - 0.002, cd + 0.002, kValveIron);
        for (int k = 0; k < 8; ++k) {                        // the tubes: bars across both faces, 4 per cell of height
            double y = y0 + 0.008 + (k + 0.5) * (y1 - y0 - 0.016) / 8;
            box(x0, y - 0.0025, -cd - 0.0015, x1, y + 0.0025, -cd, kValveMid);
            box(x0, y - 0.0025, cd, x1, y + 0.0025, cd + 0.0015, kValveMid);
        }
        if (end) {                                           // end tank, its caps top and bottom, a weld seam to the core
            box(tx0, y0 + 0.008, -td, tx1, y1 - 0.008, td, kValvePaint0);
            for (int s = -1; s <= 1; s += 2)
                box(tx0 - (tile == 0 ? 0 : 0.002), s < 0 ? y0 : y1 - 0.008, -td - 0.002, tx1 + (tile == 2 ? 0 : 0.002), s < 0 ? y0 + 0.008 : y1, td + 0.002, kValvePaint1);
            double seam = tile == 0 ? tx1 : tx0;
            box(seam - 0.0015, y0 + 0.008, -td - 0.001, seam + 0.0015, y1 - 0.008, td + 0.001, kValveIron);
        }
        return encode_valve_mesh(stock, m, b, new_name);
    }
    const double e = 0.006;                                  // top and bottom rail height
    if (style == 1) {                                        // silver bar-and-plate: light core, dark fin slots, cast tanks
        box(x0, y0 + e, -cd, x1, y1 - e, cd, kCoreSilver);
        fins_up(x0, x1, y0 + e, y1 - e, cd, 10, 0.0026, 0.0008, kFinDark);
        for (int s = -1; s <= 1; s += 2) box(x0, s < 0 ? y0 : y1 - e, -cd - 0.002, x1, s < 0 ? y0 + e : y1, cd + 0.002, kValveAlloy);
        if (end) {
            box(tx0, y0, -td, tx1, y1, td, kValvePaint0);
            for (int k = 0; k < 3; ++k) {                     // cast ribs up the tank's faces
                double x = tx0 + tank_w * (k + 1) / 4;
                box(x - 0.0012, y0 + 0.004, -td - 0.0012, x + 0.0012, y1 - 0.004, -td, kValvePaint0);
                box(x - 0.0012, y0 + 0.004, td, x + 0.0012, y1 - 0.004, td + 0.0012, kValvePaint0);
            }
            for (int s = -1; s <= 1; s += 2) box(tx0, s < 0 ? y0 - 0.002 : y1 - 0.004, -td - 0.002, tx1, s < 0 ? y0 + 0.004 : y1 + 0.002, td + 0.002, kValvePaint1);
        }
    } else if (style == 2) {                                 // black race core: black core, bright fins, polished lips, inlet stubs
        box(x0, y0 + e, -cd, x1, y1 - e, cd, kCoreBlack);
        fins_up(x0, x1, y0 + e, y1 - e, cd, 16, 0.0012, 0.0006, kValveAlloy);
        for (int s = -1; s <= 1; s += 2) box(x0, s < 0 ? y0 : y1 - e, -cd - 0.003, x1, s < 0 ? y0 + e : y1, cd + 0.003, kValveAlloy);
        if (end) {
            box(tx0, y0, -td, tx1, y1, td, kValvePaint0);
            for (int s = -1; s <= 1; s += 2) box(tx0 - 0.001, s < 0 ? y0 - 0.002 : y1 - 0.005, -td - 0.002, tx1 + 0.001, s < 0 ? y0 + 0.005 : y1 + 0.002, td + 0.002, kValvePaint1);
            double c[3] = {(tx0 + tx1) / 2, y0 + kCell * 0.35, 0};   // an inlet stub on the tank's front, in the bottom cell
            b.cylinder(2, c, 0.009, td, td + 0.012, kValveAlloy, 16);
            b.cylinder(2, c, 0.0105, td + 0.008, td + 0.010, kValveIron, 16);   // hose bead
        }
    } else if (style == 3) {                                 // framed tube-and-fin: a bezel round the core, fine light and dark fins
        box(x0, y0 + e, -cd, x1, y1 - e, cd, kValveMid);
        fins_up(x0, x1, y0 + e, y1 - e, cd, 20, 0.0016, 0.0005, kFinDark);
        for (int s = -1; s <= 1; s += 2) box(x0, s < 0 ? y0 : y1 - e * 1.6, -cd - 0.004, x1, s < 0 ? y0 + e * 1.6 : y1, cd + 0.004, kValvePaint1);
        if (end) {
            double inner = tile == 0 ? tx1 : tx0;            // the bezel's side bar along the core's edge
            box(inner - 0.003, y0, -cd - 0.004, inner + 0.003, y1, cd + 0.004, kValvePaint1);
            box(tx0, y0 + 0.003, -td * 0.85, tx1, y1 - 0.003, td * 0.85, kValvePaint0);
            for (int k = 0; k < 2; ++k) {                     // bolts down the bezel, top and bottom cells
                double y = k ? y1 - kCell * 0.25 : y0 + kCell * 0.25, p[3] = {inner, y, cd + 0.004};
                bolt(b, 2, p, 0.0022, kValveAlloy);
            }
        }
    } else {                                                 // round-tank front mount: round end tanks, brackets, silver core
        const double cr = 0.011;                             // a slimmer core, so the round tanks stand proud of it
        box(x0, y0 + e, -cr, x1, y1 - e, cr, kCoreSilver);
        fins_up(x0, x1, y0 + e, y1 - e, cr, 12, 0.0018, 0.0007, kValveIron);
        for (int s = -1; s <= 1; s += 2) box(x0, s < 0 ? y0 : y1 - e, -cr - 0.002, x1, s < 0 ? y0 + e : y1, cr + 0.002, kValveIron);
        if (end) {
            double c[3] = {(tx0 + tx1) / 2, 0, 0}, r = std::min(tank_w / 2, td);
            b.frustum_phase(1, c, r, r, y0 + 0.008, y1 - 0.008, kValvePaint0, 16, 0);       // the round tank, full height
            b.frustum_phase(1, c, r * 0.82, r * 0.6, y1 - 0.008, y1, kValvePaint1, 16, 0);  // domed end caps
            b.frustum_phase(1, c, r * 0.6, r * 0.82, y0, y0 + 0.008, kValvePaint1, 16, 0);
            for (int s = -1; s <= 1; s += 2) {               // mounting brackets out the back, in the end cells
                double y = s < 0 ? y0 + kCell * 0.2 : y1 - kCell * 0.2;
                box(c[0] - 0.004, y - 0.004, -td - 0.006, c[0] + 0.004, y + 0.004, -r * 0.5, kValveIron);
            }
        }
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

}  // namespace turbo
