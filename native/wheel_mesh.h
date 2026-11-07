#pragma once
// AnyVehicleOverhaul race and drag wheels: rim + tyre meshes generated in code (Nate 2026-11-07). Axle along z, the
// outer face looks +z, centre at the origin. Three rim styles for Nate to pick from, each on any of the tyres:
//   0  Drag 5-spoke   polished, wide flat tapered spokes, deep lip
//   1  Race 6-spoke   forged-look thick spokes, bronze
//   2  Mesh           two-piece cross-spoke with a polished lip and rivets
// Tyres: race (low profile, two grooves), drag slick (tall, wide, round shoulders), skinny drag front.
#include "turbo_pd_mesh.h"
#include <cmath>
#include <vector>

namespace wheelmesh {
using pdmesh::Mesh;
using pdmesh::Rgb;
using pdmesh::V3;
using pdmesh::kPi;

constexpr Rgb kRubber{30, 30, 32}, kSidewall{40, 40, 43}, kLetter{150, 150, 150}, kBacking{22, 22, 24};
constexpr Rgb kPolished{236, 238, 240}, kBronze{168, 132, 72}, kGunmetal{70, 72, 78}, kGoldMesh{196, 170, 104}, kSilver{200, 202, 206};

struct Pt { double r, z; Rgb c; };
// Revolve a profile around z. The outside of the surface is on the right of the direction of travel in (r, z)
// (normal = (dz, -dr)), so a profile walked in +z at a fixed r faces outward.
inline void lathe(Mesh& m, const std::vector<Pt>& p, int n = 48) {
    for (size_t j = 0; j + 1 < p.size(); ++j) {
        double dr = p[j + 1].r - p[j].r, dz = p[j + 1].z - p[j].z;
        if (std::abs(dr) + std::abs(dz) < 1e-9) continue;
        for (int k = 0; k < n; ++k) {
            double a0 = 2 * kPi * k / n, a1 = 2 * kPi * (k + 1) / n, am = (a0 + a1) / 2;
            auto P = [&](const Pt& q, double a) { return V3{q.r * std::cos(a), q.r * std::sin(a), q.z}; };
            V3 out{dz * std::cos(am), dz * std::sin(am), -dr};
            m.face({P(p[j], a0), P(p[j], a1), P(p[j + 1], a1), P(p[j + 1], a0)}, out, p[j].c);
        }
    }
}

struct Tyre { double outer, rim, width, shoulder; int kind; };   // kind 0 race, 1 drag slick, 2 skinny front
inline Tyre tyre_of(int kind) {
    switch (kind) {
    case 1: return {0.36, 0.215, 0.32, 0.07, 1};
    case 2: return {0.33, 0.235, 0.10, 0.025, 2};
    default: return {0.33, 0.24, 0.24, 0.025, 0};
    }
}

inline void tyre(Mesh& m, const Tyre& t) {
    const double w = t.width / 2, R = t.outer, s = t.shoulder;
    std::vector<Pt> p;
    p.push_back({t.rim, -w + 0.01, kSidewall});
    // back sidewall up to the shoulder (bulging a little), round shoulder, tread, front shoulder, front sidewall
    const int ns = 6;
    for (int i = 0; i <= ns; ++i) { double f = double(i) / ns; p.push_back({t.rim + (R - s - t.rim) * f, -w - 0.012 * std::sin(kPi * f), kSidewall}); }
    for (int i = 1; i <= ns; ++i) { double a = kPi / 2 * i / ns; p.push_back({R - s + s * std::sin(a), -w + s - s * std::cos(a), kRubber}); }
    if (t.kind == 0) {   // two grooves in the race tread
        const double g = 0.012;
        for (double gz : {-0.04, 0.04}) {
            p.push_back({R, gz - g, kRubber}); p.push_back({R - 0.008, gz - g, kBacking});
            p.push_back({R - 0.008, gz + g, kRubber}); p.push_back({R, gz + g, kRubber});
        }
    }
    p.push_back({R, w - s, kRubber});
    for (int i = 1; i <= ns; ++i) { double a = kPi / 2 * i / ns; p.push_back({R - s + s * std::cos(a), w - s + s * std::sin(a), kSidewall}); }
    for (int i = 1; i <= ns; ++i) {
        double f = double(i) / ns, r = (R - s) - (R - s - t.rim) * f;
        // a light band of "lettering" round the middle of the front sidewall
        bool band = f > 0.35 && f < 0.6 && t.kind != 2;
        p.push_back({r, w + 0.012 * std::sin(kPi * (1 - f)), band ? kLetter : kSidewall});
    }
    p.push_back({t.rim, w - 0.01, kSidewall});
    lathe(m, p, 64);
}

// The rim's lip, barrel and a dark backing plate deep inside; the spokes sit between the hub and the lip.
inline void barrel(Mesh& m, double rr, double zf, double zb, Rgb lip, double lip_w = 0.014) {
    lathe(m, {{rr + 0.004, zf - 0.02, lip}, {rr + 0.004, zf, lip}, {rr - lip_w, zf, lip}, {rr - lip_w, zb, kGunmetal}, {0.0, zb, kBacking}}, 64);
}
inline void hub(Mesh& m, double rh, double z, Rgb c, Rgb cap, int lugs) {
    lathe(m, {{rh, z - 0.03, c}, {rh, z, c}, {rh * 0.55, z + 0.004, c}, {rh * 0.32, z + 0.004, cap}, {rh * 0.30, z + 0.012, cap}, {0.0, z + 0.014, cap}}, 40);
    for (int k = 0; k < lugs; ++k) {
        double a = 2 * kPi * (k + 0.5) / lugs, rb = rh * 0.72;
        pdmesh::bolt(m, 2, rb * std::cos(a), rb * std::sin(a), z + 0.002, 1, 0.006, kSilver);
    }
}
// One spoke from radius r0 (width w0, face z0) to r1 (width w1, face z1) along angle a, `depth` thick.
inline void spoke(Mesh& m, double a, double r0, double r1, double w0, double w1, double z0, double z1, double depth, Rgb c, double twist = 0) {
    V3 d0{std::cos(a), std::sin(a), 0}, d1{std::cos(a + twist), std::sin(a + twist), 0};
    V3 s0{-d0.y, d0.x, 0}, s1{-d1.y, d1.x, 0};
    V3 iL = d0 * r0 + s0 * (w0 / 2), iR = d0 * r0 - s0 * (w0 / 2), oL = d1 * r1 + s1 * (w1 / 2), oR = d1 * r1 - s1 * (w1 / 2);
    auto at = [](V3 p, double z) { return V3{p.x, p.y, z}; };
    V3 fiL = at(iL, z0), fiR = at(iR, z0), foL = at(oL, z1), foR = at(oR, z1);
    V3 biL = at(iL, z0 - depth), biR = at(iR, z0 - depth), boL = at(oL, z1 - depth), boR = at(oR, z1 - depth);
    V3 n = pdmesh::unit(pdmesh::cross(foR - fiR, fiL - fiR));
    if (n.z < 0) n = n * -1.0;
    m.face({fiR, foR, foL, fiL}, n, c);
    V3 side = s0 + s1;
    m.face({fiL, foL, boL, biL}, side, c);
    m.face({fiR, biR, boR, foR}, side * -1.0, c);
}

// Rim styles.
inline void rim(Mesh& m, int style, double rr, double width) {
    const double zf = width / 2 - 0.004, zb = -width / 2 + 0.02;
    switch (style) {
    case 0: {   // drag 5-spoke: deep dish, spokes drop from the lip down to a hub set far back
        barrel(m, rr, zf, zb, kPolished, 0.018);
        const double zh = zf - 0.07;
        for (int k = 0; k < 5; ++k) spoke(m, 2 * kPi * k / 5 + kPi / 2, 0.055, rr - 0.018, 0.075, 0.022, zh + 0.004, zf - 0.006, 0.018, kPolished);
        hub(m, 0.06, zh + 0.006, kPolished, kPolished, 5);
        break;
    }
    case 1: {   // race 6-spoke: flat face, thick spokes that taper, a step where they meet the lip
        barrel(m, rr, zf, zb, kGunmetal, 0.010);
        const double zh = zf - 0.035;
        for (int k = 0; k < 6; ++k) spoke(m, 2 * kPi * k / 6, 0.05, rr - 0.010, 0.05, 0.034, zh + 0.012, zf - 0.012, 0.03, kBronze);
        hub(m, 0.055, zh + 0.012, kBronze, kGunmetal, 5);
        break;
    }
    default: {   // mesh: ten Y spokes crossing into a lattice, polished lip with rivets
        barrel(m, rr, zf, zb, kPolished, 0.03);
        const double zh = zf - 0.03, ro = rr - 0.03;
        for (int k = 0; k < 10; ++k) {
            double a = 2 * kPi * k / 10;
            spoke(m, a, 0.05, ro, 0.016, 0.010, zh + 0.008, zf - 0.016, 0.012, kGoldMesh, 0.17);
            spoke(m, a, 0.05, ro, 0.016, 0.010, zh + 0.010, zf - 0.014, 0.012, kGoldMesh, -0.17);
        }
        for (int k = 0; k < 30; ++k) {
            double a = 2 * kPi * k / 30, rb = rr - 0.016;
            pdmesh::bolt(m, 2, rb * std::cos(a), rb * std::sin(a), zf, 1, 0.003, kSilver);
        }
        hub(m, 0.055, zh + 0.010, kGoldMesh, kSilver, 5);
        break;
    }
    }
}

inline Mesh wheel(int style, int tyre_kind, bool with_tyre = true) {
    Mesh m;
    Tyre t = tyre_of(tyre_kind);
    if (with_tyre) tyre(m, t);
    rim(m, style, t.rim, t.width);
    return m;
}

// As the game's tyre items lie: axle along y, the inboard face (against the wheel part) at y kInboardY, the outer
// face looking +y (the stock rim_20 and car wheels span y -0.195 .. +0.075, their hub near +y).
constexpr double kInboardY = -0.195;
inline Mesh game_wheel(int style, int tyre_kind, bool with_tyre = true) {
    Mesh m = wheel(style, tyre_kind, with_tyre);
    const double shift = kInboardY + tyre_of(tyre_kind).width / 2 + 0.012;   // the tyre's back sidewall bulge lands on kInboardY
    for (auto& v : m.v) {   // rotate -90 deg about x: (x, y, z) -> (x, z, -y), then slide along the axle
        float y = v.pos[1], z = v.pos[2], ny = v.normal[1], nz = v.normal[2];
        v.pos[1] = float(z + shift); v.pos[2] = -y;
        v.normal[1] = nz; v.normal[2] = -ny;
    }
    return m;
}

}  // namespace wheelmesh
