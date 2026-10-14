#pragma once
// Blow-off valve and wastegate meshes, built from simple shapes at start-up (the file header is copied from the stock
// impeller mesh, like the turbos'). They use the stock parts' look: eight-sided shapes, a painted body, dark port
// frames with light bolt heads. Paint slots (the game's colour markers, see kPaint0/kPaint1 in turbo_mesh.h): the
// body takes the part's first colour and the cap its second, so the paint tool colours them separately.
//
//   blow-off valve, small: 1 cell, small pipe; large: 2 x 2 x 2 cells, large pipe. A short pipe along z (inlet -z
//     from the turbo, outlet +z to the air manifold, a blue ring on the outlet) with the valve on top: neck, clamp
//     band, body, cap with bolts, and a flared vent trumpet on the side.
//   wastegate, small: 1 cell, small pipe; large: 2 x 2 x 2 cells, large pipe. Inlet from below (the exhaust
//     manifold), outlet out the +x side, the actuator can on top with its cap, bolts and a boost nipple.
#include "turbo_mesh.h"

namespace turbo {

struct MeshBuilder {
    std::vector<MeshVertex> verts;
    std::vector<uint32_t> idx;
    static constexpr double kPi = 3.14159265358979;
    static constexpr double kOrigin[3] = {0, 0, 0};

    uint32_t vertex(double x, double y, double z, double nx, double ny, double nz, const uint8_t c[3]) {
        MeshVertex v{};
        v.pos[0] = float(x); v.pos[1] = float(y); v.pos[2] = float(z);
        v.rgba[0] = c[0]; v.rgba[1] = c[1]; v.rgba[2] = c[2]; v.rgba[3] = 255;
        v.normal[0] = float(nx); v.normal[1] = float(ny); v.normal[2] = float(nz);
        verts.push_back(v);
        return uint32_t(verts.size() - 1);
    }
    // The shapes below list a, b, c counter-clockwise seen from outside; the game draws only faces that are clockwise
    // from the viewer (like every stock mesh), so they are stored the other way round. Stored as given, the outside
    // faces were culled and the parts looked see-through.
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(c); idx.push_back(b); }
    // Maps (u, v, w) in the shape's frame (w along the axis) to x, y, z.
    static void place(int axis, const double centre[3], double u, double v, double w, double out[3]) {
        double l[3];
        if (axis == 0) { l[0] = w; l[1] = u; l[2] = v; }
        else if (axis == 1) { l[0] = v; l[1] = w; l[2] = u; }
        else { l[0] = u; l[1] = v; l[2] = w; }
        for (int i = 0; i < 3; ++i) out[i] = centre[i] + l[i];
    }
    // A closed frustum along `axis` from w0 to w1 (relative to centre), radii r0 and r1, outward faces.
    void frustum(int axis, const double centre[3], double r0, double r1, double w0, double w1, const uint8_t c[3], int seg = 24) {
        double slope = (r0 - r1) / (w1 - w0);
        uint32_t ring0 = uint32_t(verts.size());
        for (int i = 0; i <= seg; ++i) {
            double a = 2 * kPi * i / seg, cu = std::cos(a), sv = std::sin(a), p[3], l = std::sqrt(1 + slope * slope);
            double nn[3]; place(axis, kOrigin, cu / l, sv / l, slope / l, nn);
            place(axis, centre, r0 * cu, r0 * sv, w0, p); vertex(p[0], p[1], p[2], nn[0], nn[1], nn[2], c);
            place(axis, centre, r1 * cu, r1 * sv, w1, p); vertex(p[0], p[1], p[2], nn[0], nn[1], nn[2], c);
        }
        for (int i = 0; i < seg; ++i) {
            uint32_t a = ring0 + 2 * i, b = a + 1, d = a + 2, e = a + 3;
            tri(a, d, b); tri(b, d, e);
        }
        disc(axis, centre, r0, w0, false, c, seg);
        disc(axis, centre, r1, w1, true, c, seg);
    }
    void cylinder(int axis, const double centre[3], double r, double w0, double w1, const uint8_t c[3], int seg = 24) { frustum(axis, centre, r, r, w0, w1, c, seg); }
    // frustum() with flat faces (one normal per side) and its first corner turned by `phase` radians.
    void frustum_phase(int axis, const double centre[3], double r0, double r1, double w0, double w1, const uint8_t c[3], int seg, double phase) {
        double slope = (r0 - r1) / (w1 - w0);
        for (int i = 0; i < seg; ++i) {
            double a0 = phase + 2 * kPi * i / seg, a1 = phase + 2 * kPi * (i + 1) / seg, am = (a0 + a1) / 2;
            double l = std::sqrt(1 + slope * slope), nn[3], p[3];
            place(axis, kOrigin, std::cos(am) / l, std::sin(am) / l, slope / l, nn);
            uint32_t q[4];
            const double corner[4][3] = {{r0, a0, w0}, {r0, a1, w0}, {r1, a1, w1}, {r1, a0, w1}};
            for (int k = 0; k < 4; ++k) {
                place(axis, centre, corner[k][0] * std::cos(corner[k][1]), corner[k][0] * std::sin(corner[k][1]), corner[k][2], p);
                q[k] = vertex(p[0], p[1], p[2], nn[0], nn[1], nn[2], c);
            }
            tri(q[0], q[1], q[3]); tri(q[1], q[2], q[3]);
        }
        disc_phase(axis, centre, r0, w0, false, c, seg, phase);
        disc_phase(axis, centre, r1, w1, true, c, seg, phase);
    }
    void disc_phase(int axis, const double centre[3], double r, double w, bool facing_up, const uint8_t c[3], int seg, double phase) {
        double n[3]; place(axis, kOrigin, 0, 0, facing_up ? 1 : -1, n);
        double p[3]; place(axis, centre, 0, 0, w, p);
        uint32_t mid = vertex(p[0], p[1], p[2], n[0], n[1], n[2], c), first = uint32_t(verts.size());
        for (int i = 0; i < seg; ++i) {
            double a = phase + 2 * kPi * i / seg;
            place(axis, centre, r * std::cos(a), r * std::sin(a), w, p);
            vertex(p[0], p[1], p[2], n[0], n[1], n[2], c);
        }
        for (int i = 0; i < seg; ++i) {
            uint32_t a = first + uint32_t(i), d = first + uint32_t((i + 1) % seg);
            if (facing_up) tri(mid, a, d); else tri(mid, d, a);
        }
    }
    // An axis-aligned box from lo to hi.
    void box(const double lo[3], const double hi[3], const uint8_t c[3]) {
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side) {
                double n[3] = {0, 0, 0}; n[axis] = side ? 1 : -1;
                int a = (axis + 1) % 3, d = (axis + 2) % 3;
                double w = side ? hi[axis] : lo[axis];
                uint32_t q[4];
                const double uv[4][2] = {{lo[a], lo[d]}, {hi[a], lo[d]}, {hi[a], hi[d]}, {lo[a], hi[d]}};
                for (int k = 0; k < 4; ++k) {
                    double p[3]; p[axis] = w; p[a] = uv[k][0]; p[d] = uv[k][1];
                    q[k] = vertex(p[0], p[1], p[2], n[0], n[1], n[2], c);
                }
                if (side) { tri(q[0], q[1], q[2]); tri(q[0], q[2], q[3]); }
                else { tri(q[0], q[2], q[1]); tri(q[0], q[3], q[2]); }
            }
    }
    void disc(int axis, const double centre[3], double r, double w, bool facing_up, const uint8_t c[3], int seg) {
        double n[3]; place(axis, kOrigin, 0, 0, facing_up ? 1 : -1, n);
        double p[3]; place(axis, centre, 0, 0, w, p);
        uint32_t mid = vertex(p[0], p[1], p[2], n[0], n[1], n[2], c), first = uint32_t(verts.size());
        for (int i = 0; i <= seg; ++i) {
            double a = 2 * kPi * i / seg;
            place(axis, centre, r * std::cos(a), r * std::sin(a), w, p);
            vertex(p[0], p[1], p[2], n[0], n[1], n[2], c);
        }
        for (int i = 0; i < seg; ++i) {
            if (facing_up) tri(mid, first + i, first + i + 1);
            else tri(mid, first + i + 1, first + i);
        }
    }
    // A flat arrow lying on the plane y = top, pointing +z, from z0 to z1, half width hw.
    void arrow(double x, double top, double z0, double z1, double hw, const uint8_t c[3]) {
        double shaft = z0 + (z1 - z0) * 0.55;
        uint32_t a = vertex(x - hw * 0.45, top, z0, 0, 1, 0, c), b = vertex(x + hw * 0.45, top, z0, 0, 1, 0, c);
        uint32_t d = vertex(x + hw * 0.45, top, shaft, 0, 1, 0, c), e = vertex(x - hw * 0.45, top, shaft, 0, 1, 0, c);
        tri(a, e, d); tri(a, d, b);
        uint32_t f = vertex(x - hw, top, shaft, 0, 1, 0, c), g = vertex(x + hw, top, shaft, 0, 1, 0, c), h = vertex(x, top, z1, 0, 1, 0, c);
        tri(f, h, g);
    }
};


// Paint markers and the stock parts' fixed colours.
static const uint8_t kValvePaint0[3] = {153, 0, 0}, kValvePaint1[3] = {153, 38, 0};
static const uint8_t kValveIron[3] = {51, 51, 51}, kValveMid[3] = {113, 113, 113}, kValveAlloy[3] = {198, 198, 198};
static const uint8_t kValveAirBlue[3] = {40, 110, 210}, kValveHole[3] = {14, 14, 16};

// An eight-sided prism or frustum along `axis` with flats facing the other two axes.
inline void oct(MeshBuilder& b, int axis, const double c[3], double r0, double r1, double w0, double w1, const uint8_t col[3]) {
    const double k = 1.0 / std::cos(MeshBuilder::kPi / 8);   // flat-to-corner: the flats sit at r
    b.frustum_phase(axis, c, r0 * k, r1 * k, w0, w1, col, 8, MeshBuilder::kPi / 8);
}
// A small square bolt head on the face of `axis` (pointing +), centred at p.
inline void bolt(MeshBuilder& b, int axis, const double p[3], double half, const uint8_t col[3]) {
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) { lo[i] = p[i] - half; hi[i] = p[i] + half; }
    lo[axis] = p[axis]; hi[axis] = p[axis] + half * 0.8;
    b.box(lo, hi, col);
}
inline std::vector<uint8_t> encode_valve_mesh(const std::vector<uint8_t>& stock, const MeshLayout& m, const MeshBuilder& b, const char* new_name) {
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : b.verts) for (int j = 0; j < 3; ++j) { bounds[j] = std::fmin(bounds[j], v.pos[j]); bounds[3 + j] = std::fmax(bounds[3 + j], v.pos[j]); }
    std::string name(new_name);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + k); };
    uint32_t u;
    put(stock.data(), 12);
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);
    put(bounds, sizeof bounds);
    u = uint32_t(b.verts.size() * sizeof(MeshVertex)); put(&u, 4); put(b.verts.data(), b.verts.size() * sizeof(MeshVertex));
    u = uint32_t(b.idx.size() * 4); put(&u, 4); put(b.idx.data(), b.idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

// The pipe sizes the game draws for a small and a large gas node.
inline double valve_pipe_radius(bool large) { return large ? 0.046 : 0.019; }

// Looks the valves come in (style argument): 0 classic, 1 finned, 2 bell (BOV) or hyper (wastegate), 3 pancake (BOV)
// or bracket (wastegate), 4 race port (BOV) or billet (wastegate). The mod builds Nate's picks (2026-10-14).
constexpr int kValveStyleCount = 5;
constexpr int kBlowOffStyle = 4, kWastegateStyle = 1;

// `count` thin rings around a body of radius r between w0 and w1, as cooling fins.
inline void fins(MeshBuilder& b, int axis, const double c[3], double r, double w0, double w1, int count, const uint8_t col[3]) {
    double pitch = (w1 - w0) / count, t = pitch * 0.38;
    for (int k = 0; k < count; ++k) {
        double w = w0 + pitch * (k + 0.5);
        oct(b, axis, c, r, r, w - t / 2, w + t / 2, col);
    }
}
// Four (or n) bolt heads in a circle of radius r on the face at height w along axis 1 (y).
inline void bolt_ring(MeshBuilder& b, const double c[3], double r, double w, int n, double half, const uint8_t col[3]) {
    for (int k = 0; k < n; ++k) {
        double a = MeshBuilder::kPi / n + k * 2 * MeshBuilder::kPi / n, p[3] = {c[0] + std::cos(a) * r, c[1] + w, c[2] + std::sin(a) * r};
        bolt(b, 1, p, half, col);
    }
}

// Builds the small (large = false) or large blow-off valve. `stock` is any version-5 game mesh; only its header and
// trailer are reused. Returns an empty vector if the stock mesh is not readable.
inline std::vector<uint8_t> build_blow_off_mesh(const std::vector<uint8_t>& stock, bool large, const char* new_name, int style = 0) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double u = large ? kCell : kCell / 2;                  // half the part's size
    const double c0 = large ? kCell / 2 : 0;                     // centre of the part
    const double rp = valve_pipe_radius(large), rb = large ? 0.044 : 0.02;
    MeshBuilder b;
    const double axis[3] = {c0, c0, c0};
    // the pipe stub and its two port frames
    oct(b, 2, axis, rp, rp, -u + 0.1 * u, u - 0.1 * u, kValveMid);
    oct(b, 2, axis, rp * 1.32, rp * 1.32, -u, -u + 0.14 * u, kValveIron);
    oct(b, 2, axis, rp * 1.32, rp * 1.32, u - 0.14 * u, u, kValveIron);
    oct(b, 2, axis, rp * 1.4, rp * 1.4, u - 0.32 * u, u - 0.2 * u, kValveAirBlue);   // outlet: to the air manifold
    oct(b, 2, axis, rp * 0.72, rp * 0.72, -u - 0.002, -u + 0.001, kValveHole);
    oct(b, 2, axis, rp * 0.72, rp * 0.72, u - 0.001, u + 0.002, kValveHole);
    // the valve on top
    const double top = rp * 0.85;                                // where the neck leaves the pipe (relative to c0)
    const double hu = large ? 0.75 * u : u;                      // the large pipe leaves less room above it
    const double neck1 = rp + 0.08 * hu, band1 = neck1 + 0.08 * hu;
    oct(b, 1, axis, rb * 0.62, rb * 0.62, top, neck1, kValveMid);
    switch (style) {
    default: {                                                   // classic: body, cap with screws, side trumpet
        const double body1 = band1 + 0.32 * hu, cap1 = body1 + 0.18 * hu;
        oct(b, 1, axis, rb * 1.1, rb * 1.1, neck1, band1, kValveAlloy);
        oct(b, 1, axis, rb, rb, band1, body1, kValvePaint0);
        oct(b, 1, axis, rb * 1.06, rb * 0.78, body1, cap1, kValvePaint1);
        bolt_ring(b, axis, rb * 0.5, cap1, 4, rb * 0.13, kValveAlloy);
        const double horn_at[3] = {c0, c0 + (band1 + body1) / 2, c0};
        oct(b, 0, horn_at, rb * 0.42, rb * 0.42, rb * 0.9, rb * 1.25, kValveAlloy);
        oct(b, 0, horn_at, rb * 0.48, rb * 0.78, rb * 1.25, rb * 1.75, kValveAlloy);
        oct(b, 0, horn_at, rb * 0.6, rb * 0.6, rb * 1.75, rb * 1.76, kValveHole);
        break;
    }
    case 1: {                                                    // finned: tall finned body, vent straight up
        const double body1 = band1 + 0.42 * hu, cap1 = body1 + 0.1 * hu, vent1 = cap1 + 0.1 * hu;
        oct(b, 1, axis, rb * 1.1, rb * 1.1, neck1, band1, kValveAlloy);
        oct(b, 1, axis, rb * 0.86, rb * 0.86, band1, body1, kValvePaint0);
        fins(b, 1, axis, rb * 1.12, band1, body1, 4, kValvePaint0);
        oct(b, 1, axis, rb * 1.02, rb * 0.9, body1, cap1, kValvePaint1);
        oct(b, 1, axis, rb * 0.5, rb * 0.62, cap1, vent1, kValveAlloy);
        oct(b, 1, axis, rb * 0.46, rb * 0.46, vent1 - 0.001, vent1 + 0.001, kValveHole);
        break;
    }
    case 2: {                                                    // bell: short body, big flared bell out the side
        const double body1 = band1 + 0.26 * hu, cap1 = body1 + 0.14 * hu;
        oct(b, 1, axis, rb * 1.1, rb * 1.1, neck1, band1, kValveAlloy);
        oct(b, 1, axis, rb * 0.95, rb * 0.95, band1, body1, kValvePaint0);
        oct(b, 1, axis, rb * 1.0, rb * 0.6, body1, cap1, kValvePaint1);
        const double bell_at[3] = {c0, c0 + (band1 + body1) / 2, c0};
        oct(b, 0, bell_at, rb * 0.4, rb * 0.4, rb * 0.85, rb * 1.1, kValvePaint1);
        oct(b, 0, bell_at, rb * 0.4, rb * 0.84, rb * 1.1, rb * 1.72, kValveAlloy);   // within the part: the large one is tight
        oct(b, 0, bell_at, rb * 0.88, rb * 0.88, rb * 1.72, rb * 1.8, kValveAlloy);
        oct(b, 0, bell_at, rb * 0.76, rb * 0.76, rb * 1.8, rb * 1.81, kValveHole);
        break;
    }
    case 3: {                                                    // pancake: wide flat diaphragm, two-tone, rim bolts
        const double low1 = band1 + 0.16 * hu, high1 = low1 + 0.16 * hu, dome1 = high1 + 0.08 * hu;
        oct(b, 1, axis, rb * 0.62, rb * 0.62, neck1, band1, kValveMid);
        oct(b, 1, axis, rb * 1.5, rb * 1.5, band1, low1, kValvePaint0);
        oct(b, 1, axis, rb * 1.58, rb * 1.58, low1 - 0.02 * hu, low1 + 0.02 * hu, kValveAlloy);
        oct(b, 1, axis, rb * 1.5, rb * 1.3, low1, high1, kValvePaint1);
        oct(b, 1, axis, rb * 0.7, rb * 0.5, high1, dome1, kValvePaint1);
        bolt_ring(b, axis, rb * 1.36, low1 + 0.02 * hu, 8, rb * 0.09, kValveAlloy);
        const double vent_at[3] = {c0, c0 + band1 + 0.08 * hu, c0};
        oct(b, 0, vent_at, rb * 0.3, rb * 0.3, rb * 1.4, rb * 1.8, kValveAlloy);
        oct(b, 0, vent_at, rb * 0.22, rb * 0.22, rb * 1.8, rb * 1.81, kValveHole);
        break;
    }
    case 4: {                                                    // race port: slim tall body, slotted vent crown
        const double body1 = band1 + 0.4 * hu, crown1 = body1 + 0.12 * hu, top1 = crown1 + 0.06 * hu;
        oct(b, 1, axis, rb * 1.0, rb * 1.0, neck1, band1, kValveAlloy);
        oct(b, 1, axis, rb * 0.8, rb * 0.8, band1, body1, kValvePaint0);
        oct(b, 1, axis, rb * 0.86, rb * 0.86, body1 - 0.04 * hu, body1, kValveAlloy);
        oct(b, 1, axis, rb * 0.78, rb * 0.78, body1, crown1, kValvePaint1);
        for (int k = 0; k < 4; ++k) {                            // dark vent slots round the crown
            double a = k * MeshBuilder::kPi / 2, cx = std::cos(a), cz = std::sin(a), r = rb * 0.79, w = rb * 0.2;
            double lo[3] = {c0 + cx * r - (cz != 0 ? w : 0.002), c0 + body1 + 0.025 * hu, c0 + cz * r - (cx != 0 ? w : 0.002)};
            double hi[3] = {c0 + cx * r + (cz != 0 ? w : 0.002), c0 + crown1 - 0.025 * hu, c0 + cz * r + (cx != 0 ? w : 0.002)};
            b.box(lo, hi, kValveHole);
        }
        oct(b, 1, axis, rb * 0.82, rb * 0.6, crown1, top1, kValveAlloy);
        break;
    }
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

// Builds the small or large wastegate: inlet from below, outlet out the +x side, actuator on top.
inline std::vector<uint8_t> build_wastegate_mesh(const std::vector<uint8_t>& stock, bool large, const char* new_name, int style = 0) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    const double u = large ? kCell : kCell / 2, c0 = large ? kCell / 2 : 0;
    const double rp = valve_pipe_radius(large), ra = large ? 0.054 : 0.026;
    const double centre[3] = {c0, c0, c0};
    MeshBuilder b;
    // inlet: frame under the part and the valve housing above it
    oct(b, 1, centre, rp * 1.32, rp * 1.32, -u, -u + 0.14 * u, kValveIron);
    oct(b, 1, centre, rp * 0.72, rp * 0.72, -u - 0.002, -u + 0.001, kValveHole);
    oct(b, 1, centre, rp, rp, -u + 0.14 * u, -0.1 * u, kValveMid);
    oct(b, 1, centre, rp * 1.18, rp * 1.18, -0.1 * u, 0.28 * u, kValveMid);
    // outlet out the +x side
    oct(b, 0, centre, rp, rp, 0, u - 0.14 * u, kValveMid);
    oct(b, 0, centre, rp * 1.32, rp * 1.32, u - 0.14 * u, u, kValveIron);
    oct(b, 0, centre, rp * 0.72, rp * 0.72, u - 0.001, u + 0.002, kValveHole);
    switch (style) {
    default: {                                                   // classic: band, can, cap with bolts and a nipple
        const double band0 = 0.28 * u, band1 = 0.38 * u, can1 = 0.72 * u, cap1 = 0.9 * u;
        oct(b, 1, centre, ra * 1.06, ra * 1.06, band0, band1, kValveAlloy);
        oct(b, 1, centre, ra, ra, band1, can1, kValvePaint0);
        oct(b, 1, centre, ra * 1.04, ra * 0.8, can1, cap1, kValvePaint1);
        bolt_ring(b, centre, ra * 0.55, cap1, 4, ra * 0.11, kValveAlloy);
        const double nipple[3] = {c0, c0 + cap1, c0};
        oct(b, 1, nipple, ra * 0.16, ra * 0.12, 0, 0.1 * u, kValveAlloy);
        break;
    }
    case 1: {                                                    // finned can, two nipples
        const double band1 = 0.36 * u, can1 = 0.78 * u, cap1 = 0.9 * u;
        oct(b, 1, centre, ra * 1.0, ra * 1.0, 0.28 * u, band1, kValveAlloy);
        oct(b, 1, centre, ra * 0.82, ra * 0.82, band1, can1, kValvePaint0);
        fins(b, 1, centre, ra * 1.04, band1, can1, 5, kValvePaint0);
        oct(b, 1, centre, ra * 0.98, ra * 0.86, can1, cap1, kValvePaint1);
        for (int k = 0; k < 2; ++k) {
            const double nipple[3] = {c0 + (k ? 0.4 : -0.4) * ra, c0 + cap1, c0};
            oct(b, 1, nipple, ra * 0.13, ra * 0.1, 0, 0.08 * u, kValveAlloy);
        }
        break;
    }
    case 2: {                                                    // hyper: wide flat diaphragm, two-tone, rim bolts
        const double neck1 = 0.4 * u, low1 = 0.6 * u, high1 = 0.78 * u, dome1 = 0.88 * u;
        oct(b, 1, centre, ra * 0.7, ra * 0.7, 0.28 * u, neck1, kValveAlloy);
        oct(b, 1, centre, ra * 1.3, ra * 1.3, neck1, low1, kValvePaint0);
        oct(b, 1, centre, ra * 1.38, ra * 1.38, low1 - 0.025 * u, low1 + 0.025 * u, kValveAlloy);
        oct(b, 1, centre, ra * 1.3, ra * 1.12, low1, high1, kValvePaint1);
        oct(b, 1, centre, ra * 0.62, ra * 0.46, high1, dome1, kValvePaint1);
        bolt_ring(b, centre, ra * 1.2, low1 + 0.025 * u, 8, ra * 0.08, kValveAlloy);
        break;
    }
    case 3: {                                                    // bracket: OEM can lying on a plate, rod to the gate
        const double plate0 = 0.28 * u, plate1 = 0.36 * u, rc = ra * 0.6;
        double lo[3] = {c0 - ra * 0.9, c0 + plate0, c0 - u * 0.85}, hi[3] = {c0 + ra * 0.9, c0 + plate1, c0 + u * 0.3};
        b.box(lo, hi, kValveIron);
        const double can_at[3] = {c0, c0 + plate1 + rc, c0};
        oct(b, 2, can_at, rc, rc, -u * 0.82, -u * 0.05, kValvePaint0);
        oct(b, 2, can_at, rc * 1.06, rc * 0.8, -u * 0.05, u * 0.12, kValvePaint1);
        oct(b, 2, can_at, rc * 1.08, rc * 1.08, -u * 0.86, -u * 0.8, kValveAlloy);
        oct(b, 2, can_at, rc * 0.14, rc * 0.14, u * 0.12, u * 0.62, kValveAlloy);   // the rod
        const double nipple[3] = {c0, c0 + plate1 + 2 * rc, c0 - u * 0.45};
        oct(b, 1, nipple, ra * 0.13, ra * 0.1, 0, 0.08 * u, kValveAlloy);
        break;
    }
    case 4: {                                                    // billet: square machined blocks, corner bolts
        const double s = ra * 0.92, body0 = 0.3 * u, body1 = 0.66 * u, cap1 = 0.84 * u;
        double lo[3] = {c0 - s, c0 + body0, c0 - s}, hi[3] = {c0 + s, c0 + body1, c0 + s};
        b.box(lo, hi, kValvePaint0);
        double clo[3] = {c0 - s * 1.05, c0 + body1, c0 - s * 1.05}, chi[3] = {c0 + s * 1.05, c0 + cap1, c0 + s * 1.05};
        b.box(clo, chi, kValvePaint1);
        double llo[3] = {c0 - s * 1.07, c0 + body1 - 0.03 * u, c0 - s * 1.07}, lhi[3] = {c0 + s * 1.07, c0 + body1 + 0.01 * u, c0 + s * 1.07};
        b.box(llo, lhi, kValveAlloy);
        for (int k = 0; k < 4; ++k) {
            double p[3] = {c0 + (k & 1 ? 0.72 : -0.72) * s, c0 + cap1, c0 + (k & 2 ? 0.72 : -0.72) * s};
            bolt(b, 1, p, ra * 0.1, kValveAlloy);
        }
        const double nipple[3] = {c0, c0 + cap1, c0};
        oct(b, 1, nipple, ra * 0.16, ra * 0.12, 0, 0.1 * u, kValveAlloy);
        break;
    }
    }
    return encode_valve_mesh(stock, m, b, new_name);
}

}  // namespace turbo
