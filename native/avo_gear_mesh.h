#pragma once
// Trailing link gear meshes (look 3, picked by Nate 2026-11-25), generated in code like AnyMoto's (anymoto_mesh.h:
// same vertex format, paint slots and face markers). Body in paint slot 0, the coil spring in slot 1.
//   base   static: the mount block with its link and torque markers, and the bracket out to the top of the leg
//   steer  turns with the steering about the leg: the leg, the shock bracket and the pivot block
//   arm    swings about the pivot (origin), +z runs to the axle: two side plates and the axle
//   shock  stretched from the bracket (z 0) to the arm's lug (z 1): body, chrome shaft, spring coils
//   tyre   the built-in wheel, centred on the origin, axle along x
#include "avo_gear_logic.h"
#include "anymoto_mesh.h"
#include <cmath>

namespace avogear {
using anymoto::Mesh;
using anymoto::Rgb;
using anymoto::Marker;
using anymoto::marker;

constexpr Rgb kBody = anymoto::kPaint0, kSpring = anymoto::kPaint1;
constexpr Rgb kIron = anymoto::kIron, kChrome = anymoto::kChrome, kBolt = anymoto::kBolt, kMid = anymoto::kMid, kRubber = anymoto::kRubber;
constexpr Rgb kRim{150, 152, 156};

inline Mesh gear_base(const Size& s) {
    Layout l = layout(s);
    Mesh m;
    m.box({-0.12, -0.04, -0.12}, {0.12, 0.04, 0.12}, kBody);                          // mount plate (top face bolts on)
    m.box({-0.06, -0.07, -0.06}, {0.06, -0.04, 0.06}, kBody);                         // boss under the plate
    m.prism(1, {0, 0, l.leg_z}, l.leg_r * 1.25, l.leg_top - 0.06, l.leg_top - 0.025, kIron, 10);   // steering collar
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) m.prism(1, {sx * 0.09, -0.04, sz * 0.09}, 0.009, -0.008, 0, kBolt, 6);
    Vec3 up{0, 1, 0};
    marker(m, {-0.12, 0, 0}, {-1, 0, 0}, up, Marker::link_in);       // steering_pos
    marker(m, {0.12, 0, 0}, {1, 0, 0}, up, Marker::link_in);         // steering_neg
    marker(m, {0, 0, -0.12}, {0, 0, -1}, up, Marker::link_in);       // brake
    marker(m, {0, 0, 0.12}, {0, 0, 1}, up, Marker::torque);          // drive
    return m;
}
// In the steer frame (part frame at zero steering): the leg from the collar down to the pivot block.
inline Mesh gear_steer(const Size& s) {
    Layout l = layout(s);
    double k = s.scale;
    Mesh m;
    m.prism(1, {0, 0, l.leg_z}, l.leg_r, l.pivot.y, l.leg_top - 0.06, kBody, 12);
    m.prism(1, {0, 0, l.leg_z}, l.leg_r * 1.15, l.pivot.y - 0.01, l.pivot.y + 0.05 * k, kBody, 12);    // foot
    // Pivot block across the foot, wide enough for the arm's plates.
    m.box({-l.fork_x - 0.015, l.pivot.y - 0.03 * k, l.leg_z - l.leg_r}, {l.fork_x + 0.015, l.pivot.y + 0.03 * k, l.leg_z + l.leg_r}, kBody);
    // Shock bracket on the front of the leg, a cross bar out to both shocks (they sit either side of the wheel).
    m.box({-0.025 * k, l.shock_top.y - 0.03 * k, l.leg_z}, {0.025 * k, l.shock_top.y + 0.03 * k, l.shock_top.z}, kBody);
    m.prism(0, l.shock_top, 0.016 * k, -l.fork_x - 0.02, l.fork_x + 0.02, kBody, 8);
    for (int sx = -1; sx <= 1; sx += 2) m.prism(0, {sx * (l.fork_x + 0.02), l.shock_top.y, l.shock_top.z}, 0.012 * k, 0, sx * 0.008, kBolt, 6);
    return m;
}
inline Mesh gear_arm(const Size& s) {
    Layout l = layout(s);
    double k = s.scale;
    Mesh m;
    m.prism(0, {0, 0, 0}, 0.028 * k, -l.fork_x - 0.02, l.fork_x + 0.02, kIron, 10);              // pivot tube
    for (int sx = -1; sx <= 1; sx += 2) {
        m.box({sx * l.fork_x - 0.012, -0.026 * k, 0}, {sx * l.fork_x + 0.012, 0.026 * k, l.arm + 0.03 * k}, kBody);
        m.prism(0, {sx * (l.fork_x + 0.012), 0, l.arm}, 0.02 * k, 0, 0.008, kBolt, 6);           // axle nut
    }
    m.box({-l.fork_x, -0.02 * k, 0.03 * k}, {l.fork_x, 0.02 * k, 0.09 * k}, kBody);               // cross brace by the pivot
    // Shock lugs on top of both side plates, at shock_along.
    double z = l.arm * l.shock_along;
    for (int sx = -1; sx <= 1; sx += 2)
        m.box({sx * l.fork_x - 0.014, 0.0, z - 0.03 * k}, {sx * l.fork_x + 0.014, l.shock_lift + 0.012 * k, z + 0.03 * k}, kBody);
    m.prism(0, {0, 0, l.arm}, 0.016 * k, -l.fork_x, l.fork_x, kMid, 8);                            // axle
    return m;
}
// Two coilovers, one each side of the wheel. stretch_between's frame has its y axis along the part's x, so the
// pair sits at y = +-fork_x here.
inline Mesh gear_shock(const Size& s) {
    double k = s.scale;
    Layout l = layout(s);
    Mesh m;
    for (int sx = -1; sx <= 1; sx += 2) {
        Vec3 c{0, sx * l.fork_x, 0};
        m.prism(2, c, 0.022 * k, 0.0, 0.55, kIron, 10);
        m.prism(2, c, 0.009 * k, 0.5, 1.0, kChrome, 8);
        m.box(c + Vec3{-0.018 * k, -0.018 * k, 0.94}, c + Vec3{0.018 * k, 0.018 * k, 1.0}, kIron);
        for (int n = 0; n < 8; ++n) {
            double z = 0.08 + n * 0.11;
            m.prism(2, c, 0.034 * k, z, z + 0.035, kSpring, 10);
        }
    }
    return m;
}
// The wheel: a rounded tyre on a grey hub, axle along x.
// Both sides of a quad: the lathed faces' winding is not worth getting right by hand, and the game culls by it.
inline void quad2(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb col) { m.quad(a, b, c, d, col); m.quad(a, d, c, b, col); }
inline Mesh gear_tyre(const Size& s) {
    const double R = s.radius, h = s.width / 2, rim = R * 0.62;
    Mesh m;
    constexpr int n = 28;
    // (radius, x) stations of the tyre's outside, from the inner sidewall round the tread to the outer one.
    const double pr[] = {rim, R * 0.88, R * 0.97, R, R, R * 0.97, R * 0.88, rim};
    const double px[] = {-h * 0.92, -h, -h * 0.80, -h * 0.45, h * 0.45, h * 0.80, h, h * 0.92};
    auto at = [&](double r, double x, int k) { double a = 2 * kPi * k / n; return Vec3{x, r * std::cos(a), r * std::sin(a)}; };
    for (int j = 0; j + 1 < 8; ++j)
        for (int k = 0; k < n; ++k)
            quad2(m, at(pr[j], px[j], k), at(pr[j], px[j], k + 1), at(pr[j + 1], px[j + 1], k + 1), at(pr[j + 1], px[j + 1], k), kRubber);
    // Hub: a drum inside the tyre with dished faces and a bolt circle each side, so the spin shows.
    for (int k = 0; k < n; ++k) quad2(m, at(rim, -h * 0.92, k + 1), at(rim, -h * 0.92, k), at(rim, h * 0.92, k), at(rim, h * 0.92, k + 1), kRim);
    for (int side = -1; side <= 1; side += 2) {
        double x0 = side * h * 0.92, x1 = side * h * 0.70;
        for (int k = 0; k < n; ++k) {
            Vec3 a = at(rim, x0, k), b = at(rim, x0, k + 1), c = at(R * 0.2, x1, k + 1), d = at(R * 0.2, x1, k);
            quad2(m, a, b, c, d, kRim);
        }
        m.prism(0, {x1, 0, 0}, R * 0.2, side > 0 ? 0 : -0.01, side > 0 ? 0.01 : 0, kMid, 8);
        for (int bolt = 0; bolt < 5; ++bolt) {
            double a = 2 * kPi * bolt / 5;
            m.prism(0, {x1 + side * 0.006, R * 0.12 * std::cos(a), R * 0.12 * std::sin(a)}, R * 0.025, -0.006, 0.006, kBolt, 6);
        }
    }
    return m;
}

}  // namespace avogear
