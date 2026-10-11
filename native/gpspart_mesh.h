#pragma once
// GpsPart part mesh: the stock compass sensor mesh plus a yellow arrow that shows the part's forward axis (+Z).
// Built at start-up from the game's own compass_sensor_a.mesh, so no game asset ships with the mod.
//
// Mesh file layout as read from compass_sensor_a.mesh (Anymaker 0.1.24; static inference, checked by parse):
//   "mesh" u32 version(5) u32 (1) u32 name_length, name bytes
//   32-byte attribute header, 56 bytes (zero), 6 x f64 bounds (min xyz, max xyz)
//   u32 vertex_bytes, vertices of 36 bytes: f32 position[3], u8 rgba[4], f32 uv[2], f32 normal[3]
//   u32 index_bytes, u32 indices (triangle list), 8-byte trailer
// Anything that does not match this layout is rejected and the stock mesh is used instead.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gpspart {

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
    for (uint32_t i = 0; i < m.indices; ++i) { uint32_t v; read_u32(b, m.index_bytes + 4 + size_t(i) * 4, v); if (v >= m.vertices) return false; }
    return true;
}

// Adds two-sided yellow arrows pointing +Z: one on the top of the compass post and one on the front (+Z) face of
// the body, each lifted 0.5 mm off the surface. Returns an empty vector when the input layout is not recognised.
inline std::vector<uint8_t> build_gps_mesh(const std::vector<uint8_t>& stock, const char* new_name) {
    MeshLayout m;
    if (!parse_mesh(stock, m)) return {};
    std::vector<MeshVertex> verts(m.vertices);
    std::memcpy(verts.data(), stock.data() + m.vertex_bytes + 4, m.vertices * sizeof(MeshVertex));
    std::vector<uint32_t> idx(m.indices);
    std::memcpy(idx.data(), stock.data() + m.index_bytes + 4, m.indices * 4);
    double bounds[6];
    std::memcpy(bounds, stock.data() + m.bounds, sizeof bounds);
    float top = -1e9f, front = -1e9f;
    for (auto& v : verts) { top = v.pos[1] > top ? v.pos[1] : top; front = v.pos[2] > front ? v.pos[2] : front; }
    float face_lo = 1e9f, face_hi = -1e9f;   // height of the body's front face (the post sits further back)
    for (auto& v : verts) if (v.pos[2] >= front - 1e-4f) { face_lo = v.pos[1] < face_lo ? v.pos[1] : face_lo; face_hi = v.pos[1] > face_hi ? v.pos[1] : face_hi; }

    const uint8_t yellow[4] = {255, 196, 0, 255};
    auto tri = [&](const float a[3], const float b[3], const float c[3], const float n[3]) {
        for (int side = 0; side < 2; ++side) {
            uint32_t base = uint32_t(verts.size());
            const float* p[3] = {a, b, c};
            for (int k = 0; k < 3; ++k) {
                MeshVertex v{};
                std::memcpy(v.pos, p[k], sizeof v.pos);
                std::memcpy(v.rgba, yellow, 4);
                for (int j = 0; j < 3; ++j) v.normal[j] = side ? -n[j] : n[j];
                verts.push_back(v);
            }
            if (side) { idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 1); }
            else { idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2); }
        }
    };
    // Top of the post: arrowhead plus shaft, pointing +Z.
    float y = top + 0.0005f;
    const float up[3] = {0, 1, 0};
    float h0[3] = {0, y, 0.026f}, h1[3] = {-0.018f, y, 0.004f}, h2[3] = {0.018f, y, 0.004f};
    float s0[3] = {-0.007f, y, 0.004f}, s1[3] = {0.007f, y, 0.004f}, s2[3] = {0.007f, y, -0.022f}, s3[3] = {-0.007f, y, -0.022f};
    tri(h0, h1, h2, up); tri(s0, s1, s2, up); tri(s0, s2, s3, up);
    // Front face: an upward-pointing triangle marks the front.
    float z = front + 0.0005f, mid = (face_lo + face_hi) * 0.5f;
    const float fwd[3] = {0, 0, 1};
    float f0[3] = {0, mid + 0.022f, z}, f1[3] = {-0.022f, mid - 0.016f, z}, f2[3] = {0.022f, mid - 0.016f, z};
    tri(f0, f1, f2, fwd);
    bounds[4] = bounds[4] > y ? bounds[4] : y;
    bounds[5] = bounds[5] > z ? bounds[5] : z;

    std::string name(new_name);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); out.insert(out.end(), c, c + n); };
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

}  // namespace gpspart
