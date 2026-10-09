#pragma once
// AnyMirror plane math. Pure and platform-free so it is unit tested without the game.
// Positions are vehicle grid coordinates (vec3_s32), the same values the edge tool sends to the server.
#include <cstdint>

namespace anymirror {
struct Grid { int32_t v[3]; };
inline bool operator==(const Grid& a, const Grid& b) { return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2]; }

// The mirror plane is perpendicular to `axis` (0 = X, 1 = Y, 2 = Z) at grid coordinate twice / 2, so it can
// sit on a grid line (even) or halfway between two (odd).
struct Plane { int axis = 0; int32_t twice = 0; };

constexpr int32_t kLimit = 1 << 24;   // far beyond any buildable vehicle; keeps every sum inside int32

inline bool in_range(const Grid& p) {
    for (int32_t c : p.v) if (c <= -kLimit || c >= kLimit) return false;
    return true;
}
inline Grid mirror(Grid p, const Plane& plane) {
    p.v[plane.axis] = plane.twice - p.v[plane.axis];
    return p;
}
inline bool same_edge(const Grid& a0, const Grid& a1, const Grid& b0, const Grid& b1) {
    return (a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0);
}
// The mirrored twin of edge p0-p1. False when the edge is its own mirror image (it lies in the plane or
// crosses it symmetrically) or a coordinate is out of range; then nothing extra should be placed.
inline bool mirrored_edge(const Grid& p0, const Grid& p1, const Plane& plane, Grid& m0, Grid& m1) {
    if (plane.axis < 0 || plane.axis > 2 || plane.twice <= -kLimit || plane.twice >= kLimit || !in_range(p0) || !in_range(p1)) return false;
    m0 = mirror(p0, plane);
    m1 = mirror(p1, plane);
    return !same_edge(p0, p1, m0, m1);
}
// A plane through the middle of the given bounds on `axis`.
inline Plane centred(int axis, const Grid& lo, const Grid& hi) {
    Plane p; p.axis = axis < 0 || axis > 2 ? 0 : axis;
    int64_t twice = int64_t(lo.v[p.axis]) + int64_t(hi.v[p.axis]);
    p.twice = twice <= -kLimit ? -kLimit + 1 : twice >= kLimit ? kLimit - 1 : int32_t(twice);
    return p;
}
inline Plane nudged(Plane p, int32_t half_steps) {
    int64_t twice = int64_t(p.twice) + half_steps;
    p.twice = twice <= -kLimit ? -kLimit + 1 : twice >= kLimit ? kLimit - 1 : int32_t(twice);
    return p;
}

struct Box { double min[3], max[3]; };
// The visible wall in vehicle-local metres: a thin slab on the plane covering the build's node bounds plus a
// margin of `margin` grid steps on the other two axes.
inline Box wall(const Plane& plane, const Grid& lo, const Grid& hi, double grid_size, int32_t margin, double thickness) {
    Box b{};
    for (int a = 0; a < 3; ++a) {
        if (a == plane.axis) {
            double centre = plane.twice * 0.5 * grid_size;
            b.min[a] = centre - thickness * 0.5;
            b.max[a] = centre + thickness * 0.5;
        } else {
            b.min[a] = (double(lo.v[a]) - margin) * grid_size;
            b.max[a] = (double(hi.v[a]) + margin) * grid_size;
        }
    }
    return b;
}
}  // namespace anymirror
