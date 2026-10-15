#pragma once
// Turbo part meshes, built at start-up from the game's own small impeller mesh (rom/meshes/components/turbo_a.mesh),
// so no game asset ships with the mod.
//
// turbo_a.mesh is the 1 x 3 x 3 cell impeller: x from -0.04 to 0.05 m (the axial intake faces -x, the hub +x),
// y and z from -0.12 to 0.12 m, the outlet stub on the top (+y) edge leaving +z. One cell is 0.08 m.
// A turbo of size N (N x N face, 2 cells thick) is:
//   cold side, cell x = 0: the impeller scaled by N/3 across its face, recentred on the N x N face (paint 1);
//   hot side,  cell x = 1: the same impeller mirrored front to back, turned half a turn about its axis (outlet
//                          stub at the bottom leaving -z: the turbine inlet) (paint 2).
// Mesh file layout (Anymaker 0.1.24; the same reader as GpsPart, checked by parse):
//   "mesh" u32 version(5) u32 (1) u32 name_length, name bytes
//   32-byte attribute header, 56 bytes (zero), 6 x f64 bounds (min xyz, max xyz)
//   u32 vertex_bytes, vertices of 36 bytes: f32 position[3], u8 rgba[4], f32 uv[2], f32 normal[3]
//   u32 index_bytes, u32 indices (triangle list), 8-byte trailer
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "turbo_logic.h"

namespace turbo {

constexpr double kCell = 0.08;

struct MeshVertex { float pos[3]; uint8_t rgba[4]; float uv[2]; float normal[3]; };
static_assert(sizeof(MeshVertex) == 36, "mesh vertex");

struct MeshLayout { size_t bounds = 0, vertex_bytes = 0, index_bytes = 0, trailer = 0; uint32_t vertices = 0, indices = 0; };

inline bool read_u32(const std::vector<uint8_t>& b, size_t at, uint32_t& out) {
    if (at + 4 > b.size()) return false;
    std::memcpy(&out, b.data() + at, 4); return true;
}
inline bool parse_mesh(const std::vector<uint8_t>& b, MeshLayout& m) {
    uint32_t version = 0, name = 0, vbytes = 0, ibytes = 0;
    if (b.size() < 16 || std::memcmp(b.data(), "mesh", 4) != 0 || !read_u32(b, 4, version) || version != 5 || !read_u32(b, 12, name) || name > 256) return false;
    m.bounds = 16 + size_t(name) + 32 + 56;
    m.vertex_bytes = m.bounds + 48;
    if (!read_u32(b, m.vertex_bytes, vbytes) || vbytes % sizeof(MeshVertex) || vbytes == 0) return false;
    m.index_bytes = m.vertex_bytes + 4 + vbytes;
    if (!read_u32(b, m.index_bytes, ibytes) || ibytes % 12) return false;
    m.trailer = m.index_bytes + 4 + ibytes;
    if (m.trailer + 8 != b.size()) return false;
    m.vertices = vbytes / sizeof(MeshVertex); m.indices = ibytes / 4;
    for (uint32_t i = 0; i < m.indices; ++i) { uint32_t v = 0; read_u32(b, m.index_bytes + 4 + size_t(i) * 4, v); if (v >= m.vertices) return false; }
    return true;
}

// Paint: the stock impeller's red is the game's first paint marker (153, 0, 0): a part's mesh is drawn in the part's
// own colours wherever it carries one of these markers, and the paint tool sets each one separately. The compressor
// housing keeps the first marker and the turbine housing takes the second (153, 38, 0), so the cold and hot sides are
// painted independently. A new turbo starts steel on the cold side and cast iron on the hot side (turbo_mod.cpp).
constexpr uint8_t kPaint0[3] = {153, 0, 0}, kPaint1[3] = {153, 38, 0};
inline bool is_paint0(const uint8_t rgba[4]) { return rgba[0] > 120 && rgba[1] < 30 && rgba[2] < 30; }
inline void cold_paint(uint8_t rgba[4]) {
    if (is_paint0(rgba)) { rgba[0] = kPaint0[0]; rgba[1] = kPaint0[1]; rgba[2] = kPaint0[2]; }
}
inline void hot_paint(uint8_t rgba[4]) {
    if (is_paint0(rgba)) { rgba[0] = kPaint1[0]; rgba[1] = kPaint1[1]; rgba[2] = kPaint1[2]; }
}

// Simple shapes for the mod's own meshes (valves, filters, intercoolers) and the turbos' port openings and oil
// fittings.
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
// Where the pipe end of a turbo's gas node sits: the centre of its face (a large pipe's 2 x 2 face), in mesh metres.
inline void port_face_centre(int n, Node node, double out[3]) {
    Port p = port(n, node);
    int span = spec(n).large_pipe ? 2 : 1, axis = p.dir / 2;
    out[0] = p.x * kCell; out[1] = p.y * kCell; out[2] = p.z * kCell;
    for (int i = 0; i < 3; ++i)
        if (i != axis) out[i] += (span - 1) * kCell / 2;
    out[axis] += (p.dir % 2 ? 0.5 : -0.5) * kCell;
}
inline double turbo_pipe_radius(int n) { return spec(n).large_pipe ? 0.046 : 0.019; }   // what the game draws (valve_pipe_radius)
static const uint8_t kOilBrass[3] = {196, 152, 58};   // the oil ports' ring, so they read apart from the gas ports

// A port opening on a face: a dark frame and the hole, a hair proud of the face so they draw over it. `axis` and
// `sign` give the way the face looks out.
inline void port_opening(MeshBuilder& b, int axis, int sign, const double c[3], double hole, double frame) {
    const double out = sign > 0 ? 1.0 : -1.0, corner = hole / std::cos(MeshBuilder::kPi / 8);
    oct(b, axis, c, frame, frame, std::fmin(-0.004 * out, 0.0004 * out), std::fmax(-0.004 * out, 0.0004 * out), kValveIron);
    b.disc_phase(axis, c, corner, 0.0008 * out, sign > 0, kValveHole, 8, MeshBuilder::kPi / 8);
}
// An oil port under the turbo: a dark pad with two bolts, a brass ring and the hole, on the bottom face of its cell.
// The pad is a boss rising into the housing, so it meets it where the housing's corner is cut away (the 2x2).
inline void oil_fitting(MeshBuilder& b, const double c[3]) {
    const double y = c[1];                                    // the bottom face (cell centre - half a cell)
    const double lo[3] = {c[0] - 0.026, y - 0.0003, c[2] - 0.026}, hi[3] = {c[0] + 0.026, y + 0.045, c[2] + 0.026};
    b.box(lo, hi, kValveIron);
    for (int k = 0; k < 2; ++k) {                             // bolt heads on the pad's diagonal
        double d = k ? 0.019 : -0.019, bl[3] = {c[0] + d - 0.004, y - 0.0012, c[2] - d - 0.004}, bh[3] = {c[0] + d + 0.004, y, c[2] - d + 0.004};
        b.box(bl, bh, kValveAlloy);
    }
    oct(b, 1, c, 0.016, 0.016, -0.0011, 0.0, kOilBrass);
    b.disc_phase(1, c, 0.011 / std::cos(MeshBuilder::kPi / 8), -0.0013, false, kValveHole, 8, MeshBuilder::kPi / 8);
}

// Builds the size-n turbo mesh from the stock impeller. Returns an empty vector when the input layout is not
// recognised or is not the 3 x 3 impeller.
//
// Every opening lines up with the pipe end the game draws on that node (port() in turbo_logic.h):
// - The axial intake and exhaust (the impeller's eye) sit at the face's centre, where the pipe is on the 3x3 and 4x4.
//   On the 5x5 a large pipe cannot be centred (it is 2 cells on a 5-cell face), so the eye moves half a cell up and
//   back with it, a little smaller so it stays inside the housing's face ring. On the 2x2 a small pipe is a quarter of
//   the face, away from the centre: the eye is filled in flat and a new opening drawn at the pipe.
// - The 4x4 and 5x5 take a large pipe on the boost outlet and the turbine inlet, which spans both of the part's cells
//   along x; a cast flange block there carries the opening across both (the stock housings' own outlets are one
//   cell wide and are painted over).
// - Oil ports get a pad, a brass ring and a hole under the part, at the oil nodes.
inline std::vector<uint8_t> build_turbo_mesh(const std::vector<uint8_t>& stock, int n, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m) || n < 2 || n > 5) return {};
    std::vector<MeshVertex> src(m.vertices);
    std::memcpy(src.data(), stock.data() + m.vertex_bytes + 4, m.vertices * sizeof(MeshVertex));
    std::vector<uint32_t> idx(m.indices);
    std::memcpy(idx.data(), stock.data() + m.index_bytes + 4, m.indices * 4);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (auto& v : src) for (int j = 0; j < 3; ++j) { lo[j] = v.pos[j] < lo[j] ? v.pos[j] : lo[j]; hi[j] = v.pos[j] > hi[j] ? v.pos[j] : hi[j]; }
    // Expect the 3 x 3 impeller (face 0.24 m square, centred, about one cell thick).
    if (hi[1] - lo[1] < 0.2f || hi[1] - lo[1] > 0.28f || hi[2] - lo[2] < 0.2f || hi[2] - lo[2] > 0.28f || hi[0] - lo[0] > 0.12f) return {};

    const float s = float(n) / 3.0f;                    // face scale
    const float c = float((n - 1) * kCell * 0.5);       // centre of the N x N face, cells 0..n-1
    const bool large = spec(n).large_pipe;
    // The eye: the stock impeller's intake, everything within its face ring (0.06 m from the axis, an octagon, so the
    // larger of |y| and |z|). The next ring of the face is at 0.0731 m.
    constexpr float kEye = 0.0601f, kFaceRing = 0.0731f;
    double eye_at[3];
    port_face_centre(n, kColdIn, eye_at);
    const float shift = float(eye_at[1]) - c;            // the same along z: the axial ports sit on the diagonal
    float eye_scale = 1.0f;
    bool fill_eye = false;
    if (std::fabs(shift) > 1e-6f) {
        eye_scale = (kFaceRing * s - 0.002f - std::fabs(shift)) / (kEye * s);
        fill_eye = eye_scale < 0.6f;                    // no room to move it: fill it in and draw a new one
    }
    std::vector<MeshVertex> verts;
    verts.reserve(src.size() * 2 + 1500);
    std::vector<uint32_t> out_idx;
    out_idx.reserve(idx.size() * 2 + 2500);
    // Normals scale by the inverse of the position scale, then renormalise.
    auto normal = [&](const float in[3], float sx, float sy, float sz, float out[3]) {
        float x = in[0] / sx, y = in[1] / sy, z = in[2] / sz, l = std::sqrt(x * x + y * y + z * z);
        if (l < 1e-6f) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return; }
        out[0] = x / l; out[1] = y / l; out[2] = z / l;
    };
    for (int side = 0; side < 2; ++side) {
        uint32_t base = uint32_t(verts.size());
        for (const auto& v : src) {
            MeshVertex o = v;
            float x = v.pos[0], y = v.pos[1], z = v.pos[2];
            float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
            const bool eye = std::fmax(std::fabs(y), std::fabs(z)) <= kEye;
            const bool outlet = z > 0.1f && y > 0.045f;   // the stock outlet stub's end, around its opening
            if (fill_eye && eye && x < 0) {               // flat over the eye, in the housing's paint
                x = lo[0]; nx = -1; ny = 0; nz = 0;
                o.rgba[0] = kPaint0[0]; o.rgba[1] = kPaint0[1]; o.rgba[2] = kPaint0[2];
            }
            if (large && outlet && (o.rgba[0] < 60 && o.rgba[1] < 60)) {   // under the flange block: paint the old opening over
                o.rgba[0] = kPaint0[0]; o.rgba[1] = kPaint0[1]; o.rgba[2] = kPaint0[2];
            }
            if (side) {                                 // mirror front to back, then half a turn about the axis
                x = float(kCell) - x; y = -y; z = -z;
                nx = -nx; ny = -ny; nz = -nz;
                hot_paint(o.rgba);
            } else {
                cold_paint(o.rgba);
            }
            const float k = !fill_eye && eye ? s * eye_scale : s, d = !fill_eye && eye ? shift : 0.0f;
            o.pos[0] = x; o.pos[1] = y * k + c + d; o.pos[2] = z * k + c + d;
            const float nin[3] = {nx, ny, nz};
            normal(nin, 1.0f, s, s, o.normal);
            verts.push_back(o);
        }
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            // The mirror flips handedness; the half turn does not. Keep faces outward on the hot side.
            if (side) { out_idx.push_back(base + idx[t]); out_idx.push_back(base + idx[t + 2]); out_idx.push_back(base + idx[t + 1]); }
            else { out_idx.push_back(base + idx[t]); out_idx.push_back(base + idx[t + 1]); out_idx.push_back(base + idx[t + 2]); }
        }
    }
    // The mod's own pieces: new openings where the stock ones do not meet the pipe, and the oil fittings.
    MeshBuilder b;
    const double rp = turbo_pipe_radius(n), hole = rp + 0.002, frame = rp + (large ? 0.007 : 0.0055);
    if (fill_eye) {
        double at[3];
        port_face_centre(n, kColdIn, at); port_opening(b, 0, -1, at, hole, frame);
        port_face_centre(n, kHotOut, at); port_opening(b, 0, 1, at, hole, frame);
    }
    if (large) {
        // Just proud of the part's front and back faces, so the stock outlet stub under it never shows through.
        const double x0 = -0.5 * kCell - 0.0005, x1 = 1.5 * kCell + 0.0005, reach = 0.065, half = rp + 0.014;
        double at[3];
        port_face_centre(n, kColdOut, at);             // boost outlet: top far corner, looking +z
        const double top = (n - 0.5) * kCell;
        const double ol[3] = {x0, at[1] - half, at[2] - reach}, oh[3] = {x1, top - 0.003, at[2]};
        b.box(ol, oh, kPaint0);
        port_opening(b, 2, 1, at, hole, frame);
        port_face_centre(n, kHotIn, at);               // turbine inlet: bottom near corner, looking -z
        const double il[3] = {x0, -0.5 * kCell + 0.003, at[2]}, ih[3] = {x1, at[1] + half, at[2] + reach};
        b.box(il, ih, kPaint1);
        port_opening(b, 2, -1, at, hole, frame);
    }
    for (bool in : {true, false}) {
        Port o = oil_port(n, in);
        const double at[3] = {o.x * kCell, (o.y - 0.5) * kCell, o.z * kCell};
        oil_fitting(b, at);
    }
    const uint32_t first = uint32_t(verts.size());
    verts.insert(verts.end(), b.verts.begin(), b.verts.end());
    for (uint32_t i : b.idx) out_idx.push_back(first + i);

    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : verts) for (int j = 0; j < 3; ++j) { bounds[j] = v.pos[j] < bounds[j] ? v.pos[j] : bounds[j]; bounds[3 + j] = v.pos[j] > bounds[3 + j] ? v.pos[j] : bounds[3 + j]; }

    std::string name(new_name);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + k); };
    uint32_t u;
    put(stock.data(), 12);                                   // magic, version, (1)
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);                   // attribute header + reserved bytes
    put(bounds, sizeof bounds);
    u = uint32_t(verts.size() * sizeof(MeshVertex)); put(&u, 4); put(verts.data(), verts.size() * sizeof(MeshVertex));
    u = uint32_t(out_idx.size() * 4); put(&u, 4); put(out_idx.data(), out_idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

}  // namespace turbo
