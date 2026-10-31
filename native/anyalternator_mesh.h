#pragma once
// AnyAlternator meshes, built at start-up from the game's own alternator (rom/meshes/components/alternator_a_motor.mesh),
// so no game asset ships with the mod.
//
// alternator_a_motor.mesh is the 2 x 2 x 3 cell alternator: x and y from -0.04 to 0.12 m (cells 0 and 1), z from
// -0.12 to 0.12 m (cells -1 to 1) with the shaft stub out to 0.13 m on the +z end, centred on x = y = 0 (the torque
// cell). One cell is 0.08 m.
// A bigger alternator (N x N cells across, L long) is that mesh:
//   - body: scaled by N/2 across about the middle of its face and by L/3 along z about its +z end (which stays at
//     0.12 m, as the definition keeps the +z end at cell 1);
//   - shaft: the hub, its ring and the stub (everything that is not body paint, within 0.031 m of the axis, on the
//     +z face) moved onto the torque cell (c, c), grown a little (not the full N/2: the torque face is one cell) and
//     lifted 6 mm onto a collar, since the end plate under it is now solid;
//   - the end plate's opening that the stock hub filled (an octagon of radius 0.03 m about the old axis, scaled with
//     the body) is closed with a flat octagon in the plate's paint.
// Mesh file layout (Anymaker 0.2.0; the same reader as GpsPart and the turbos):
//   "mesh" u32 version(5) u32 (1) u32 name_length, name bytes
//   32-byte attribute header, 56 bytes (zero), 6 x f64 bounds (min xyz, max xyz)
//   u32 vertex_bytes, vertices of 36 bytes: f32 position[3], u8 rgba[4], f32 uv[2], f32 normal[3]
//   u32 index_bytes, u32 indices (triangle list), 8-byte trailer
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "anyalternator_logic.h"

namespace anyalternator {

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

// Body paint (slot 0, the stock end plates): within 2 of (153, 0, 0), as the game matches paint markers.
inline bool is_body_paint(const uint8_t rgba[4]) { return rgba[0] >= 151 && rgba[0] <= 155 && rgba[1] <= 2 && rgba[2] <= 2; }
constexpr float kHubRadius = 0.031f, kHubFace = 0.112f, kLift = 0.006f;
inline bool is_hub(const MeshVertex& v) {
    return !is_body_paint(v.rgba) && v.pos[2] >= kHubFace && std::fabs(v.pos[0]) <= kHubRadius && std::fabs(v.pos[1]) <= kHubRadius;
}
inline float hub_scale(const Size& s) { return 1.0f + 0.35f * (float(s.width) / 2.0f - 1.0f); }

// Empty when the stock mesh is not the alternator this expects.
inline std::vector<uint8_t> build_mesh(const std::vector<uint8_t>& stock, const Size& size) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    std::vector<MeshVertex> verts(m.vertices);
    std::memcpy(verts.data(), stock.data() + m.vertex_bytes + 4, m.vertices * sizeof(MeshVertex));
    std::vector<uint32_t> idx(m.indices);
    std::memcpy(idx.data(), stock.data() + m.index_bytes + 4, m.indices * 4);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (auto& v : verts) for (int j = 0; j < 3; ++j) { lo[j] = std::fmin(lo[j], v.pos[j]); hi[j] = std::fmax(hi[j], v.pos[j]); }
    auto about = [](float a, float b) { return std::fabs(a - b) < 0.006f; };
    if (!about(lo[0], -0.04f) || !about(hi[0], 0.12f) || !about(lo[1], -0.04f) || !about(hi[1], 0.12f) || !about(lo[2], -0.12f) || hi[2] > 0.15f) return {};

    const float sxy = float(size.width) / 2.0f, sz = float(size.length) / 3.0f, k = hub_scale(size);
    const float mid = 0.04f, new_mid = float(-0.5 * kCell + 0.5 * kCell * size.width), front = 0.12f;
    const float shaft = float(kCell * size.shaft_cell());
    for (auto& v : verts) {
        float* p = v.pos;
        float n[3] = {v.normal[0], v.normal[1], v.normal[2]};
        if (is_hub(v)) {
            p[0] = shaft + p[0] * k; p[1] = shaft + p[1] * k;
            p[2] = front + kLift + (p[2] - front) * k;
            continue;                                // a uniform scale keeps the normals
        }
        p[0] = new_mid + (p[0] - mid) * sxy; p[1] = new_mid + (p[1] - mid) * sxy;
        p[2] = p[2] > front ? p[2] : front + (p[2] - front) * sz;
        // Normals scale by the inverse of the position scale, then renormalise.
        n[0] /= sxy; n[1] /= sxy; n[2] /= sz;
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-6f) for (int j = 0; j < 3; ++j) v.normal[j] = n[j] / l;
    }
    // The collar under the lifted hub, and the patch over the old opening. Faces are listed counter-clockwise from
    // outside and stored the other way round, as the game draws clockwise faces (like every stock mesh).
    auto vertex = [&](float x, float y, float z, float nx, float ny, float nz, const uint8_t c[3]) {
        MeshVertex v{};
        v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
        v.rgba[0] = c[0]; v.rgba[1] = c[1]; v.rgba[2] = c[2]; v.rgba[3] = 255;
        v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
        verts.push_back(v);
        return uint32_t(verts.size() - 1);
    };
    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(c); idx.push_back(b); };
    // The stock opening and ring: an octagon with corners at (0.03, 0.012) and its turns.
    const float oct[8][2] = {{0.03f, -0.012f}, {0.03f, 0.012f}, {0.012f, 0.03f}, {-0.012f, 0.03f},
                             {-0.03f, 0.012f}, {-0.03f, -0.012f}, {-0.012f, -0.03f}, {0.012f, -0.03f}};
    static const uint8_t kPlate[3] = {153, 0, 0}, kRing[3] = {198, 198, 198};
    {
        const float cx = new_mid + (0 - mid) * sxy, cy = cx;
        uint32_t centre = vertex(cx, cy, front, 0, 0, 1, kPlate), ring[8];
        for (int i = 0; i < 8; ++i) ring[i] = vertex(cx + oct[i][0] * sxy, cy + oct[i][1] * sxy, front, 0, 0, 1, kPlate);
        for (int i = 0; i < 8; ++i) tri(centre, ring[i], ring[(i + 1) % 8]);
    }
    for (int i = 0; i < 8; ++i) {
        const float* a = oct[i];
        const float* b = oct[(i + 1) % 8];
        float nx = (a[1] - b[1]), ny = (b[0] - a[0]), l = std::sqrt(nx * nx + ny * ny);
        nx /= l; ny /= l;
        float ax = shaft + a[0] * k, ay = shaft + a[1] * k, bx = shaft + b[0] * k, by = shaft + b[1] * k;
        uint32_t a0 = vertex(ax, ay, front, nx, ny, 0, kRing), b0 = vertex(bx, by, front, nx, ny, 0, kRing);
        uint32_t a1 = vertex(ax, ay, front + kLift, nx, ny, 0, kRing), b1 = vertex(bx, by, front + kLift, nx, ny, 0, kRing);
        tri(a0, b0, b1); tri(a0, b1, a1);
    }
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (auto& v : verts) for (int j = 0; j < 3; ++j) { bounds[j] = std::fmin(bounds[j], v.pos[j]); bounds[3 + j] = std::fmax(bounds[3 + j], v.pos[j]); }

    std::string name(size.mesh);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + n); };
    uint32_t u;
    put(stock.data(), 12);                                   // magic, version, (1)
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);                   // attribute header + reserved bytes
    put(bounds, sizeof bounds);
    u = uint32_t(verts.size() * sizeof(MeshVertex)); put(&u, 4); put(verts.data(), verts.size() * sizeof(MeshVertex));
    u = uint32_t(idx.size() * 4); put(&u, 4); put(idx.data(), idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

}  // namespace anyalternator
