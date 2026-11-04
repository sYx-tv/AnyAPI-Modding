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

// ------------------------------------------------------------------------------------------- twin screw looks
// 0 "Smooth cast": a rounded cast case over the twin rotors, the gear case up front, a round inlet on the back.
// 1 "Ribbed": the same case with fins along the top and the sides, like a cast race blower.
// 2 "Intercooled lid": a squared-off case carrying a black intercooler core on top between two end tanks.
inline void twin_screw(Mesh& m, int look) {
    Rgb body = kPaint0, accent = kPaint1;
    double top = look == 2 ? 0.070 : 0.095;
    if (look == 2) m.prism(2, rounded(-0.036, -0.030, 0.116, top, 0.008), -0.030, 0.105, body, false);
    else m.prism(2, twin_lobes(0.010, 0.070, 0.035, 0.048, 10), -0.030, 0.105, body);
    // the gear case up front, following the rotor case (rounded), with the drive snout standing out of it
    if (look == 2) m.prism(2, rounded(-0.038, -0.032, 0.118, top + 0.004, 0.010), -0.034, -0.026, accent, false);
    else m.prism(2, twin_lobes(0.010, 0.070, 0.035, 0.050, 10), -0.034, -0.026, accent);
    drive_snout(m, accent);
    base_plate(m, body);
    flange_z(m, 0.105, 0.120, kAlloy);                                                   // inlet (back face)
    m.prism(2, circle(0.040, 0.040, 0.036, 24), 0.118, 0.1205, kDark);                   // its bore
    if (look == 1) {
        for (double x : {-0.012, 0.016, 0.096, 0.112})                                   // fins along the top
            m.box({x - 0.003, 0.070, -0.026}, {x + 0.003, 0.100 + (x > 0 && x < 0.10 ? 0.0 : -0.004), 0.100}, accent);
        for (double y : {0.005, 0.030, 0.055})                                           // and down the sides
            for (double xs : {-0.046, 0.118}) m.box({xs, y - 0.003, -0.026}, {xs + 0.008, y + 0.003, 0.100}, accent);
    }
    if (look == 2) {
        m.box({-0.030, top, -0.020}, {0.110, top + 0.030, 0.095}, kCore);                // intercooler core
        for (double x = -0.024; x < 0.106; x += 0.0185)                                  // its fin lines
            if (std::fabs(x - kFar) > 0.002) m.box({x - 0.0012, top + 0.030, -0.018}, {x + 0.0012, top + 0.0315, 0.093}, kIron);
        m.box({-0.036, top, -0.030}, {0.116, top + 0.036, -0.020}, kAlloy);               // end tanks
        m.box({-0.036, top, 0.095}, {0.116, top + 0.036, 0.105}, kAlloy);
        m.prism(0, circle(top + 0.014, 0.100, 0.007, 10), 0.116, 0.126, kAlloy);          // coolant necks
        m.prism(0, circle(top + 0.014, -0.025, 0.007, 10), 0.116, 0.126, kAlloy);
    }
}

// ------------------------------------------------------------------------------------------- Roots looks
// 0 "Classic": a polished, deep-ribbed case, the gear drive cover up front, a flat inlet plate on top with its bolts.
// 1 "Bug catcher": the classic case under a tall scoop with an open mesh mouth facing forward.
// 2 "Street": a smoother black case with rounded edges and polished strakes, a low round inlet on top.
inline void roots(Mesh& m, int look) {
    Rgb body = look == 2 ? kDark : kPaint0, rib = look == 2 ? kPolish : kPaint1;
    double top = 0.085;
    m.prism(2, look == 2 ? rounded(-0.036, -0.030, 0.116, top, 0.022) : rounded(-0.036, -0.030, 0.116, top, 0.006), -0.026, 0.108, body, look == 2);
    if (look != 2)
        for (double y = -0.020; y < top - 0.005; y += 0.0145)                           // deep ribs down both sides
            if (std::fabs(y - kNear) > 0.003)
                for (double xs : {-0.044, 0.116}) m.box({xs, y - 0.0025, -0.022}, {xs + 0.008, y + 0.0025, 0.104}, rib);
    if (look == 2)                                                                       // polished strakes down both sides
        for (double y : {0.000, 0.022, 0.058})
            for (double xs : {-0.040, 0.116}) m.box({xs, y - 0.004, -0.022}, {xs + 0.004, y + 0.004, 0.104}, rib);
    // the gear drive cover up front: rounded on top, as on a classic blower's snout
    m.prism(2, rounded(-0.036, -0.032, 0.116, top + 0.008, look == 2 ? 0.020 : 0.030), -0.034, -0.026, rib, true);
    drive_snout(m, rib);
    m.prism(2, rounded(-0.036, -0.030, 0.116, top, 0.010), 0.108, 0.116, rib, false);   // rear bearing plate
    base_plate(m, body);
    if (look == 0) {                                                                     // inlet plate with its bolts
        m.box({-0.020, top, -0.010}, {0.100, top + 0.008, 0.090}, kAlloy);
        for (double x : {-0.012, 0.084}) for (double z : {-0.002, 0.026, 0.054, 0.082})
            m.prism(1, circle(x, z, 0.0035, 6), top + 0.008, top + 0.011, kPolish);
        m.prism(1, circle(0.040, 0.040, 0.038, 24), top + 0.008, top + 0.0095, kDark);
    } else if (look == 1) {                                                              // bug catcher scoop
        m.box({-0.020, top, -0.010}, {0.100, top + 0.008, 0.090}, kAlloy);
        double s0 = top + 0.008, s1 = top + 0.095;
        m.face({{-0.016, s0, 0.086}, {0.096, s0, 0.086}, {0.096, s1, 0.050}, {-0.016, s1, 0.050}}, {0, 0.6, 1}, kPolish);      // back, sloped
        m.face({{-0.016, s0, -0.006}, {-0.016, s0, 0.086}, {-0.016, s1, 0.050}, {-0.016, s1, -0.020}}, {-1, 0, 0}, kPolish);   // sides
        m.face({{0.096, s0, -0.006}, {0.096, s1, -0.020}, {0.096, s1, 0.050}, {0.096, s0, 0.086}}, {1, 0, 0}, kPolish);
        m.face({{-0.016, s1, -0.020}, {-0.016, s1, 0.050}, {0.096, s1, 0.050}, {0.096, s1, -0.020}}, {0, 1, 0}, kPolish);       // top
        m.face({{-0.012, s0 + 0.004, -0.007}, {0.088, s0 + 0.004, -0.007}, {0.088, s1 - 0.004, -0.019}, {-0.012, s1 - 0.004, -0.019}}, {0, 0, -1}, kCore);   // mesh mouth
        for (double x = -0.002; x < 0.09; x += 0.0135)
            if (std::fabs(x - kFar) > 0.002) m.box({x - 0.001, s0 + 0.004, -0.0205}, {x + 0.001, s1 - 0.004, -0.018}, kIron);
        m.box({-0.020, s1 - 0.004, -0.024}, {0.100, s1 + 0.002, -0.016}, kPolish);       // lip
    } else {
        flange_y(m, top + 0.003, top + 0.012, kPolish, 0.044);
        m.prism(1, circle(0.040, 0.040, 0.032, 24), top + 0.012, top + 0.0135, kCore);
    }
}

constexpr int kTwinScrewLooks = 3, kRootsLooks = 3;
// The looks that ship. PENDING Nate's pick (2026-11-04, three of each shown); until then the first of each.
constexpr int kTwinScrewLook = 0, kRootsLook = 0;
inline Mesh build(bool roots_kind, int look) {
    Mesh m;
    if (roots_kind) roots(m, look); else twin_screw(m, look);
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
