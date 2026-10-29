#pragma once
// AnyCVT meshes, built at start-up from the game's own gearboxes (rom/meshes/components/gear_box_a/b/c.mesh), so no
// game asset ships with the mod and the CVTs look exactly like the stock gearboxes.
//
// The small CVT uses gear_box_a.mesh as it is. The medium and large are gear_box_b and gear_box_c with the small
// gearbox's shifter connector (the flat decals on top of gear_box_a, where a shifter link is wired) copied onto the
// top of the case over the cell where their mechanical face now is (anycvt_logic.h): the stock medium and large shift
// by air and have none.
//
// gear_box_a's connector: the triangles whose corners all lie on its top face (y 0.12 m) within 0.031 m of the middle
// across and 0.049 to 0.111 m along z (centred on the shifter cell, z = 0.08). One cell is 0.08 m.
// Mesh file layout (Anymaker 0.1.28; the same reader as AnyAlternator and the turbos):
//   "mesh" u32 version(5) u32 (1) u32 name_length, name bytes
//   32-byte attribute header, 56 bytes (zero), 6 x f64 bounds (min xyz, max xyz)
//   u32 vertex_bytes, vertices of 36 bytes: f32 position[3], u8 rgba[4], f32 uv[2], f32 normal[3]
//   u32 index_bytes, u32 indices (triangle list), 8-byte trailer
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "anycvt_logic.h"

namespace anycvt {

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
inline std::vector<MeshVertex> vertices_of(const std::vector<uint8_t>& b, const MeshLayout& m) {
    std::vector<MeshVertex> v(m.vertices);
    std::memcpy(v.data(), b.data() + m.vertex_bytes + 4, m.vertices * sizeof(MeshVertex));
    return v;
}
inline std::vector<uint32_t> indices_of(const std::vector<uint8_t>& b, const MeshLayout& m) {
    std::vector<uint32_t> i(m.indices);
    std::memcpy(i.data(), b.data() + m.index_bytes + 4, m.indices * 4);
    return i;
}
inline float top_of(const std::vector<MeshVertex>& v) {
    float hi = -1e9f;
    for (auto& x : v) hi = std::fmax(hi, x.pos[1]);
    return hi;
}

// gear_box_a's shifter connector as a triangle list (3 vertices each), centred on the shifter cell; empty when the
// small gearbox is not the one this expects.
constexpr float kSmallTop = 0.12f, kPadHalfWidth = 0.031f, kPadZ0 = 0.049f, kPadZ1 = 0.111f, kSmallShifterZ = 0.08f;
constexpr size_t kMinConnectorTriangles = 20;
inline std::vector<MeshVertex> shifter_connector(const std::vector<uint8_t>& small_mesh) {
    MeshLayout m;
    if (!parse_mesh(small_mesh, m)) return {};
    std::vector<MeshVertex> v = vertices_of(small_mesh, m);
    std::vector<uint32_t> idx = indices_of(small_mesh, m);
    if (std::fabs(top_of(v) - kSmallTop) > 0.006f) return {};
    auto on_pad = [](const MeshVertex& x) {
        return x.pos[1] >= kSmallTop - 0.0005f && std::fabs(x.pos[0]) <= kPadHalfWidth && x.pos[2] >= kPadZ0 && x.pos[2] <= kPadZ1;
    };
    std::vector<MeshVertex> out;
    for (size_t t = 0; t + 2 < idx.size(); t += 3)
        if (on_pad(v[idx[t]]) && on_pad(v[idx[t + 1]]) && on_pad(v[idx[t + 2]]))
            for (int k = 0; k < 3; ++k) out.push_back(v[idx[t + k]]);
    if (out.size() / 3 < kMinConnectorTriangles) return {};
    return out;
}

// The medium or large CVT mesh: the stock gearbox with the connector laid 0.5 mm over its top face at the shifter
// cell. Empty when the stock mesh is not the gearbox this expects.
inline std::vector<uint8_t> build_mesh(const std::vector<uint8_t>& stock, const std::vector<MeshVertex>& connector, const Size& size) {
    MeshLayout m;
    if (!size.mesh || connector.empty() || !parse_mesh(stock, m)) return {};
    std::vector<MeshVertex> verts = vertices_of(stock, m);
    std::vector<uint32_t> idx = indices_of(stock, m);
    if (std::fabs(top_of(verts) - float(size.top_y)) > 0.006f) return {};
    const float dy = float(size.top_y) - kSmallTop + 0.0005f;
    const float dx = float(kCell * size.shifter.x), dz = float(kCell * size.shifter.z) - kSmallShifterZ;
    for (MeshVertex v : connector) {   // the triangles keep the stock decals' winding and normals
        v.pos[0] += dx; v.pos[1] += dy; v.pos[2] += dz;
        verts.push_back(v);
        idx.push_back(uint32_t(verts.size() - 1));
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

}  // namespace anycvt
