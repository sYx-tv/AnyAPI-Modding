#pragma once
// Centrifugal supercharger meshes, in five looks (kSuperchargerStyle picks the one the game uses). A head unit: the
// compressor's snail-shell volute on the front, its large-pipe intake centred on the front face, the discharge rising
// tangentially to the top face, and the step-up gear case behind it with the input shaft (the torque spot a stock
// Pulley Wheel bolts onto) on the back face.
//   small: 2 x 2 x 2 cells; large intake on the front face, small outlet on top (cell x 1, z 0), torque spot on the
//          back face (cell 0, 0).
//   large: the small scaled up twice: 4 x 4 x 4 cells; large intake (cells 1-2), large outlet on top (cells x 2-3,
//          z 0-1), torque spot on the back face (cell 0, 0).
#include "turbo_valve_mesh.h"
#include <algorithm>
#include <array>

namespace turbo {

struct ScFrame {                          // maps the small part's coordinates onto this size (k = 1 or 2)
    double k = 1, corner = -kCell / 2;
    double p(double v) const { return corner + k * (v - corner); }
    double l(double v) const { return k * v; }
};

// A triangle wound so its front faces along n (the builder stores counter-clockwise-from-outside triangles).
inline void tri_facing(MeshBuilder& b, uint32_t a, uint32_t c, uint32_t d, const double n[3]) {
    const auto& A = b.verts[a].pos; const auto& C = b.verts[c].pos; const auto& D = b.verts[d].pos;
    double u[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]}, v[3] = {D[0] - A[0], D[1] - A[1], D[2] - A[2]};
    double x[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    if (x[0] * n[0] + x[1] * n[1] + x[2] * n[2] >= 0) b.tri(a, c, d); else b.tri(a, d, c);
}

// A tube swept along a path: ring i is centred at c[i] in the plane of the unit vectors e1[i], e2[i], radius r[i].
inline void sweep(MeshBuilder& b, const std::vector<std::array<double, 3>>& c, const std::vector<std::array<double, 3>>& e1,
                  const std::vector<std::array<double, 3>>& e2, const std::vector<double>& r, int seg, const uint8_t col[3], bool caps = true) {
    size_t n = c.size();
    std::vector<uint32_t> first(n);
    for (size_t i = 0; i < n; ++i) {
        first[i] = uint32_t(b.verts.size());
        for (int j = 0; j <= seg; ++j) {
            double a = 2 * MeshBuilder::kPi * j / seg, cs = std::cos(a), sn = std::sin(a), nn[3];
            for (int q = 0; q < 3; ++q) nn[q] = cs * e1[i][q] + sn * e2[i][q];
            b.vertex(c[i][0] + r[i] * nn[0], c[i][1] + r[i] * nn[1], c[i][2] + r[i] * nn[2], nn[0], nn[1], nn[2], col);
        }
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int j = 0; j < seg; ++j) {
            uint32_t a = first[i] + j, a1 = a + 1, d = first[i + 1] + j, d1 = d + 1;
            double nn[3]; for (int q = 0; q < 3; ++q) nn[q] = b.verts[a].normal[q] + b.verts[d1].normal[q];
            tri_facing(b, a, d, d1, nn); tri_facing(b, a, d1, a1, nn);
        }
    if (!caps) return;
    for (int end = 0; end < 2; ++end) {                    // flat caps on both ends
        size_t i = end ? n - 1 : 0, o = end ? n - 2 : 1;
        double t[3]; for (int q = 0; q < 3; ++q) t[q] = c[i][q] - c[o][q];
        double l = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]); for (double& q : t) q /= l;
        uint32_t mid = b.vertex(c[i][0], c[i][1], c[i][2], t[0], t[1], t[2], col), ring = uint32_t(b.verts.size());
        for (int j = 0; j <= seg; ++j) { auto p = b.verts[first[i] + j].pos; b.vertex(p[0], p[1], p[2], t[0], t[1], t[2], col); }
        for (int j = 0; j < seg; ++j) tri_facing(b, mid, ring + j, ring + j + 1, t);
    }
}

// A prism along z from z0 to z1 over a convex polygon in x, y (any order; its convex hull is used).
inline void prism_z(MeshBuilder& b, std::vector<std::array<double, 2>> pts, double z0, double z1, const uint8_t col[3]) {
    std::sort(pts.begin(), pts.end());
    std::vector<std::array<double, 2>> h;
    auto cross = [](const std::array<double, 2>& o, const std::array<double, 2>& a, const std::array<double, 2>& c) {
        return (a[0] - o[0]) * (c[1] - o[1]) - (a[1] - o[1]) * (c[0] - o[0]);
    };
    for (int pass = 0; pass < 2; ++pass) {
        size_t base = h.size();
        for (size_t i = 0; i < pts.size(); ++i) {
            const auto& p = pass ? pts[pts.size() - 1 - i] : pts[i];
            while (h.size() >= base + 2 && cross(h[h.size() - 2], h.back(), p) <= 0) h.pop_back();
            h.push_back(p);
        }
        h.pop_back();
    }
    size_t m = h.size();
    double cx = 0, cy = 0; for (auto& p : h) { cx += p[0]; cy += p[1]; } cx /= m; cy /= m;
    for (size_t i = 0; i < m; ++i) {                         // sides
        auto& p = h[i]; auto& q = h[(i + 1) % m];
        double nx = q[1] - p[1], ny = -(q[0] - p[0]), l = std::hypot(nx, ny); nx /= l; ny /= l;
        if (nx * (p[0] - cx) + ny * (p[1] - cy) < 0) { nx = -nx; ny = -ny; }
        double n[3] = {nx, ny, 0};
        uint32_t a = b.vertex(p[0], p[1], z0, nx, ny, 0, col), c = b.vertex(q[0], q[1], z0, nx, ny, 0, col);
        uint32_t d = b.vertex(q[0], q[1], z1, nx, ny, 0, col), e = b.vertex(p[0], p[1], z1, nx, ny, 0, col);
        tri_facing(b, a, c, d, n); tri_facing(b, a, d, e, n);
    }
    for (int s = 0; s < 2; ++s) {                            // caps
        double z = s ? z1 : z0, n[3] = {0, 0, s ? 1.0 : -1.0};
        uint32_t mid = b.vertex(cx, cy, z, 0, 0, n[2], col), f = uint32_t(b.verts.size());
        for (auto& p : h) b.vertex(p[0], p[1], z, 0, 0, n[2], col);
        for (size_t i = 0; i < m; ++i) tri_facing(b, mid, f + uint32_t(i), f + uint32_t((i + 1) % m), n);
    }
}
inline void circle_pts(std::vector<std::array<double, 2>>& out, double x, double y, double r, int seg = 32) {
    for (int i = 0; i < seg; ++i) { double a = 2 * MeshBuilder::kPi * i / seg; out.push_back({x + r * std::cos(a), y + r * std::sin(a)}); }
}
// A box turned `angle` about z, centred at (x, y), half sizes hx, hy, spanning z0..z1.
inline void box_turned(MeshBuilder& b, double x, double y, double hx, double hy, double angle, double z0, double z1, const uint8_t col[3]) {
    double c = std::cos(angle), s = std::sin(angle);
    std::vector<std::array<double, 2>> p;
    for (int i = 0; i < 4; ++i) { double u = (i == 0 || i == 3) ? -hx : hx, v = i < 2 ? -hy : hy; p.push_back({x + u * c - v * s, y + u * s + v * c}); }
    prism_z(b, p, z0, z1, col);
}

// An open tube along z: outer wall r_out, bore r_in (its wall faces inwards), a flat ring on the z0 end.
inline void open_tube_z(MeshBuilder& b, double cx, double cy, double r_out, double r_in, double z0, double z1, const uint8_t col[3], const uint8_t bore[3], int seg) {
    for (int wall = 0; wall < 2; ++wall) {
        double r = wall ? r_in : r_out, sgn = wall ? -1 : 1;
        uint32_t f = uint32_t(b.verts.size());
        for (int j = 0; j <= seg; ++j) {
            double a = 2 * MeshBuilder::kPi * j / seg, cs = std::cos(a), sn = std::sin(a);
            b.vertex(cx + r * cs, cy + r * sn, z0, sgn * cs, sgn * sn, 0, wall ? bore : col);
            b.vertex(cx + r * cs, cy + r * sn, z1, sgn * cs, sgn * sn, 0, wall ? bore : col);
        }
        for (int j = 0; j < seg; ++j) {
            uint32_t a = f + 2 * j; double n[3] = {b.verts[a].normal[0] + b.verts[a + 2].normal[0], b.verts[a].normal[1] + b.verts[a + 2].normal[1], 0};
            tri_facing(b, a, a + 2, a + 3, n); tri_facing(b, a, a + 3, a + 1, n);
        }
    }
    uint32_t f = uint32_t(b.verts.size());                   // the end ring, facing -z
    for (int j = 0; j <= seg; ++j) {
        double a = 2 * MeshBuilder::kPi * j / seg;
        b.vertex(cx + r_in * std::cos(a), cy + r_in * std::sin(a), z0, 0, 0, -1, col);
        b.vertex(cx + r_out * std::cos(a), cy + r_out * std::sin(a), z0, 0, 0, -1, col);
    }
    double n[3] = {0, 0, -1};
    for (int j = 0; j < seg; ++j) { uint32_t a = f + 2 * j; tri_facing(b, a, a + 1, a + 3, n); tri_facing(b, a, a + 3, a + 2, n); }
}

// A thin shell along z from radius r0 at z0 to r1 at z1, drawn on both sides (outside col, inside bore).
inline void shell_z(MeshBuilder& b, double cx, double cy, double r0, double r1, double z0, double z1, const uint8_t col[3], const uint8_t bore[3], int seg) {
    double slope = (r0 - r1) / (z1 - z0), l = std::sqrt(1 + slope * slope);
    for (int side = 0; side < 2; ++side) {
        double sg = side ? -1 : 1;
        uint32_t f = uint32_t(b.verts.size());
        for (int j = 0; j <= seg; ++j) {
            double a = 2 * MeshBuilder::kPi * j / seg, cs = std::cos(a), sn = std::sin(a);
            double nx = sg * cs / l, ny = sg * sn / l, nz = sg * slope / l;
            b.vertex(cx + r0 * cs, cy + r0 * sn, z0, nx, ny, nz, side ? bore : col);
            b.vertex(cx + r1 * cs, cy + r1 * sn, z1, nx, ny, nz, side ? bore : col);
        }
        for (int j = 0; j < seg; ++j) {
            uint32_t a = f + 2 * j; double n[3] = {b.verts[a].normal[0] + b.verts[a + 2].normal[0], b.verts[a].normal[1] + b.verts[a + 2].normal[1], b.verts[a].normal[2]};
            tri_facing(b, a, a + 2, a + 3, n); tri_facing(b, a, a + 3, a + 1, n);
        }
    }
}

constexpr int kSuperchargerStyleCount = 5;
constexpr int kSuperchargerStyle = 0;    // placeholder until Nate picks a look (1-5 on the looks page = styles 0-4)
inline const char* supercharger_style_name(int s) {
    static const char* names[] = {"Polished head unit", "Ribbed satin cast", "Black billet", "Retro finned drum", "Velocity stack"};
    return names[std::clamp(s, 0, kSuperchargerStyleCount - 1)];
}

// Builds the small (large = false) or large supercharger in one of the looks. Paint: the compressor housing takes
// the first colour, the gear case the second.
inline std::vector<uint8_t> build_supercharger_mesh(const std::vector<uint8_t>& stock, bool large, const char* new_name, int style = 0) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    MeshBuilder b;
    const ScFrame f{large ? 2.0 : 1.0};
    const double h = kCell / 2, pi = MeshBuilder::kPi;
    const int seg = 32, steps = 64;
    // the compressor, in the small part's coordinates: axis along z at the face centre, volute plane z = 0
    const double cx = f.p(h), cy = f.p(h), zc = f.p(0);
    const double rho0 = f.l(0.031), rho1 = f.l(0.040), t0 = f.l(0.019), t1 = f.l(0.031);
    const double top = f.p(kCell + h), front = f.p(-h), back = f.p(kCell + h);
    const uint8_t* housing = kValvePaint0;
    const uint8_t* gears = kValvePaint1;
    static const uint8_t kAccent[3] = {196, 38, 34}, kChrome[3] = {226, 228, 230}, kBlade[3] = {120, 122, 126}, kBore[3] = {60, 62, 66};
    auto rho_at = [&](double s) { return rho0 + (rho1 - rho0) * s; };
    auto t_at = [&](double s) { return t0 + (t1 - t0) * s; };
    // a path following the scroll at (radial offset dr, axial offset dz), each a fraction of the tube's radius
    auto scroll_path = [&](double dr, double dz, double radius, const uint8_t* col) {
        std::vector<std::array<double, 3>> c, e1, e2; std::vector<double> r;
        for (int i = 0; i <= steps; ++i) {
            double s = 0.02 + 0.98 * double(i) / steps, a = 2 * pi * s, rr = rho_at(s) + dr * t_at(s);
            c.push_back({cx + rr * std::cos(a), cy + rr * std::sin(a), zc + dz * t_at(s)});
            e1.push_back({std::cos(a), std::sin(a), 0}); e2.push_back({0, 0, 1}); r.push_back(radius);
        }
        sweep(b, c, e1, e2, r, 10, col);
    };

    // the volute: a tube spiralling once round the axis, growing towards the discharge
    {
        std::vector<std::array<double, 3>> c, e1, e2; std::vector<double> r;
        for (int i = 0; i <= steps; ++i) {
            double s = double(i) / steps, a = 2 * pi * s;
            c.push_back({cx + rho_at(s) * std::cos(a), cy + rho_at(s) * std::sin(a), zc});
            e1.push_back({std::cos(a), std::sin(a), 0}); e2.push_back({0, 0, 1}); r.push_back(t_at(s));
        }
        sweep(b, c, e1, e2, r, seg, housing);
    }
    if (style == 1) { scroll_path(0.97, 0, f.l(0.0030), housing); scroll_path(0.70, 0.70, f.l(0.0022), housing); }   // cast ribs
    if (style == 2) { scroll_path(0.62, -0.78, f.l(0.0016), kChrome); scroll_path(0.62, 0.78, f.l(0.0016), kChrome); }   // machined edges
    // the shroud the volute wraps round, and the back plate joining it to the gear case
    { double c[3] = {cx, cy, 0}; b.cylinder(2, c, rho0 + f.l(0.002), zc - t0 * 0.9, zc + t0, housing, seg); }
    // the discharge: straight up from the volute's end to the top face, a flange where the pipe joins
    {
        double x = cx + rho1, c[3] = {x, 0, zc}, rf = large ? 0.046 + 0.008 : 0.019 + 0.008;
        double ft = f.l(0.008), fr = std::max(rf, t1 + f.l(0.003));
        b.cylinder(1, c, t1, cy, top - ft, housing, seg);
        const uint8_t* fc = style == 2 ? kChrome : style == 1 ? housing : style == 3 ? kChrome : kValveIron;
        b.cylinder(1, c, fr, top - ft, top - f.l(0.001), fc, seg);
        b.disc(1, c, (large ? 0.046 : 0.019), top - f.l(0.001) + 0.0003, true, kValveHole, seg);
        for (int k = 0; k < 4; ++k) {
            double a = pi / 4 + k * pi / 2, rb = fr - f.l(0.0035);
            double p[3] = {x + rb * std::cos(a), top - f.l(0.001), zc + rb * std::sin(a)};
            bolt(b, 1, p, f.l(0.0016), style == 0 || style == 4 ? kChrome : kValveAlloy);
        }
    }
    // the intake: an open collar round the large pipe (a flared trumpet on the velocity stack), the impeller inside
    {
        const double rp = 0.046, eye = zc - t0 * 0.9;        // the large pipe's radius; the shroud's front face
        const double inner = front + f.l(0.010);             // how far in the impeller sits
        if (style == 4) {
            const int rings = 12;
            auto rad = [&](double s) { return rp + f.l(0.003) + std::pow(1 - s, 2.4) * f.l(0.024); };   // s 0 at the lip
            for (int i = 0; i < rings; ++i) {
                double s0 = double(i) / rings, s1 = double(i + 1) / rings;
                double z0 = front + (eye - front) * s0, z1 = front + (eye - front) * s1;
                shell_z(b, cx, cy, rad(s0), rad(s1), z0, z1, kChrome, kChrome, seg + 8);
            }
            open_tube_z(b, cx, cy, rad(0) + f.l(0.0025), rad(0) - f.l(0.0015), front, front + f.l(0.003), kChrome, kChrome, seg + 8);   // rolled lip
        } else {
            double rc = rp + f.l(style == 3 ? 0.010 : 0.007);
            const uint8_t* col = style == 0 ? kChrome : style == 2 ? housing : style == 3 ? kChrome : kValveAlloy;
            open_tube_z(b, cx, cy, rc, rp, front + f.l(0.004), eye, col, kBore, seg + 8);
            open_tube_z(b, cx, cy, rc + f.l(0.003), rp, front, front + f.l(0.004), style == 2 ? kChrome : col, kBore, seg + 8);   // the lip
            if (style == 2) open_tube_z(b, cx, cy, rc + f.l(0.0012), rc - f.l(0.001), front + f.l(0.008), front + f.l(0.0105), kAccent, kAccent, seg + 8);
            if (style == 0 || style == 2)                     // a clamp ring of bolts where the collar meets the housing
                for (int k = 0; k < 8; ++k) {
                    double a = k * pi / 4 + pi / 8, rb = rc + f.l(0.002), bc[3] = {cx + rb * std::cos(a), cy + rb * std::sin(a), 0};
                    b.cylinder(2, bc, f.l(0.0017), eye - f.l(0.005), eye, kValveIron, 6);
                }
            if (style == 3)                                   // a screen across the mouth
                for (int k = -3; k <= 3; ++k) {
                    double lo[3] = {cx - rp * std::sqrt(1 - std::pow(k / 3.6, 2)), cy + k * rp / 3.6 - f.l(0.0006), front + f.l(0.004)};
                    double hi[3] = {cx + rp * std::sqrt(1 - std::pow(k / 3.6, 2)), cy + k * rp / 3.6 + f.l(0.0006), front + f.l(0.0055)};
                    b.box(lo, hi, kValveAlloy);
                }
        }
        // inside: the dark eye behind the impeller's blades and its polished nose
        double c[3] = {cx, cy, 0};
        b.disc(2, c, rp, inner + f.l(0.008), false, kValveHole, seg);
        for (int k = 0; k < 9; ++k) {
            double a = k * 2 * pi / 9;
            box_turned(b, cx + std::cos(a) * rp * 0.52, cy + std::sin(a) * rp * 0.52, rp * 0.44, f.l(0.0011), a + 0.5, inner + f.l(0.001), inner + f.l(0.008), kBlade);
        }
        b.frustum(2, c, f.l(0.003), rp * 0.26, inner - f.l(0.002), inner + f.l(0.008), kChrome, 16);   // the nose
    }
    // the gear case: a drum behind the compressor joined to the input shaft's boss at the torque spot (back face,
    // centred on cell 0, 0, where the pulley wheel sits; the same size on both, since a pulley wheel is one size)
    {
        const double ix = 0, iy = 0, g0 = zc + t0, g1 = back - f.l(0.007);
        auto hull = [&](double rd, double ri, double z0, double z1, const uint8_t* col) {
            std::vector<std::array<double, 2>> p; circle_pts(p, cx, cy, rd, 48); circle_pts(p, ix, iy, ri, 24); prism_z(b, p, z0, z1, col);
        };
        if (style == 3) {                                    // a round finned drum with the input on its own arm
            double c[3] = {cx, cy, 0};
            b.cylinder(2, c, f.l(0.050), g0, g1 - f.l(0.010), gears, seg);
            b.frustum(2, c, f.l(0.050), f.l(0.036), g1 - f.l(0.010), g1, gears, seg);   // a domed back
            for (int k = 0; k < 12; ++k) {                   // radial fins
                double a = k * pi / 6;
                box_turned(b, cx + std::cos(a) * f.l(0.050), cy + std::sin(a) * f.l(0.050), f.l(0.006), f.l(0.0014), a, g0 + f.l(0.004), g1 - f.l(0.012), gears);
            }
            hull(f.l(0.020), 0.024, g0 + f.l(0.020), g1, gears);   // the arm down to the input shaft
        } else {
            hull(f.l(0.052), 0.028, g0, g1, gears);
            if (style == 1 || style == 4)                    // cooling fins round the case
                for (int k = 1; k <= 4; ++k) {
                    double z = g0 + (g1 - g0) * k / 5.0;
                    hull(f.l(0.056), 0.031, z - f.l(0.0016), z + f.l(0.0016), gears);
                }
            if (style == 0 || style == 2) {                  // a cover plate on the back with its bolts
                hull(f.l(0.045), 0.022, g1, g1 + f.l(0.003), style == 0 ? kChrome : gears);
                for (int k = 0; k < 6; ++k) {
                    double a = k * pi / 3 + pi / 6, bc[3] = {cx + f.l(0.039) * std::cos(a), cy + f.l(0.039) * std::sin(a), 0};
                    b.cylinder(2, bc, f.l(0.0018), g1 + f.l(0.003), g1 + f.l(0.0055), style == 0 ? kValveIron : kChrome, 6);
                }
            }
            if (style == 1) {                                // a mounting ear out the right side
                double lo[3] = {cx + f.l(0.050), cy - f.l(0.011), g0 + f.l(0.006)}, hi[3] = {f.p(kCell + h) - 0.002, cy + f.l(0.011), g0 + f.l(0.020)};
                b.box(lo, hi, gears);
                double bc[3] = {(lo[0] + hi[0]) / 2, cy, 0};
                b.cylinder(2, bc, f.l(0.0045), g0 + f.l(0.0055), g0 + f.l(0.0205), kValveHole, 10);
            }
        }
        // the input shaft's boss, where the pulley wheel bolts on
        double ic[3] = {ix, iy, 0};
        b.cylinder(2, ic, 0.024, g1, back - 0.004, style == 2 || style == 0 ? kChrome : kValveAlloy, 24);
        b.cylinder(2, ic, 0.016, back - 0.004, back - 0.0005, kValveIron, 24);
        for (int k = 0; k < 4; ++k) {
            double a = pi / 4 + k * pi / 2, bc[3] = {ix + 0.0105 * std::cos(a), iy + 0.0105 * std::sin(a), 0};
            b.cylinder(2, bc, 0.0018, back - 0.0005, back + 0.0010, kValveAlloy, 6);
        }
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

}  // namespace turbo
