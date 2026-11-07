#pragma once
// Twin screw and Roots supercharger meshes, generated in code and written into rom/meshes/components at start-up.
//
// The parts stretch on all three axes: the game moves every vertex past each axis's stretch plane (center_stretch, in
// cells) out by the added cells. The unstretched part spans -0.04 to 0.12 m on each axis. The planes sit where the
// large-pipe ports stay round: the intake flange (centred on the port, 0.04 m across both its axes) is all on the near
// side of the planes across it, at 0.092 m (center_stretch 1.15), and the plane along it is at 0.04 m (0.5), so the
// intake rides out with the length (twin screw, on its back face) or the height (Roots, on top). The housing, ribs and
// lids that cross the planes grow with the blower, the pulley snout stays put; nothing sits on a plane (clear_of_planes).
// Axes: x across, y up, z along the engine (front, z-, carries the drive; the twin screw's intake is on the back).
// Several looks per kind (Nate picks one; the others are dropped once he has).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pdmesh {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNear = 0.04, kFar = 0.092;   // the two stretch plane positions (m), center_stretch 0.5 and 1.15
struct Planes { double p[3]; };
// Twin screw: across x and y at kFar (its back-face intake), along z at kNear. Roots: across x and z (its top intake).
inline Planes planes(bool roots_kind) { return roots_kind ? Planes{{kFar, kNear, kFar}} : Planes{{kFar, kFar, kNear}}; }
constexpr double kCenterNear = 0.5, kCenterFar = 1.15;
struct V3 { double x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 unit(V3 a) { double l = std::sqrt(dot(a, a)); return l > 0 ? a * (1.0 / l) : V3{0, 1, 0}; }
struct Vertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(Vertex) == 36, "mesh vertex");
struct Rgb { uint8_t r, g, b; };
constexpr Rgb kPaint0{153, 0, 0}, kPaint1{153, 37, 0};           // paint slots 0 and 1
constexpr Rgb kAlloy{196, 198, 200}, kPolish{232, 234, 236}, kDark{46, 48, 52}, kIron{78, 80, 84}, kCore{30, 32, 34}, kBelt{24, 24, 26};

struct Mesh {
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    uint32_t vertex(V3 p, V3 n, Rgb c) {
        Vertex x{};
        x.pos[0] = float(p.x); x.pos[1] = float(p.y); x.pos[2] = float(p.z);
        x.rgba[0] = c.r; x.rgba[1] = c.g; x.rgba[2] = c.b; x.rgba[3] = 255;
        x.normal[0] = float(n.x); x.normal[1] = float(n.y); x.normal[2] = float(n.z);
        v.push_back(x);
        return uint32_t(v.size() - 1);
    }
    // a, b, c counter-clockwise seen from outside; stored the other way round, as the game draws clockwise faces.
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.push_back(a); i.push_back(c); i.push_back(b); }
    // A flat face, wound to face `out` (its normal points that way).
    void face(const std::vector<V3>& p, V3 out, Rgb c) {
        if (p.size() < 3) return;
        V3 n = unit(out);
        std::vector<uint32_t> id;
        for (auto& q : p) id.push_back(vertex(q, n, c));
        for (size_t k = 1; k + 1 < p.size(); ++k) {
            V3 f = cross(p[k] - p[0], p[k + 1] - p[0]);
            if (dot(f, f) < 1e-20) continue;
            if (dot(f, out) >= 0) tri(id[0], id[k], id[k + 1]); else tri(id[0], id[k + 1], id[k]);
        }
    }
    void box(V3 lo, V3 hi, Rgb c) {
        V3 p[8];
        for (int k = 0; k < 8; ++k) p[k] = {k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z};
        face({p[0], p[2], p[3], p[1]}, {0, 0, -1}, c);
        face({p[4], p[5], p[7], p[6]}, {0, 0, 1}, c);
        face({p[0], p[4], p[6], p[2]}, {-1, 0, 0}, c);
        face({p[1], p[3], p[7], p[5]}, {1, 0, 0}, c);
        face({p[0], p[1], p[5], p[4]}, {0, -1, 0}, c);
        face({p[2], p[6], p[7], p[3]}, {0, 1, 0}, c);
    }
    // A prism along axis (0 x, 1 y, 2 z) from a0 to a1 with a cross-section `ring` (in the other two axes, in order
    // (y, z) for x, (x, z) for y, (x, y) for z), smooth-shaded sides, capped.
    void prism(int axis, const std::vector<std::pair<double, double>>& ring, double a0, double a1, Rgb c, bool smooth = true, bool caps = true) {
        auto put = [&](double u, double w, double a) {
            return axis == 0 ? V3{a, u, w} : axis == 1 ? V3{u, a, w} : V3{u, w, a};
        };
        size_t n = ring.size();
        double cu = 0, cw = 0;
        for (auto& r : ring) { cu += r.first / n; cw += r.second / n; }
        for (size_t k = 0; k < n; ++k) {
            auto& r0 = ring[k]; auto& r1 = ring[(k + 1) % n];
            V3 p0 = put(r0.first, r0.second, a0), p1 = put(r1.first, r1.second, a0), p2 = put(r1.first, r1.second, a1), p3 = put(r0.first, r0.second, a1);
            V3 mid = put((r0.first + r1.first) / 2 - cu, (r0.second + r1.second) / 2 - cw, 0);
            if (!smooth) { face({p0, p1, p2, p3}, mid, c); continue; }
            V3 n0 = unit(put(r0.first - cu, r0.second - cw, 0)), n1 = unit(put(r1.first - cu, r1.second - cw, 0));
            uint32_t a = vertex(p0, n0, c), b = vertex(p1, n1, c), d = vertex(p2, n1, c), e = vertex(p3, n0, c);
            V3 f = cross(p1 - p0, p3 - p0);
            if (dot(f, mid) >= 0) { tri(a, b, d); tri(a, d, e); } else { tri(a, d, b); tri(a, e, d); }
        }
        if (!caps) return;
        std::vector<V3> c0, c1;
        for (auto& r : ring) { c0.push_back(put(r.first, r.second, a0)); c1.push_back(put(r.first, r.second, a1)); }
        V3 dir = put(0, 0, 1);
        face(c0, dir * -1.0, c); face(c1, dir, c);
    }
};

// A circle (n points, the first half a step off the axes, so no point lands on a middle plane through its centre).
inline std::vector<std::pair<double, double>> circle(double cu, double cw, double r, int n = 24) {
    std::vector<std::pair<double, double>> out;
    for (int k = 0; k < n; ++k) { double a = 2 * kPi * (k + 0.5) / n; out.push_back({cu + r * std::cos(a), cw + r * std::sin(a)}); }
    return out;
}
// A rounded rectangle from (u0, w0) to (u1, w1), corner radius r.
inline std::vector<std::pair<double, double>> rounded(double u0, double w0, double u1, double w1, double r, int per_corner = 4) {
    std::vector<std::pair<double, double>> out;
    double cu[4] = {u1 - r, u0 + r, u0 + r, u1 - r}, cw[4] = {w1 - r, w1 - r, w0 + r, w0 + r};
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k <= per_corner; ++k) {
            double a = kPi / 2 * (c + double(k) / per_corner);
            out.push_back({cu[c] + r * std::cos(a), cw[c] + r * std::sin(a)});
        }
    return out;
}
// The twin screw's figure-8 rotor case: two lobes side by side (centres u0, u1 at height w), joined top and bottom.
inline std::vector<std::pair<double, double>> twin_lobes(double u0, double u1, double w, double r, int n = 12) {
    std::vector<std::pair<double, double>> out;
    for (int k = 0; k <= n; ++k) { double a = -kPi / 2 + kPi * k / n; out.push_back({u1 + r * std::cos(a), w + r * std::sin(a)}); }      // right lobe, bottom to top
    for (int k = 0; k <= n; ++k) { double a = kPi / 2 + kPi * k / n; out.push_back({u0 + r * std::cos(a), w + r * std::sin(a)}); }       // left lobe, top to bottom
    return out;
}

// Pulley drive on the front face, at the torque spot (near x cell, top row: x 0, y 0.08 at z -0.04): a snout boss and
// the input shaft. The stock Pulley Wheel bolts on in front of it.
inline void drive_snout(Mesh& m, Rgb housing) {
    m.prism(2, circle(0.0, 0.08, 0.024, 20), -0.036, -0.026, housing);
    m.prism(2, circle(0.0, 0.08, 0.017, 16), -0.040, -0.036, kIron);
    m.prism(2, circle(0.0, 0.08, 0.008, 12), -0.0435, -0.040, kPolish);
}
// A large-pipe flange (the game's large pipe is 2 x 2 cells; its port centre sits at (0.04, 0.04) on the face).
inline void flange_z(Mesh& m, double z0, double z1, Rgb c) {
    m.prism(2, circle(0.040, 0.040, 0.050, 28), z0, z1, c);
    for (int k = 0; k < 4; ++k) { double a = kPi / 4 + kPi / 2 * k; m.prism(2, circle(0.04 + 0.058 * std::cos(a), 0.04 + 0.058 * std::sin(a), 0.006, 8), z0, z1 + 0.002, kPolish); }
}
inline void flange_y(Mesh& m, double y0, double y1, Rgb c, double r = 0.050) {
    m.prism(1, circle(0.040, 0.040, r, 28), y0, y1, c);
}
// The base flange that bolts to the air manifold (bottom face, the whole footprint, inset).
inline void base_plate(Mesh& m, Rgb c) { m.prism(1, rounded(-0.034, -0.034, 0.114, 0.114, 0.010), -0.040, -0.030, c, false); }

// ------------------------------------------------------------------------------------------- shared detail
using Ring = std::vector<std::pair<double, double>>;
// A ring scaled about (cu, cw).
inline Ring scaled(const Ring& r, double cu, double cw, double k) {
    Ring out;
    for (auto& p : r) out.push_back({cu + (p.first - cu) * k, cw + (p.second - cw) * k});
    return out;
}
inline Ring moved(const Ring& r, double du, double dw) {
    Ring out;
    for (auto& p : r) out.push_back({p.first + du, p.second + dw});
    return out;
}
// A loft along an axis through sections of equal point count (smooth sides), capped at both ends.
inline void loft(Mesh& m, int axis, const std::vector<std::pair<double, Ring>>& s, Rgb c, bool cap0 = true, bool cap1 = true) {
    auto put = [&](double u, double w, double a) { return axis == 0 ? V3{a, u, w} : axis == 1 ? V3{u, a, w} : V3{u, w, a}; };
    if (s.size() < 2) return;
    size_t n = s[0].second.size();
    for (size_t j = 0; j + 1 < s.size(); ++j) {
        const Ring &r0 = s[j].second, &r1 = s[j + 1].second;
        double cu0 = 0, cw0 = 0, cu1 = 0, cw1 = 0;
        for (size_t k = 0; k < n; ++k) { cu0 += r0[k].first / n; cw0 += r0[k].second / n; cu1 += r1[k].first / n; cw1 += r1[k].second / n; }
        for (size_t k = 0; k < n; ++k) {
            size_t q = (k + 1) % n;
            V3 a = put(r0[k].first, r0[k].second, s[j].first), b = put(r0[q].first, r0[q].second, s[j].first);
            V3 d = put(r1[q].first, r1[q].second, s[j + 1].first), e = put(r1[k].first, r1[k].second, s[j + 1].first);
            V3 out = put((r0[k].first + r0[q].first) / 2 - cu0, (r0[k].second + r0[q].second) / 2 - cw0, 0);
            V3 na = unit(put(r0[k].first - cu0, r0[k].second - cw0, 0)), nb = unit(put(r0[q].first - cu0, r0[q].second - cw0, 0));
            V3 nd = unit(put(r1[q].first - cu1, r1[q].second - cw1, 0)), ne = unit(put(r1[k].first - cu1, r1[k].second - cw1, 0));
            uint32_t ia = m.vertex(a, na, c), ib = m.vertex(b, nb, c), id = m.vertex(d, nd, c), ie = m.vertex(e, ne, c);
            V3 f = cross(b - a, e - a);
            if (dot(f, out) >= 0) { m.tri(ia, ib, id); m.tri(ia, id, ie); } else { m.tri(ia, id, ib); m.tri(ia, ie, id); }
        }
    }
    auto cap = [&](const std::pair<double, Ring>& sec, double sign) {
        std::vector<V3> pts;
        for (auto& p : sec.second) pts.push_back(put(p.first, p.second, sec.first));
        m.face(pts, put(0, 0, sign), c);
    };
    if (cap0) cap(s.front(), -1); if (cap1) cap(s.back(), 1);
}
// A hex bolt head standing out of a face along `axis` (sign +-1), centre (u, w) at a.
inline void bolt(Mesh& m, int axis, double u, double w, double a, double sign, double r = 0.0035, Rgb c = kPolish) {
    Ring h;
    for (int k = 0; k < 6; ++k) { double t = kPi / 6 + kPi / 3 * k; h.push_back({u + r * std::cos(t), w + r * std::sin(t)}); }
    m.prism(axis, h, sign > 0 ? a : a - 0.003, sign > 0 ? a + 0.003 : a, c, false);
}
// A pulley snout at the torque spot (x 0, y 0.08, on the front face at z -0.04): a cone out of the case, a bearing
// nose, and the drive hub with its bolts and the shaft. The stock Pulley Wheel bolts on in front of it.
inline void pulley_nose(Mesh& m, double z_case, double r_base, Rgb housing, double cy = 0.08) {
    loft(m, 2, {{z_case, circle(0.0, cy, r_base, 28)}, {-0.031, circle(0.0, cy, 0.019, 28)}, {-0.035, circle(0.0, cy, 0.016, 28)}}, housing);
    m.prism(2, circle(0.0, cy, 0.013, 24), -0.0395, -0.035, kIron);                     // drive hub
    for (int k = 0; k < 5; ++k) { double t = 2 * kPi * (k + 0.5) / 5; bolt(m, 2, 0.0095 * std::cos(t), cy + 0.0095 * std::sin(t), -0.0395, -1, 0.0018); }
    m.prism(2, circle(0.0, cy, 0.0045, 12), -0.0435, -0.0395, kPolish);                 // shaft
}

// ------------------------------------------------------------------------------------------- twin screw looks
// The rotor case of a twin screw seen from the front: two bores side by side under one smooth casting, the male rotor's
// bore a little bigger. Centred on the intake port (x 0.04), y from about 0 to 0.09.
inline Ring screw_case(double grow = 0) {
    Ring r;
    const int n = 18;
    double cl = 0.013, cr = 0.067, cy = 0.052, rl = 0.044 + grow, rr = 0.046 + grow;   // the top clears the height's stretch plane, so it grows
    for (int k = 0; k <= n; ++k) { double a = -kPi / 2 + kPi * k / n; r.push_back({cr + rr * std::cos(a), cy + rr * std::sin(a)}); }   // right bore, bottom to top
    for (int k = 1; k < 5; ++k) { double t = double(k) / 5; r.push_back({cr - (cr - cl) * t, cy + (rl + rr) / 2 - 0.004 * std::sin(kPi * t)}); }   // the saddle on top
    for (int k = 0; k <= n; ++k) { double a = kPi / 2 + kPi * k / n; r.push_back({cl + rl * std::cos(a), cy + rl * std::sin(a)}); }   // left bore, top to bottom
    for (int k = 1; k < 5; ++k) { double t = double(k) / 5; r.push_back({cl + (cr - cl) * t, cy - (rl + rr) / 2 + 0.002}); }       // flat underside
    return r;
}
// The lower intake: a plenum casting under the rotors that bolts to the air manifold, its runners showing down the sides.
inline void screw_lower(Mesh& m, Rgb body, Rgb accent) {
    m.prism(1, rounded(-0.036, -0.034, 0.116, 0.114, 0.012, 6), -0.040, -0.031, accent, false);     // base flange
    for (double z : {-0.026, 0.002, 0.026, 0.060, 0.084, 0.106})                                          // its bolts
        for (double x : {-0.031, 0.111}) bolt(m, 1, x, z, -0.031, 1);
    loft(m, 1, {{-0.031, rounded(-0.032, -0.030, 0.112, 0.110, 0.014, 6)}, {-0.012, rounded(-0.026, -0.024, 0.106, 0.104, 0.018, 6)},
                {0.006, rounded(-0.024, -0.022, 0.104, 0.102, 0.020, 6)}}, body, false, true);
    for (double z : {-0.016, 0.010, 0.068, 0.094})                                                       // runner ribs
        for (double xs : {-0.027, 0.107})
            loft(m, 1, {{-0.031, circle(xs + (xs < 0 ? -0.002 : 0.002), z, 0.0055, 12)}, {0.004, circle(xs + (xs < 0 ? 0.004 : -0.004), z, 0.0045, 12)}}, body);
}
inline void screw_inlet(Mesh& m, double z0, bool throttle_body) {
    loft(m, 2, {{z0, circle(0.040, 0.048, 0.034, 32)}, {z0 + 0.008, circle(0.040, 0.044, 0.039, 32)}, {0.111, circle(0.040, 0.040, 0.044, 32)}}, kAlloy);   // inlet bell
    if (throttle_body) {
        m.prism(2, circle(0.040, 0.040, 0.046, 32), 0.111, 0.119, kAlloy);
        m.prism(2, circle(0.040, 0.040, 0.040, 32), 0.1191, 0.1197, kCore);
        m.box({0.074, 0.030, 0.104}, {0.088, 0.058, 0.118}, kDark);                    // the throttle motor on its side
        m.prism(0, circle(0.040, 0.115, 0.004, 10), -0.010, 0.074, kIron);             // butterfly shaft
    } else {
        m.prism(2, circle(0.040, 0.040, 0.049, 32), 0.111, 0.117, kPolish);
        for (int k = 0; k < 6; ++k) { double t = 2 * kPi * (k + 0.5) / 6; bolt(m, 2, 0.04 + 0.044 * std::cos(t), 0.04 + 0.044 * std::sin(t), 0.117, 1, 0.0028); }
        m.prism(2, circle(0.040, 0.040, 0.037, 32), 0.1171, 0.1177, kCore);
    }
}
// 0 "Kenne Bell": a smooth twin-bore casting with soft ends, two raised bands, the nose cone up front and a round
//   polished inlet on the back.
// 1 "Whipple": the bores polished, cast ribs running along the top of each, and an electronic throttle body on the inlet.
// 2 "Magnuson": a smooth rounded lid hiding the rotors, a raised logo panel and side cooling ribs, the nose cone up front.
inline void twin_screw(Mesh& m, int look) {
    Rgb body = kPaint0, accent = kPaint1;
    screw_lower(m, look == 1 ? kAlloy : body, accent);
    double z0 = -0.026, z1 = 0.100;
    if (look == 2) {
        Ring lid = rounded(-0.030, 0.004, 0.110, 0.100, 0.034, 8);
        loft(m, 2, {{z0, scaled(lid, 0.04, 0.05, 0.85)}, {z0 + 0.006, scaled(lid, 0.04, 0.05, 0.97)}, {z0 + 0.012, lid}, {z1 - 0.010, lid},
                    {z1 - 0.004, scaled(lid, 0.04, 0.05, 0.96)}, {z1, scaled(lid, 0.04, 0.05, 0.86)}}, body);
        m.prism(1, rounded(0.006, 0.010, 0.074, 0.064, 0.010, 6), 0.0995, 0.1045, accent, false);   // logo panel
        for (double y : {0.026, 0.040, 0.054})                                                      // side cooling ribs
            for (double xs : {-0.031, 0.109}) m.prism(2, circle(xs + (xs < 0 ? 0 : 0.002), y, 0.0035, 10), 0.002, 0.082, accent);
    } else {
        Ring c = screw_case();
        loft(m, 2, {{z0, scaled(c, 0.04, 0.052, 0.85)}, {z0 + 0.005, scaled(c, 0.04, 0.052, 0.97)}, {z0 + 0.010, c}, {z1 - 0.008, c},
                    {z1 - 0.003, scaled(c, 0.04, 0.052, 0.97)}, {z1, scaled(c, 0.04, 0.052, 0.88)}}, look == 1 ? kPolish : body);
        if (look == 0)
            for (double z : {-0.006, 0.084}) m.prism(2, screw_case(0.003), z, z + 0.006, accent);    // cast bands
        if (look == 1)
            for (double x : {0.013, 0.067})                                                          // ribs along each bore
                for (double dx : {-0.010, 0.0, 0.010}) {
                    double top = 0.052 + std::sqrt(std::max(0.0, 0.045 * 0.045 - dx * dx));
                    m.prism(2, circle(x + dx, top, 0.003, 10), -0.016, 0.092, accent);
                }
    }
    // the front gear case and the pulley nose
    loft(m, 2, {{-0.033, scaled(screw_case(0.004), 0.04, 0.052, 0.92)}, {-0.029, screw_case(0.004)}, {z0 + 0.002, screw_case(0.004)}}, accent);
    for (int k = 0; k < 8; ++k) {                                                                     // its bolts
        double t = 2 * kPi * (k + 0.5) / 8;
        bolt(m, 2, 0.04 + 0.060 * std::cos(t), 0.052 + 0.036 * std::sin(t), -0.033, -1, 0.0025);
    }
    pulley_nose(m, -0.033, 0.026, accent);
    screw_inlet(m, z1, look == 1);
    // a boost reference fitting and a coolant boss on top, near the back
    m.prism(1, circle(0.030, 0.070, 0.0045, 10), 0.0935, 0.106, kAlloy);
    m.prism(1, circle(0.060, 0.074, 0.006, 12), 0.0935, 0.101, kIron);
}

// ------------------------------------------------------------------------------------------- Roots looks
// The classic blower case seen from the front: a deep rounded box with its long ribs, rotors side by side.
inline Ring roots_case(double grow = 0) { return rounded(-0.032 - grow, -0.014 - grow, 0.112 + grow, 0.082 + grow, 0.016 + grow, 6); }
// The adapter that bolts the blower to the manifold: a tapered plate under the case.
inline void roots_adapter(Mesh& m, Rgb c) {
    m.prism(1, rounded(-0.036, -0.036, 0.116, 0.116, 0.010, 6), -0.040, -0.030, c, false);
    loft(m, 1, {{-0.030, rounded(-0.034, -0.032, 0.114, 0.112, 0.011, 6)}, {-0.014, rounded(-0.031, -0.026, 0.111, 0.106, 0.012, 6)}}, c, false, false);
    for (double z : {-0.028, 0.000, 0.024, 0.060, 0.084, 0.108})
        for (double x : {-0.033, 0.113}) bolt(m, 1, x, z, -0.030, 1, 0.003);
}
// 0 "6-71": the polished classic, rounded ribs down both sides, the tall gear case with its bolts up front, a bolted
//   inlet plate on top.
// 1 "Bug catcher": the 6-71 under a tall scoop, its mesh mouth facing forward.
// 2 "Street": the 6-71 in black with polished ribs and a chrome air cleaner on top.
inline void roots(Mesh& m, int look) {
    Rgb body = look == 2 ? kDark : kPaint0, trim = look == 2 ? kPolish : kPaint1;
    roots_adapter(m, look == 2 ? kDark : kAlloy);
    double z0 = -0.022, z1 = 0.100, top = 0.082;
    Ring c = roots_case();
    loft(m, 2, {{z0, scaled(c, 0.04, 0.034, 0.95)}, {z0 + 0.004, c}, {z1 - 0.004, c}, {z1, scaled(c, 0.04, 0.034, 0.95)}}, body);
    for (double y : {-0.006, 0.008, 0.022, 0.050, 0.064})                                        // ribs down both sides (round topped)
        for (double xs : {-0.032, 0.112}) m.prism(2, circle(xs + (xs < 0 ? -0.002 : 0.002), y, 0.0042, 12), z0 + 0.004, z1 - 0.004, trim);
    for (double x : {-0.012, 0.016, 0.064, 0.086})                                               // and a few along the top edges
        m.prism(2, circle(x, top + 0.001, 0.0032, 10), z0 + 0.006, z1 - 0.006, trim);
    // the gear case: taller than the case, rounded over the drive, bolted round its edge
    Ring g = rounded(-0.034, -0.016, 0.114, 0.100, 0.030, 8);
    loft(m, 2, {{-0.034, scaled(g, 0.04, 0.042, 0.94)}, {-0.030, g}, {z0 + 0.001, g}}, trim);
    for (int k = 0; k < 10; ++k) {
        double t = 2 * kPi * (k + 0.5) / 10;
        bolt(m, 2, 0.04 + 0.064 * std::cos(t), 0.042 + 0.048 * std::sin(t), -0.034, -1, 0.0026);
    }
    pulley_nose(m, -0.034, 0.028, trim);
    loft(m, 2, {{z1 - 0.001, c}, {0.108, c}, {0.111, scaled(c, 0.04, 0.034, 0.95)}}, trim);       // rear bearing plate
    for (int k = 0; k < 8; ++k) { double t = 2 * kPi * (k + 0.5) / 8; bolt(m, 2, 0.04 + 0.060 * std::cos(t), 0.034 + 0.036 * std::sin(t), 0.111, 1, 0.0024); }
    // on top: the inlet
    m.prism(1, rounded(-0.016, -0.006, 0.096, 0.086, 0.010, 6), top, top + 0.006, kAlloy, false);
    for (double x : {-0.010, 0.086}) for (double z : {0.000, 0.026, 0.054, 0.080}) bolt(m, 1, x, z, top + 0.006, 1, 0.0025);
    if (look == 0) {
        m.prism(1, circle(0.040, 0.040, 0.040, 32), top + 0.006, top + 0.009, kPolish);
        m.prism(1, circle(0.040, 0.040, 0.034, 32), top + 0.0091, top + 0.0095, kCore);
    } else if (look == 1) {
        // a Hilborn-style bug catcher: a tall scoop that leans forward, its mouth a mesh, a lip round the mouth
        double y0 = top + 0.006, y1 = top + 0.090;
        Ring base = rounded(-0.010, -0.002, 0.090, 0.082, 0.020, 8);
        loft(m, 1, {{y0, base}, {y0 + 0.030, moved(scaled(base, 0.04, 0.04, 1.04), 0, -0.006)}, {y1, moved(scaled(base, 0.04, 0.04, 1.08), 0, -0.016)}}, kPolish);
        // the mouth: across the front of the top, sloping back
        double mz = -0.003 - 0.016, back = 0.070;
        m.face({{-0.012, y1 + 0.0005, mz + 0.004}, {0.088, y1 + 0.0005, mz + 0.004}, {0.088, y1 + 0.0005, back}, {-0.012, y1 + 0.0005, back}}, {0, 1, 0}, kCore);
        for (double x = -0.004; x < 0.086; x += 0.010)
            if (std::fabs(x - kFar) > 0.002) m.box({x - 0.0008, y1, mz + 0.004}, {x + 0.0008, y1 + 0.0025, back}, kIron);
        Ring lip = moved(scaled(base, 0.04, 0.04, 1.11), 0, -0.016);
        loft(m, 1, {{y1 - 0.004, lip}, {y1 + 0.003, lip}}, kPolish, false, false);
    } else {
        // a chrome air cleaner: a round lid over a filter band, its stud and wing nut on top
        m.prism(1, circle(0.040, 0.040, 0.044, 36), top + 0.006, top + 0.010, kPolish);
        m.prism(1, circle(0.040, 0.040, 0.046, 36), top + 0.010, top + 0.030, kCore);
        loft(m, 1, {{top + 0.030, circle(0.040, 0.040, 0.050, 36)}, {top + 0.036, circle(0.040, 0.040, 0.049, 36)}, {top + 0.040, circle(0.040, 0.040, 0.040, 36)}}, kPolish);
        m.prism(1, circle(0.040, 0.040, 0.004, 10), top + 0.040, top + 0.048, kPolish);
        m.box({0.031, top + 0.044, 0.038}, {0.049, top + 0.048, 0.042}, kPolish);
    }
}

constexpr int kTwinScrewLooks = 3, kRootsLooks = 3;
// The looks that ship: Nate picked the Magnuson style twin screw (2026-11-04, round 2). The Roots is on hold.
constexpr int kTwinScrewLook = 2, kRootsLook = 0;
// A vertex within 1 mm of a stretch plane is eased 1 mm off it, on its own side (a few curves land close to one).
inline void ease_off_planes(Mesh& m, bool roots_kind) {
    Planes pl = planes(roots_kind);
    for (auto& x : m.v)
        for (int k = 0; k < 3; ++k) {
            double d = double(x.pos[k]) - pl.p[k];
            if (std::fabs(d) < 0.001) x.pos[k] = float(pl.p[k] + (d < 0 ? -0.001 : 0.001));
        }
}
inline Mesh build(bool roots_kind, int look) {
    Mesh m;
    if (roots_kind) roots(m, look); else twin_screw(m, look);
    ease_off_planes(m, roots_kind);
    return m;
}
// Nothing may sit on a stretch plane (the game could not tell which way it goes).
inline bool clear_of_planes(const Mesh& m, bool roots_kind, double margin = 0.0005) {
    Planes pl = planes(roots_kind);
    for (auto& x : m.v)
        for (int k = 0; k < 3; ++k)
            if (std::fabs(double(x.pos[k]) - pl.p[k]) < margin) return false;
    return true;
}
// The part as the game draws it stretched by (ex, ey, ez) cells (for previews and tests).
inline Mesh stretched(Mesh m, bool roots_kind, int ex, int ey, int ez) {
    Planes pl = planes(roots_kind);
    const double add[3] = {ex * 0.08, ey * 0.08, ez * 0.08};
    for (auto& x : m.v) for (int k = 0; k < 3; ++k) if (x.pos[k] > pl.p[k]) x.pos[k] = float(x.pos[k] + add[k]);
    return m;
}

inline const uint8_t* attribute_header() {
    static const uint8_t h[88] = {3, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    return h;
}
inline std::vector<uint8_t> encode(const Mesh& m, const std::string& name) {
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& x : m.v) for (int j = 0; j < 3; ++j) { bounds[j] = std::min(bounds[j], double(x.pos[j])); bounds[3 + j] = std::max(bounds[3 + j], double(x.pos[j])); }
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* b = static_cast<const uint8_t*>(p); out.insert(out.end(), b, b + k); };
    uint32_t u = 5, one = 1;
    put("mesh", 4); put(&u, 4); put(&one, 4);
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(attribute_header(), 88);
    put(bounds, sizeof bounds);
    u = uint32_t(m.v.size() * sizeof(Vertex)); put(&u, 4); put(m.v.data(), m.v.size() * sizeof(Vertex));
    u = uint32_t(m.i.size() * 4); put(&u, 4); put(m.i.data(), m.i.size() * 4);
    uint8_t trailer[8]{};
    put(trailer, 8);
    return out;
}

}  // namespace pdmesh
