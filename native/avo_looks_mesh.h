#pragma once
// AnyVehicleOverhaul looks, v2 (Nate, 2026-11-04: "revisit all the textures for each turbo, and the intercoolers and
// stuff ... compare the turbos to real looking race turbos, bovs, the pro chargers"). Every part is generated here
// from scratch (no stock mesh underneath), three looks each, modelled on real parts:
//   Turbos            1 G-series (cast satin cover, stainless hot side)   2 Precision (black cover, cast iron)   3 EFR (gunmetal cover, ceramic hot side)
//   Blow-off valves   1 Tial Q (stepped anodised cap)                     2 HKS SSQV (black body, trumpet)        3 GFB Respons (polished, hex cap)
//   Superchargers     1 ProCharger (polished head, finned gear case)      2 Vortech (satin cast, ribbed)          3 Paxton (polished bowl, round case)
//   Intercoolers      1 bar and plate (black core, cast tanks)            2 tube and fin (silver core, black tanks) 3 fabricated (raw core, welded tanks)
//   Air filters       1 cone (pleated cone, chrome lid)                   2 mesh sock (stack in a sock)           3 round (dome-top drum)
// Every turbo also shows its built-in wastegate: the actuator can and rod (the wastegate part is gone).
// Paint: slot 0 is the main casting (cover, body, tanks, element), slot 1 the accent (actuator can, cap, gear case).
// Axes and cells as the part definitions (turbo_logic.h): a cell is 0.08 m, cell 0 spans -0.04..0.04.
#include "turbo_pd_mesh.h"
#include <cmath>
#include <vector>

namespace avolook {
using pdmesh::Mesh;
using pdmesh::Rgb;
using pdmesh::V3;
using pdmesh::Ring;
using pdmesh::kPi;
using pdmesh::circle;
using pdmesh::rounded;
using pdmesh::loft;

constexpr double kCell = 0.08, kH = 0.04;
constexpr Rgb kP0 = pdmesh::kPaint0, kP1 = pdmesh::kPaint1;
constexpr Rgb kPolish{232, 234, 236}, kSatin{186, 188, 190}, kMachined{212, 214, 216}, kBore{24, 24, 26}, kBlackAnod{36, 36, 40};
constexpr Rgb kStainless{158, 150, 140}, kHeatGold{176, 146, 96}, kHeatBlue{98, 104, 140}, kCastIron{70, 70, 72}, kCeramic{200, 198, 190};
constexpr Rgb kBrass{190, 160, 92}, kRubber{30, 30, 32}, kSteel{150, 152, 156}, kWeld{140, 138, 132};
constexpr double kSmallPipe = 0.019, kLargePipe = 0.046;

// ------------------------------------------------------------------------------------- a tube along any path
// Rings of radius r[i] round the path p[i] (rotation-minimising frames, so it never twists). `flat` squashes the
// rings along direction `along` (the volutes are a little flatter than round, like real castings).
inline void sweep(Mesh& m, const std::vector<V3>& p, const std::vector<double>& r, const std::vector<Rgb>& c, int sides = 24, double flat = 1.0,
                  V3 along = {1, 0, 0}, bool cap0 = true, bool cap1 = true) {
    size_t n = p.size();
    if (n < 2) return;
    std::vector<V3> t(n), nn(n), bb(n);
    for (size_t i = 0; i < n; ++i) t[i] = pdmesh::unit(p[std::min(i + 1, n - 1)] - p[i == 0 ? 0 : i - 1]);
    V3 a = pdmesh::unit(along);
    V3 start = std::abs(pdmesh::dot(a, t[0])) < 0.95 ? a : (std::abs(t[0].y) < 0.9 ? V3{0, 1, 0} : V3{0, 0, 1});
    nn[0] = pdmesh::unit(start - t[0] * pdmesh::dot(start, t[0]));
    for (size_t i = 1; i < n; ++i) nn[i] = pdmesh::unit(nn[i - 1] - t[i] * pdmesh::dot(nn[i - 1], t[i]));
    for (size_t i = 0; i < n; ++i) bb[i] = pdmesh::cross(t[i], nn[i]);
    auto at = [&](size_t i, int k) {
        double ang = 2 * kPi * (k + 0.5) / sides;
        double f = std::abs(pdmesh::dot(nn[i], a)) > 0.5 ? flat : 1.0;   // squash along the flat axis only when nn lies on it
        V3 off = nn[i] * (std::cos(ang) * r[i] * f) + bb[i] * (std::sin(ang) * r[i]);
        return std::pair<V3, V3>{p[i] + off, pdmesh::unit(nn[i] * (std::cos(ang) / std::max(f, 0.2)) + bb[i] * std::sin(ang))};
    };
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < sides; ++k) {
            int q = (k + 1) % sides;
            auto A = at(i, k), B = at(i, q), D = at(i + 1, q), E = at(i + 1, k);
            uint32_t ia = m.vertex(A.first, A.second, c[i]), ib = m.vertex(B.first, B.second, c[i]);
            uint32_t id = m.vertex(D.first, D.second, c[i + 1]), ie = m.vertex(E.first, E.second, c[i + 1]);
            m.tri(ia, ib, id); m.tri(ia, id, ie);
        }
    auto cap = [&](size_t i, double s) {
        std::vector<V3> ring;
        for (int k = 0; k < sides; ++k) ring.push_back(at(i, k).first);
        m.face(ring, t[i] * s, c[i]);
    };
    if (cap0) cap(0, -1);
    if (cap1) cap(n - 1, 1);
}
inline void sweep1(Mesh& m, const std::vector<V3>& p, double r, Rgb c, int sides = 20) {
    sweep(m, p, std::vector<double>(p.size(), r), std::vector<Rgb>(p.size(), c), sides);
}
// A straight round tube on an axis (0 x, 1 y, 2 z) at (u, w) from a0 to a1; `inward` faces it in (a bore).
inline void rod(Mesh& m, int axis, double u, double w, double a0, double a1, double r, Rgb c, int sides = 20) {
    m.prism(axis, circle(u, w, r, sides), std::min(a0, a1), std::max(a0, a1), c);
}
// A dark bore (a disc just proud of a face) so a port reads as a hole.
inline void bore(Mesh& m, int axis, double u, double w, double a, double r, double sign) {
    m.prism(axis, circle(u, w, r, 24), sign > 0 ? a : a - 0.0012, sign > 0 ? a + 0.0012 : a, kBore, true);
}
// Hex bolts in a ring round (u, w) on a face along `axis`.
inline void bolt_ring(Mesh& m, int axis, double u, double w, double a, double sign, double rr, int count, double size = 0.003, Rgb c = kPolish, double phase = 0) {
    for (int k = 0; k < count; ++k) {
        double t = phase + 2 * kPi * (k + 0.5) / count;
        pdmesh::bolt(m, axis, u + rr * std::cos(t), w + rr * std::sin(t), a, sign, size, c);
    }
}

// ------------------------------------------------------------------------------------- turbos
// n 3 (3x3x2, small pipes) or 6 (6x6x2, large pipes). The shaft runs along x: compressor at the front (x cell 0, x-),
// turbine at the back (x cell 1). Ports (turbo_logic.h port()): compressor intake axial on the front face, its outlet
// tangential at the top leaving +z; turbine inlet tangential at the bottom from -z, its outlet axial on the back face.
struct TurboDims {
    double c, pipe, rb, r0, r1, xc, xt, rbt, rt0, rt1, out_x, in_x, in_y, out_y;
};
inline TurboDims turbo_dims(int n) {
    TurboDims d{};
    bool large = n >= 6;
    d.c = (n * kCell) / 2 - kH;                       // face centre (y and z)
    d.pipe = large ? kLargePipe : kSmallPipe;
    d.out_y = large ? (n - 2) * kCell + kH : (n - 1) * kCell;   // compressor outlet centre (y), on the z+ face
    d.out_x = large ? kH : 0.0;
    d.in_y = large ? kH : 0.0;                         // turbine inlet centre (y), on the z- face
    d.in_x = large ? kH : kCell;
    d.r1 = large ? 0.056 : 0.026;                      // compressor scroll: tube radius at its end, and its start
    d.r0 = d.r1 * 0.35;
    d.rb = (d.out_y - d.c) - d.r1;                     // the scroll's inner radius (its end's centre sits on the outlet)
    d.rt1 = large ? 0.05 : 0.024;                      // turbine scroll: big at the inlet, small at the end
    d.rt0 = d.rt1 * 0.35;
    d.rbt = (d.c - d.in_y) - d.rt1;
    d.xc = large ? 0.0 : -0.002;
    d.xt = large ? 0.086 : 0.084;
    return d;
}
// The compressor or turbine scroll: angle phi from a0 to a1 round the shaft at x, radius growing from ra to rz.
inline void scroll(Mesh& m, double x, double c, double rb, double ra, double rz, double a0, double a1, Rgb col, double flat, int steps = 40) {
    std::vector<V3> p; std::vector<double> r; std::vector<Rgb> cs;
    for (int i = 0; i <= steps; ++i) {
        double t = double(i) / steps, phi = a0 + (a1 - a0) * t, rr = ra + (rz - ra) * t, R = rb + rr;
        p.push_back({x, c + R * std::cos(phi), c + R * std::sin(phi)});
        r.push_back(rr); cs.push_back(col);
    }
    sweep(m, p, r, cs, 22, flat, {1, 0, 0});
}
struct TurboLook { Rgb cover_detail, chra, hot, hot_tint, can, rod; bool ported_shroud, heat_tint, recirc_boss, shield; };
inline TurboLook turbo_look(int look) {
    switch (look) {
        case 1: return {kPolish, kBrass, kCastIron, kCastIron, kP1, kSteel, true, false, false, false};          // Precision
        case 2: return {kMachined, kBlackAnod, kCeramic, kCeramic, kP1, kSteel, false, false, true, true};        // EFR
    }
    return {kMachined, kBlackAnod, kStainless, kHeatGold, kP1, kSteel, true, true, false, false};                // G-series
}
inline Mesh turbo(int n, int look) {
    Mesh m;
    TurboDims d = turbo_dims(n);
    TurboLook L = turbo_look(look);
    const double k = n >= 6 ? 2.0 : 1.0;              // detail scale
    const double c = d.c, front = -kH, back = kCell + kH;
    // compressor scroll: ends at the top (phi 0: +y) heading +z, into the outlet
    scroll(m, d.xc, c, d.rb, d.r0, d.r1, -2 * kPi + 0.35, 0.0, kP0, n >= 6 ? 0.62 : 0.85);
    {   // discharge: from the scroll's end straight out the z+ face, onto the outlet port
        double zf = n * kCell - kH;
        std::vector<V3> p = {{d.xc, d.out_y, c}, {d.xc, d.out_y, c + 0.02 * k}, {(d.xc + d.out_x) / 2, d.out_y, (c + zf) / 2}, {d.out_x, d.out_y, zf - 0.012 * k}, {d.out_x, d.out_y, zf - 0.004}};
        sweep(m, p, {d.r1, d.r1 * 0.98, d.pipe * 1.12, d.pipe * 1.08, d.pipe * 1.08}, {kP0, kP0, kP0, kP0, kP0}, 22, n >= 6 ? 0.62 : 0.85, {1, 0, 0});
        rod(m, 2, d.out_x, d.out_y, zf - 0.006 * k, zf, d.pipe * 1.22, L.cover_detail, 24);   // the hose bead
        bore(m, 2, d.out_x, d.out_y, zf, d.pipe * 0.85, 1);
    }
    // the cover: a dish over the scroll, the inlet boss, the machined lip
    double cover_r = d.rb + d.r1 * 0.55, flat = n >= 6 ? 0.62 : 0.85, xa = std::max(front + 0.012 * k, d.xc - d.r1 * flat), xb = d.xc + d.r1 * flat * 0.8;
    loft(m, 0, {{xa, circle(c, c, cover_r * 0.9, 40)}, {xa + 0.004 * k, circle(c, c, cover_r, 40)}, {xb - 0.004 * k, circle(c, c, cover_r, 40)},
                {xb, circle(c, c, cover_r * 0.9, 40)}}, kP0);
    double inlet_r = d.pipe * 1.45;
    loft(m, 0, {{front + 0.002, circle(c, c, inlet_r, 36)}, {front + 0.006 * k, circle(c, c, inlet_r * 1.02, 36)}, {xa + 0.001, circle(c, c, inlet_r * 1.3, 36)}}, kP0);
    rod(m, 0, c, c, front, front + 0.004, inlet_r * 1.03, L.cover_detail, 36);             // machined inlet lip
    bore(m, 0, c, c, front, d.pipe * 1.05, -1);
    if (L.ported_shroud)                                                                    // anti-surge slots round the inducer
        for (int i = 0; i < 10; ++i) {
            double t = 2 * kPi * (i + 0.5) / 10, rr = inlet_r * 0.82;
            m.prism(0, circle(c + rr * std::cos(t), c + rr * std::sin(t), 0.0025 * k, 8), front - 0.0008, front + 0.001, kBore);
        }
    if (L.recirc_boss) {                                                                    // EFR: the cast boss of its recirculation valve
        double bx = d.xc, by = c - cover_r * 0.55, bz = c + cover_r * 0.75;
        rod(m, 2, bx, by, bz - 0.01 * k, bz + 0.012 * k, 0.014 * k, kP0, 24);
        rod(m, 2, bx, by, bz + 0.012 * k, bz + 0.016 * k, 0.012 * k, L.cover_detail, 24);
    }
    for (int i = 0; i < 8; ++i) {                                                           // cover bolts on the back plate ring
        double t = 2 * kPi * (i + 0.5) / 8;
        pdmesh::bolt(m, 0, c + (cover_r * 0.97) * std::cos(t), c + (cover_r * 0.97) * std::sin(t), xb, 1, 0.0028 * k, kPolish);
    }
    // centre section (bearing housing) and its oil feed and drain
    double chra_r = d.rb * 0.5;
    rod(m, 0, c, c, xb, d.xt - d.rt1 * 0.7, chra_r, L.chra, 28);
    rod(m, 0, c, c, (d.xc + d.xt) / 2 - 0.006 * k, (d.xc + d.xt) / 2 + 0.006 * k, chra_r * 1.12, L.chra, 28);
    for (int side = 0; side < 2; ++side) {                                                  // oil in (front), out (back), down to the bottom face
        double ox = side ? kCell : 0.0, oz = (n - 1) / 2 * kCell;
        double ytop = c - chra_r * 0.8, ybot = -kH + 0.002;
        std::vector<V3> p = {{(d.xc + d.xt) / 2 + (side ? 0.01 : -0.01) * k, ytop, c}, {ox, ytop - 0.01 * k, (c + oz) / 2}, {ox, ybot + 0.012 * k, oz}, {ox, ybot, oz}};
        sweep1(m, p, (side ? 0.007 : 0.005) * k, kSteel, 14);
        m.prism(1, pdmesh::rounded(ox - 0.008 * k, oz - 0.008 * k, ox + 0.008 * k, oz + 0.008 * k, 0.002 * k, 2), -kH, -kH + 0.006 * k, kPolish, false);   // fitting
    }
    // turbine scroll: in at the bottom (phi pi: -y) heading +z, shrinking round the shaft
    scroll(m, d.xt, c, d.rbt, d.rt1, d.rt0, kPi, kPi - 2 * kPi + 0.35, L.hot, n >= 6 ? 0.68 : 0.9);
    {   // turbine inlet: from the z- face straight in to the scroll's start, with a 4-bolt flange
        std::vector<V3> p = {{d.in_x, d.in_y, front + 0.004}, {d.in_x, d.in_y, front + 0.014 * k}, {(d.in_x + d.xt) / 2, d.in_y, (front + c) / 2}, {d.xt, d.in_y, c - 0.01 * k}, {d.xt, d.in_y, c}};
        std::vector<Rgb> cs = {L.hot_tint, L.hot_tint, L.hot, L.hot, L.hot};
        sweep(m, p, {d.pipe * 1.1, d.pipe * 1.12, d.rt1 * 0.98, d.rt1, d.rt1}, cs, 22, n >= 6 ? 0.68 : 0.9, {1, 0, 0});
        double f = d.pipe * 1.55;
        m.prism(2, rounded(d.in_x - f, d.in_y - f, d.in_x + f, d.in_y + f, f * 0.35, 4), front, front + 0.006 * k, L.hot_tint, false);
        bolt_ring(m, 2, d.in_x, d.in_y, front + 0.006 * k, 1, f * 1.05, 4, 0.0028 * k, kPolish, kPi / 4 - kPi / 4);
        bore(m, 2, d.in_x, d.in_y, front, d.pipe * 0.9, -1);
    }
    // turbine body, outlet and its V-band
    double tb = d.rbt + d.rt1 * 0.4;
    loft(m, 0, {{d.xt - d.rt1 * 0.6, circle(c, c, tb * 0.85, 36)}, {d.xt - d.rt1 * 0.3, circle(c, c, tb, 36)}, {d.xt + d.rt1 * 0.5, circle(c, c, tb * 0.95, 36)}}, L.hot);
    double out_r = d.pipe * 1.35;
    loft(m, 0, {{d.xt + d.rt1 * 0.45, circle(c, c, tb * 0.7, 32)}, {back - 0.012 * k, circle(c, c, out_r, 32)}, {back - 0.004, circle(c, c, out_r, 32)}}, L.hot);
    rod(m, 0, c, c, back - 0.008 * k, back - 0.001, out_r * 1.12, kSteel, 32);                   // V-band clamp
    pdmesh::bolt(m, 1, back - 0.005 * k, c, c + out_r * 1.12, 1, 0.003 * k, kPolish);
    bore(m, 0, c, c, back, d.pipe * 1.0, 1);
    if (L.heat_tint) {                                                                      // a band of heat colour round the hot side
        rod(m, 0, c, c, d.xt - 0.004 * k, d.xt + 0.004 * k, tb * 1.01, kHeatBlue, 36);
    }
    if (L.shield) {                                                                         // EFR: a ribbed heat shield over the turbine's top
        for (int i = 0; i < 3; ++i) {
            double yy = c + tb * (0.55 + 0.14 * i);
            m.box({d.xt - d.rt1 * 0.5, yy, c - tb * 0.55}, {d.xt + d.rt1 * 0.5, yy + 0.003 * k, c + tb * 0.55}, kSteel);
        }
    }
    {   // the built-in wastegate: an actuator can on a bracket off the cover, its rod back to the turbine's lever
        double cz = n >= 6 ? c - d.rb - d.r1 * 0.2 : c - d.rb - 0.005, cy = c + d.rb * 0.55;
        double cr = 0.016 * k, x0 = front + 0.004, x1 = d.xc + 0.006 * k;
        rod(m, 0, cy, cz, x0, x1, cr, L.can, 28);
        rod(m, 0, cy, cz, x0 - 0.0005, x0 + 0.004 * k, cr * 1.06, L.can, 28);                   // crimp rim
        rod(m, 0, cy, cz, x1, x1 + 0.006 * k, cr * 0.45, kSteel, 16);                           // boss
        pdmesh::bolt(m, 0, cy, cz, x0, -1, 0.004 * k, kPolish);                                   // the boost line nipple
        sweep1(m, {{x1 + 0.006 * k, cy, cz}, {d.xt, cy - 0.002, cz}}, 0.0022 * k, L.rod, 10);    // rod
        sweep1(m, {{d.xt, cy - 0.002, cz}, {d.xt, c - d.rbt * 0.2, cz + 0.006 * k}}, 0.003 * k, L.rod, 10);   // lever
        m.box({x1 - 0.004 * k, cy - cr * 0.3, cz + cr * 0.6}, {x1, cy + cr * 0.3, c - cover_r * 0.7}, kSteel);   // bracket to the cover
    }
    {   // the mounting foot: a cast pad under the compressor housing, bolted down on the bottom face (it mounts there)
        double fz = n >= 6 ? kCell : 0.0, fw = 0.03 * k, fy = -kH;
        m.box({-fw, fy, fz - fw}, {fw, fy + 0.006 * k, fz + fw}, kP0);
        m.box({-0.006 * k, fy + 0.006 * k, fz - fw * 0.8}, {0.006 * k, c - cover_r * 0.75, fz + fw * 0.8}, kP0);
        for (double sx : {-1.0, 1.0}) for (double sz : {-1.0, 1.0}) pdmesh::bolt(m, 1, sx * fw * 0.7, fz + sz * fw * 0.7, fy + 0.006 * k, 1, 0.0028 * k);
    }
    return m;
}

// ------------------------------------------------------------------------------------- blow-off valves
// Inline on the boost pipe (along z through the pipe's centre), venting up out of its top. Small: one cell, small
// pipe at (0, 0). Large: 2x2x2, large pipe at (0.04, 0.04).
inline Mesh blow_off(bool large, int look) {
    Mesh m;
    const double k = large ? 2.3 : 1.0, pc = large ? kH : 0.0, pr = large ? kLargePipe : kSmallPipe;
    const double z0 = -kH, z1 = large ? kCell + kH : kH, zc = (z0 + z1) / 2, top = large ? kCell + kH : kH;
    // the body: a tube along the pipe with hose beads, a tee up to the valve
    rod(m, 2, pc, pc, z0 + 0.002, z1 - 0.002, pr * 1.12, kP0, 28);
    for (double z : {z0 + 0.002, z1 - 0.008 * k}) rod(m, 2, pc, pc, z, z + 0.006 * k, pr * 1.22, kSatin, 28);
    bore(m, 2, pc, pc, z0, pr * 0.85, -1); bore(m, 2, pc, pc, z1, pr * 0.85, 1);
    double vr = (large ? 0.042 : 0.017), ybase = pc + pr * 0.9, ytop = top - 0.002;
    rod(m, 1, pc, zc, pc, ybase + 0.004 * k, vr * 0.9, kP0, 28);                            // the tee's neck
    if (look == 0) {                                                                         // Tial Q: body, flange, stepped anodised cap with grooves
        rod(m, 1, pc, zc, ybase, ybase + (ytop - ybase) * 0.35, vr, kSatin, 32);
        rod(m, 1, pc, zc, ybase + (ytop - ybase) * 0.35, ybase + (ytop - ybase) * 0.42, vr * 1.12, kSatin, 32);
        for (int i = 0; i < 4; ++i) {
            double a = ybase + (ytop - ybase) * (0.42 + 0.13 * i);
            rod(m, 1, pc, zc, a, a + (ytop - ybase) * 0.1, vr * (1.0 - 0.07 * i), kP1, 32);
            rod(m, 1, pc, zc, a + (ytop - ybase) * 0.1, a + (ytop - ybase) * 0.13, vr * (0.93 - 0.07 * i), kBlackAnod, 32);
        }
        bolt_ring(m, 1, pc, zc, ybase + (ytop - ybase) * 0.42, 1, vr * 1.02, 6, 0.0016 * k);
        for (int i = 0; i < 6; ++i) {                                                        // vent slots round the base
            double t = 2 * kPi * (i + 0.5) / 6;
            m.prism(1, circle(pc + vr * 1.0 * std::cos(t), zc + vr * 1.0 * std::sin(t), 0.0022 * k, 8), ybase + 0.004 * k, ybase + 0.012 * k, kBore);
        }
    } else if (look == 1) {                                                                  // HKS SSQV: black body, a flared trumpet up top
        loft(m, 1, {{ybase, circle(pc, zc, vr * 1.05, 32)}, {ybase + (ytop - ybase) * 0.45, circle(pc, zc, vr * 1.1, 32)},
                    {ybase + (ytop - ybase) * 0.55, circle(pc, zc, vr * 0.7, 32)}}, kP0);
        loft(m, 1, {{ybase + (ytop - ybase) * 0.5, circle(pc, zc, vr * 0.38, 32)}, {ybase + (ytop - ybase) * 0.75, circle(pc, zc, vr * 0.45, 32)},
                    {ybase + (ytop - ybase) * 0.9, circle(pc, zc, vr * 0.7, 32)}, {ytop, circle(pc, zc, vr * 1.05, 32)}}, kPolish, true, false);
        bore(m, 1, pc, zc, ytop, vr * 0.75, 1);
        rod(m, 1, pc, zc, ybase + (ytop - ybase) * 0.2, ybase + (ytop - ybase) * 0.26, vr * 1.14, kP1, 32);   // coloured band
        bolt_ring(m, 1, pc, zc, ybase + (ytop - ybase) * 0.45, 1, vr * 0.95, 4, 0.0016 * k);
    } else {                                                                                 // GFB Respons: polished body, hex cap, vent ring
        rod(m, 1, pc, zc, ybase, ybase + (ytop - ybase) * 0.5, vr * 0.95, kPolish, 32);
        for (int i = 0; i < 8; ++i) {
            double t = 2 * kPi * (i + 0.5) / 8;
            m.box({pc + vr * 0.95 * std::cos(t) - 0.0015 * k, ybase + 0.004 * k, zc + vr * 0.95 * std::sin(t) - 0.0015 * k},
                  {pc + vr * 0.95 * std::cos(t) + 0.0015 * k, ybase + (ytop - ybase) * 0.4, zc + vr * 0.95 * std::sin(t) + 0.0015 * k}, kBore);
        }
        Ring hex;
        for (int i = 0; i < 6; ++i) { double t = kPi / 6 + kPi / 3 * i; hex.push_back({pc + vr * 1.08 * std::cos(t), zc + vr * 1.08 * std::sin(t)}); }
        m.prism(1, hex, ybase + (ytop - ybase) * 0.5, ybase + (ytop - ybase) * 0.88, kP1, false);
        rod(m, 1, pc, zc, ybase + (ytop - ybase) * 0.88, ytop, vr * 0.7, kPolish, 32);
        pdmesh::bolt(m, 1, pc, zc, ytop, 1, 0.003 * k, kSteel);
    }
    return m;
}

// ------------------------------------------------------------------------------------- centrifugal superchargers
// Small 2x2x2, large 4x4x4. Impeller axis along z, centred on the face: intake (large pipe) on the front (z-), the
// scroll in the front cells, its outlet tangential out the top at x + one cell (small: small pipe at x 0.08; large:
// large pipe at x 0.2, z 0.04), the gear case behind, down to the pulley shaft at the back bottom corner (x 0, y 0).
inline Mesh supercharger(bool large, int look) {
    Mesh m;
    const int n = large ? 4 : 2;
    const double c = n * kCell / 2 - kH, k = large ? 2.0 : 1.0, back = n * kCell - kH, top = n * kCell - kH;
    const double out_x = large ? 2.5 * kCell : kCell, out_z = large ? kH : 0.0, pipe = large ? kLargePipe : kSmallPipe;
    const double R = out_x - c, r1 = large ? 0.05 : 0.021, rb = R - r1, zs = large ? kH : 0.0;
    // scroll round z, ending at +x (phi 0) heading +y into the outlet
    {
        std::vector<V3> p; std::vector<double> r; std::vector<Rgb> cs;
        for (int i = 0; i <= 40; ++i) {
            double t = i / 40.0, phi = -2 * kPi + 0.35 + (2 * kPi - 0.35) * t, rr = r1 * (0.35 + 0.65 * t), RR = rb + rr;
            p.push_back({c + RR * std::cos(phi), c + RR * std::sin(phi), zs});
            r.push_back(rr); cs.push_back(kP0);
        }
        sweep(m, p, r, cs, 22, 0.8, {0, 0, 1});
        std::vector<V3> d = {{out_x, c, zs}, {out_x, c + 0.02 * k, zs}, {out_x, (c + top) / 2, (zs + out_z) / 2}, {out_x, top - 0.01 * k, out_z}, {out_x, top - 0.003, out_z}};
        sweep(m, d, {r1, r1, pipe * 1.1, pipe * 1.08, pipe * 1.08}, {kP0, kP0, kP0, kP0, kP0}, 22, 0.8, {0, 0, 1});
        rod(m, 1, out_x, out_z, top - 0.006 * k, top, pipe * 1.2, kPolish, 24);
        bore(m, 1, out_x, out_z, top, pipe * 0.85, 1);
    }
    double cover = rb + r1 * 0.6;
    if (look == 0) {   // ProCharger: polished head, black finned gear case
        loft(m, 2, {{-kH + 0.004, circle(c, c, kLargePipe * 1.15, 40)}, {zs - r1 * 0.7, circle(c, c, cover * 0.95, 40)}, {zs + r1 * 0.6, circle(c, c, cover, 40)},
                    {zs + r1 * 0.9, circle(c, c, cover * 0.92, 40)}}, kP0);
    } else if (look == 1) {   // Vortech: satin casting, ribs over the cover
        loft(m, 2, {{-kH + 0.004, circle(c, c, kLargePipe * 1.12, 40)}, {zs - r1 * 0.8, circle(c, c, cover, 40)}, {zs + r1 * 0.8, circle(c, c, cover, 40)}}, kP0);
        for (int i = 0; i < 6; ++i) {
            double t = 2 * kPi * (i + 0.5) / 6;
            sweep1(m, {{c + kLargePipe * 1.1 * std::cos(t), c + kLargePipe * 1.1 * std::sin(t), -kH + 0.006}, {c + cover * 0.95 * std::cos(t), c + cover * 0.95 * std::sin(t), zs - r1 * 0.75}}, 0.003 * k, kP0, 8);
        }
    } else {   // Paxton: deep polished bowl
        loft(m, 2, {{-kH + 0.004, circle(c, c, kLargePipe * 1.2, 40)}, {-kH + 0.016 * k, circle(c, c, cover * 0.75, 40)}, {zs, circle(c, c, cover * 1.02, 40)},
                    {zs + r1 * 0.9, circle(c, c, cover * 0.95, 40)}}, kP0);
    }
    rod(m, 2, c, c, -kH, -kH + 0.005, kLargePipe * (look == 2 ? 1.24 : 1.18), kPolish, 40);       // machined intake lip
    bore(m, 2, c, c, -kH, kLargePipe * 0.98, -1);
    // gear case: an oval round the impeller shaft and the pulley shaft (0, 0), from behind the scroll to the back face
    double g0 = zs + r1 * 0.9, g1 = back - 0.006 * k;
    // the hull of a big circle round the impeller shaft and a small one round the pulley shaft (support function)
    auto oval = [&](double grow) {
        Ring o;
        const double rbig = (rb + r1 * 1.6) + grow, rsmall = 0.03 * k + grow;
        for (int i = 0; i < 48; ++i) {
            double t = 2 * kPi * (i + 0.5) / 48, ux = std::cos(t), uy = std::sin(t);
            double a1 = c * ux + c * uy + rbig, a2 = rsmall;   // support of each circle along u
            o.push_back(a1 >= a2 ? std::pair<double, double>{c + rbig * ux, c + rbig * uy} : std::pair<double, double>{rsmall * ux, rsmall * uy});
        }
        return o;
    };
    Rgb gc = look == 1 ? kP0 : kP1;
    loft(m, 2, {{g0, oval(-0.004 * k)}, {g0 + 0.006 * k, oval(0)}, {g1, oval(0)}, {g1 + 0.004, oval(-0.003 * k)}}, gc);
    if (look == 0)                                                                           // cooling fins across the case
        for (double z = g0 + 0.014 * k; z < g1 - 0.006 * k; z += 0.009 * k) loft(m, 2, {{z, oval(0.003 * k)}, {z + 0.003 * k, oval(0.003 * k)}}, gc);
    if (look == 1)
        for (double z : {g0 + 0.01 * k, (g0 + g1) / 2, g1 - 0.01 * k}) loft(m, 2, {{z, oval(0.0025 * k)}, {z + 0.004 * k, oval(0.0025 * k)}}, kSatin);
    if (look == 2) loft(m, 2, {{(g0 + g1) / 2, oval(0.002 * k)}, {(g0 + g1) / 2 + 0.004 * k, oval(0.002 * k)}}, kPolish);
    // bolts round the case's back and the input shaft boss
    for (int i = 0; i < 10; ++i) {
        Ring o = oval(-0.006 * k);
        auto& q = o[size_t(i * (o.size() / 10))];
        pdmesh::bolt(m, 2, q.first, q.second, g1 + 0.004, 1, 0.0025 * k);
    }
    rod(m, 2, 0.0, 0.0, back - 0.008, back, 0.012 * k, kSteel, 24);
    rod(m, 2, 0.0, 0.0, back - 0.002, back + 0.0005, 0.005 * k, kPolish, 12);
    // feet on the bottom (it bolts down there)
    m.box({c - cover * 0.35, -kH, g0}, {c + cover * 0.35, -kH + 0.005 * k, g1}, gc);
    m.box({c - 0.005 * k, -kH + 0.005 * k, g0 + 0.004}, {c + 0.005 * k, c - cover * 0.8, g1 - 0.004}, gc);
    return m;
}

// ------------------------------------------------------------------------------------- intercooler tiles
// One cell thick (z -0.04..0.04), tiled like the stock radiator (turbo_upkeep_mesh.h build_intercooler_tile): tx 0 the
// left end (cells -1 and 0), 1 the middle, 2 the right end; ty 0 bottom, 1 middle, 2 top.
inline Mesh intercooler_tile(int tx, int ty, int look) {
    Mesh m;
    const double h = kH;
    const bool end = tx == 0 || tx == 2, bottom = ty == 0, topt = ty == 2;
    const double xa = tx == 0 ? -kCell - h : -h, tank_w = look == 1 ? 0.03 : 0.032;
    const double x0 = tx == 0 ? xa + tank_w : xa, x1 = tx == 2 ? h - tank_w : h;
    const double e = 0.006, y0 = bottom ? -h + e : -h, y1 = topt ? h - e : h;
    const double cd = look == 2 ? 0.026 : 0.024;                       // core half depth
    Rgb core = look == 0 ? Rgb{24, 24, 26} : look == 1 ? Rgb{176, 178, 180} : Rgb{150, 152, 154};
    Rgb fin = look == 0 ? Rgb{120, 122, 126} : look == 1 ? Rgb{120, 122, 124} : Rgb{196, 198, 200};
    m.box({x0, y0, -cd}, {x1, y1, cd}, core);
    if (look == 0) {                                                     // bar and plate: bars across, fins between them
        for (double y = -h + 0.004; y < h; y += 0.0067) {
            if (y < y0 || y + 0.0016 > y1) continue;
            m.box({x0, y, -cd - 0.0007}, {x1, y + 0.0016, cd + 0.0007}, Rgb{70, 70, 74});
        }
    } else if (look == 1) {                                              // tube and fin: tubes across, louvred fins
        for (double y = -h + 0.005; y < h; y += 0.008) {
            if (y < y0 || y + 0.0022 > y1) continue;
            m.box({x0, y, -cd - 0.0009}, {x1, y + 0.0022, cd + 0.0009}, fin);
        }
    } else {                                                             // fabricated race core: fine vertical fins, bright
        for (double x = xa + 0.002; x < h; x += 0.0045) {
            if (x < x0 || x + 0.001 > x1) continue;
            m.box({x, y0, -cd - 0.0006}, {x + 0.001, y1, cd + 0.0006}, fin);
        }
    }
    if (bottom) m.box({x0, -h, -cd - 0.003}, {x1, -h + e, cd + 0.003}, look == 2 ? kWeld : kSatin);
    if (topt) m.box({x0, h - e, -cd - 0.003}, {x1, h, cd + 0.003}, look == 2 ? kWeld : kSatin);
    if (!end) return m;
    const double t0 = tx == 0 ? xa : h - tank_w, t1 = tx == 0 ? xa + tank_w : h, tc = (t0 + t1) / 2, td = 0.036;
    if (look == 0) {                                                     // cast end tanks: rounded, ribbed
        double ya = bottom ? -h + 0.01 : -h, yb = topt ? h - 0.01 : h;
        m.prism(1, rounded(t0, -td, t1, td, 0.012, 4), ya, yb, kP0);
        if (bottom) loft(m, 1, {{-h, pdmesh::scaled(rounded(t0, -td, t1, td, 0.012, 4), tc, 0, 0.8)}, {ya, rounded(t0, -td, t1, td, 0.012, 4)}}, kP0);
        if (topt) loft(m, 1, {{yb, rounded(t0, -td, t1, td, 0.012, 4)}, {h, pdmesh::scaled(rounded(t0, -td, t1, td, 0.012, 4), tc, 0, 0.8)}}, kP0);
        for (double y = -h + 0.012; y < h - 0.01; y += 0.02) m.box({tc - tank_w * 0.42, y, td - 0.001}, {tc + tank_w * 0.42, y + 0.003, td + 0.0018}, kP0);
    } else if (look == 1) {                                              // black end tanks with welded caps
        m.box({t0, -h, -td}, {t1, h, td}, kP0);
        if (bottom) m.box({t0 - 0.001, -h, -td - 0.001}, {t1 + 0.001, -h + 0.005, td + 0.001}, kP1);
        if (topt) m.box({t0 - 0.001, h - 0.005, -td - 0.001}, {t1 + 0.001, h, td + 0.001}, kP1);
    } else {                                                             // fabricated: boxy tank with weld beads and a bracket
        m.box({t0, -h, -td}, {t1, h, td}, kP0);
        for (double s : {-1.0, 1.0}) m.box({t0, -h, s * td - 0.0012}, {t1, h, s * td + 0.0012}, kWeld);
        if (bottom || topt) {
            double yb = bottom ? -h + 0.016 : h - 0.016;
            m.box({tc - 0.006, yb - 0.004, -td - 0.008}, {tc + 0.006, yb + 0.004, -td}, kSteel);
        }
    }
    if (bottom) {                                                        // the hose neck on each tank's front, in the bottom cell
        rod(m, 2, tc, -h + kCell * 0.45, td, h - 0.0005, 0.011, kSatin, 20);
        rod(m, 2, tc, -h + kCell * 0.45, h - 0.006, h - 0.003, 0.0128, kSatin, 20);
    }
    return m;
}

// ------------------------------------------------------------------------------------- air filters
// Small 3x3x3, large 4x4x4, the neck on the bottom face's middle cell (small at (0.08, 0.08), large at (0.12, 0.12)).
inline Mesh air_filter(bool large, int look) {
    Mesh m;
    const int n = large ? 4 : 3;
    const double c = n * kCell / 2 - kH, pr = large ? kLargePipe : kSmallPipe, bot = -kH, top = n * kCell - kH, ext = n * kCell / 2;
    // the neck and its clamp
    rod(m, 1, c, c, bot, bot + 0.004, pr * 1.3, kRubber, 28);
    rod(m, 1, c, c, bot + 0.004, bot + 0.03 * (large ? 1.6 : 1.0), pr * 1.12, kRubber, 28);
    rod(m, 1, c, c, bot + 0.012, bot + 0.02, pr * 1.2, kSteel, 28);
    bore(m, 1, c, c, bot, pr * 0.9, -1);
    double y0 = bot + 0.03 * (large ? 1.6 : 1.0);
    if (look == 0) {                                                     // cone: pleated, tapering up to a chrome lid
        double rb = ext * 0.82, rt = ext * 0.55, ya = y0 + 0.006, yb = top - 0.02;
        m.prism(1, circle(c, c, rb * 1.02, 40), y0, ya, kRubber);
        int pleats = large ? 40 : 32;
        Ring lo, hi;
        for (int i = 0; i < pleats * 2; ++i) {
            double t = kPi * i / pleats, rr = i % 2 ? 0.92 : 1.0;
            lo.push_back({c + rb * rr * std::cos(t), c + rb * rr * std::sin(t)});
            hi.push_back({c + rt * rr * std::cos(t), c + rt * rr * std::sin(t)});
        }
        loft(m, 1, {{ya, lo}, {yb, hi}}, kP0, false, false);
        loft(m, 1, {{yb, circle(c, c, rt * 1.03, 40)}, {top - 0.006, circle(c, c, rt * 0.98, 40)}, {top - 0.002, circle(c, c, rt * 0.9, 40)}}, kP1);
        rod(m, 1, c, c, top - 0.003, top, rt * 0.45, kPolish, 32);
    } else if (look == 1) {                                              // mesh sock over a polished stack
        double rs = ext * 0.86;
        loft(m, 1, {{y0, circle(c, c, pr * 1.1, 36)}, {y0 + (top - y0) * 0.25, circle(c, c, pr * 1.3, 36)}, {y0 + (top - y0) * 0.45, circle(c, c, rs * 0.85, 36)}}, kPolish);
        m.prism(1, circle(c, c, rs, 40), y0 + (top - y0) * 0.1, top - 0.006, kP0);
        for (int i = 0; i < 24; ++i) {                                   // the sock's weave: dark vertical strands
            double t = 2 * kPi * (i + 0.5) / 24;
            m.box({c + rs * std::cos(t) - 0.0012, y0 + (top - y0) * 0.12, c + rs * std::sin(t) - 0.0012},
                  {c + rs * std::cos(t) + 0.0012, top - 0.008, c + rs * std::sin(t) + 0.0012}, Rgb{20, 20, 22});
        }
        loft(m, 1, {{top - 0.008, circle(c, c, rs * 1.02, 40)}, {top - 0.002, circle(c, c, rs * 0.95, 40)}, {top, circle(c, c, rs * 0.4, 40)}}, kP1);
        rod(m, 1, c, c, y0 + (top - y0) * 0.08, y0 + (top - y0) * 0.13, rs * 1.03, kP1, 40);   // band
    } else {                                                             // round drum: flat base, pleated drum, domed lid, wing nut
        double rd = ext * 0.9, ya = y0 + 0.004, yb = top - (large ? 0.06 : 0.045);
        m.prism(1, circle(c, c, rd * 1.04, 48), y0, ya, kP1);
        int pleats = large ? 48 : 40;
        Ring ring;
        for (int i = 0; i < pleats * 2; ++i) { double t = kPi * i / pleats, rr = i % 2 ? 0.95 : 1.0; ring.push_back({c + rd * rr * std::cos(t), c + rd * rr * std::sin(t)}); }
        loft(m, 1, {{ya, ring}, {yb, ring}}, kP0, false, false);
        loft(m, 1, {{yb, circle(c, c, rd * 1.05, 48)}, {yb + 0.008, circle(c, c, rd * 1.04, 48)}, {top - 0.012, circle(c, c, rd * 0.7, 48)}, {top - 0.006, circle(c, c, rd * 0.3, 48)}}, kP1);
        m.prism(1, circle(c, c, 0.008 * (large ? 1.6 : 1.0), 12), top - 0.008, top - 0.002, kPolish);
        m.box({c - 0.016 * (large ? 1.6 : 1.0), top - 0.005, c - 0.003}, {c + 0.016 * (large ? 1.6 : 1.0), top, c + 0.003}, kPolish);   // wing nut
    }
    return m;
}

}  // namespace avolook
