#pragma once
// AnyLaser part meshes: the Laser Block and the Laser Sensor in the three looks shown to Nate (2026-11-08), built
// from simple shapes at start-up. The file header (format version, attribute layout, trailer) is copied from the
// game's own compass_sensor_a.mesh, so no game asset ships with the mod (as GpsPart and Turbo do).
//
// Mesh file layout (gpspart_mesh.h): "mesh" u32 version(5) u32 (1) u32 name_length, name, 32-byte attribute header,
// 56 zero bytes, 6 x f64 bounds, u32 vertex_bytes + 36-byte vertices (f32 pos[3], u8 rgba[4], f32 uv[2], f32 n[3]),
// u32 index_bytes + u32 indices, 8-byte trailer.
//
// Both parts fill one cell (-0.04..0.04 on every axis) and face +Z: the lens sits on the +Z face.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace anylaser {

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
    return true;
}

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
    // Shapes list a, b, c counter-clockwise seen from outside; the game draws faces clockwise from the viewer, so
    // they are stored the other way round (turbo_mesh.h, anybattery_mesh.h).
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(c); idx.push_back(b); }
    // (u, v, w) in a shape's frame, w along `axis`, to x, y, z.
    static void place(int axis, const double centre[3], double u, double v, double w, double out[3]) {
        double l[3];
        if (axis == 0) { l[0] = w; l[1] = u; l[2] = v; }
        else if (axis == 1) { l[0] = v; l[1] = w; l[2] = u; }
        else { l[0] = u; l[1] = v; l[2] = w; }
        for (int i = 0; i < 3; ++i) out[i] = centre[i] + l[i];
    }
    void disc(int axis, const double centre[3], double r, double w, bool facing_up, const uint8_t c[3], int seg = 24) {
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
    // A closed frustum along `axis` from w0 to w1 (relative to centre), radii r0 and r1, outward faces.
    void frustum(int axis, const double centre[3], double r0, double r1, double w0, double w1, const uint8_t c[3], int seg = 24, bool caps = true) {
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
        if (!caps) return;
        disc(axis, centre, r0, w0, false, c, seg);
        disc(axis, centre, r1, w1, true, c, seg);
    }
    void cylinder(int axis, const double centre[3], double r, double w0, double w1, const uint8_t c[3], int seg = 24) { frustum(axis, centre, r, r, w0, w1, c, seg); }
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
    void box(double x0, double y0, double z0, double x1, double y1, double z1, const uint8_t c[3]) {
        const double lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
        box(lo, hi, c);
    }
};

// Colours. Plain colours, not the paint tool's slot markers, so the lens and the beam colour always match.
namespace colour {
inline constexpr uint8_t black[3] = {34, 36, 40}, plate[3] = {72, 74, 78}, barrel[3] = {26, 26, 28};
inline constexpr uint8_t red_lens[3] = {255, 46, 46}, red_ring[3] = {165, 32, 32}, target[3] = {14, 14, 16};
inline constexpr uint8_t yellow[3] = {226, 180, 32}, band[3] = {30, 30, 30}, green_lens[3] = {70, 255, 100}, green_dim[3] = {28, 80, 40};
inline constexpr uint8_t steel[3] = {190, 195, 200}, steel_dark[3] = {150, 154, 160}, base[3] = {118, 122, 128};
inline constexpr uint8_t blue_lens[3] = {90, 176, 255}, gunmetal[3] = {60, 64, 70};
}  // namespace colour

constexpr double kH = 0.04;   // half a cell

// look 0 tripwire, 1 industrial, 2 sci-fi; laser true for the Laser Block, false for the Laser Sensor.
inline void build_part(MeshBuilder& b, int look, bool laser) {
    using namespace colour;
    const double c[3] = {0, 0, 0};
    if (look == 0) {
        b.box(-kH, -kH, -kH, kH, -kH + 0.006, kH, plate);                         // base plate
        b.box(-0.036, -kH + 0.006, -0.036, 0.036, 0.034, 0.026, black);            // body
        if (laser) {
            b.cylinder(2, c, 0.016, 0.026, 0.038, barrel);                         // lens barrel
            b.disc(2, c, 0.011, 0.0384, true, red_lens);                           // lens
            b.box(-0.016, 0.034, -0.02, -0.006, 0.037, -0.01, red_lens);           // status light on top
        } else {
            b.frustum(2, c, 0.026, 0.030, 0.026, 0.038, red_ring);                 // red receiver ring
            b.disc(2, c, 0.021, 0.0384, true, target);                             // dark target
        }
        return;
    }
    if (look == 1) {
        b.box(-kH, -kH, -kH, kH, -kH + 0.005, kH, band);                          // base
        b.box(-0.036, -kH + 0.005, -0.036, 0.036, 0.034, 0.024, yellow);           // body
        b.box(-0.038, -kH + 0.005, -0.030, 0.038, 0.036, -0.016, band);            // hazard band round the back
        if (laser) {
            b.cylinder(2, c, 0.022, 0.024, 0.038, barrel);
            b.disc(2, c, 0.016, 0.0384, true, green_lens);
        } else {
            b.cylinder(2, c, 0.026, 0.024, 0.038, barrel);
            b.disc(2, c, 0.020, 0.0384, true, green_dim);
            b.box(0.008, 0.034, -0.008, 0.02, 0.037, 0.004, green_lens);           // status light
        }
        return;
    }
    b.box(-kH, -kH, -kH, kH, -kH + 0.012, kH, base);                              // base plate
    const double axis[3] = {0, 0.004, 0};
    b.box(-0.012, -kH + 0.012, -0.022, 0.012, -0.012, 0.014, steel_dark);        // saddle
    if (laser) {
        b.cylinder(2, axis, 0.024, -0.038, 0.026, steel);                          // tube
        for (int k = 0; k < 4; ++k) b.cylinder(2, axis, 0.031, -0.032 + k * 0.012, -0.027 + k * 0.012, steel_dark);   // cooling fins
        b.cylinder(2, axis, 0.019, 0.026, 0.038, gunmetal);                        // nose
        b.disc(2, axis, 0.014, 0.0384, true, blue_lens);
    } else {
        b.cylinder(2, axis, 0.02, -0.038, 0.006, steel);                           // body
        b.frustum(2, axis, 0.02, 0.034, 0.006, 0.036, steel_dark, 24, true);       // dish
        b.disc(2, axis, 0.012, 0.0365, true, blue_lens);                           // pickup in the dish
    }
}

inline std::vector<uint8_t> encode_mesh(const std::vector<uint8_t>& stock, const MeshBuilder& b, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m) || b.verts.empty()) return {};
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : b.verts) for (int j = 0; j < 3; ++j) { bounds[j] = std::fmin(bounds[j], v.pos[j]); bounds[3 + j] = std::fmax(bounds[3 + j], v.pos[j]); }
    std::string name(new_name);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t k) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + k); };
    uint32_t u;
    put(stock.data(), 12);                                   // magic, version, (1)
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);                   // attribute header + reserved bytes
    put(bounds, sizeof bounds);
    u = uint32_t(b.verts.size() * sizeof(MeshVertex)); put(&u, 4); put(b.verts.data(), b.verts.size() * sizeof(MeshVertex));
    u = uint32_t(b.idx.size() * 4); put(&u, 4); put(b.idx.data(), b.idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

inline std::vector<uint8_t> build_part_mesh(const std::vector<uint8_t>& stock, int look, bool laser, const char* new_name) {
    MeshBuilder b;
    build_part(b, look < 0 || look > 2 ? 0 : look, laser);
    return encode_mesh(stock, b, new_name);
}

}  // namespace anylaser
