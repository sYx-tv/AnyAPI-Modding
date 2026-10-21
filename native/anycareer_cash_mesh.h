#pragma once
// AnyCareer Cash look: a bundle of green bills with a paper band, made at start-up as a flat-colour mesh in the
// game's own part-mesh format. The file header is copied from the player's stock compass_sensor_a.mesh (the
// template GpsPart already builds on; see gpspart_mesh.h for the layout), so no game asset ships with the mod.
// Unit-tested by tests/anycareer_logic_test.cpp.
#include "gpspart_mesh.h"
#include <cstdint>
#include <string>
#include <vector>

namespace anycareer::cashmesh {

// Bundle size in metres: centred on x and z, sitting on y = 0. The Cash definition's floor physics box matches it.
inline constexpr float kLength = 0.16f, kWidth = 0.07f, kHeight = 0.035f;

struct Box { float lo[3], hi[3]; uint8_t rgba[4]; };

// The bundle: five stacked layers in two greens (bill edges), a lighter printed panel and a dark centre mark on
// top, and a paper band round the middle.
inline std::vector<Box> bundle() {
    std::vector<Box> boxes;
    const float x = kLength / 2, z = kWidth / 2;
    for (int i = 0; i < 5; ++i) {
        float y0 = kHeight * float(i) / 5, y1 = kHeight * float(i + 1) / 5;
        bool edge = i % 2 == 1 && i != 4;
        boxes.push_back({{-x, y0, -z}, {x, y1, z}, {uint8_t(edge ? 198 : 63), uint8_t(edge ? 214 : 122), uint8_t(edge ? 186 : 72), 255}});
    }
    const float top = kHeight;
    boxes.push_back({{-x + 0.008f, top, -z + 0.007f}, {x - 0.008f, top + 0.0006f, z - 0.007f}, {112, 170, 110, 255}});
    boxes.push_back({{-0.034f, top + 0.0006f, -0.016f}, {-0.012f, top + 0.0012f, 0.016f}, {58, 112, 66, 255}});
    boxes.push_back({{0.012f, top + 0.0006f, -0.016f}, {0.034f, top + 0.0012f, 0.016f}, {58, 112, 66, 255}});
    boxes.push_back({{-0.014f, -0.001f, -z - 0.001f}, {0.014f, top + 0.0016f, z + 0.001f}, {228, 212, 164, 255}});
    return boxes;
}

// Every face is two-sided (both windings), as GpsPart's arrows are, so the result never depends on the game's
// winding convention.
inline void add_box(const Box& b, std::vector<gpspart::MeshVertex>& verts, std::vector<uint32_t>& idx) {
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = 0; sign < 2; ++sign) {
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            float c[4][3];
            const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int k = 0; k < 4; ++k) {
                c[k][axis] = sign ? b.hi[axis] : b.lo[axis];
                c[k][u] = uv[k][0] ? b.hi[u] : b.lo[u];
                c[k][v] = uv[k][1] ? b.hi[v] : b.lo[v];
            }
            for (int side = 0; side < 2; ++side) {
                uint32_t base = uint32_t(verts.size());
                for (int k = 0; k < 4; ++k) {
                    gpspart::MeshVertex mv{};
                    for (int j = 0; j < 3; ++j) mv.pos[j] = c[k][j];
                    for (int j = 0; j < 4; ++j) mv.rgba[j] = b.rgba[j];
                    float out = (sign ? 1.f : -1.f) * (side ? -1.f : 1.f);
                    mv.normal[axis] = out;
                    verts.push_back(mv);
                }
                const uint32_t a[6] = {0, 1, 2, 0, 2, 3}, r[6] = {0, 2, 1, 0, 3, 2};
                for (int k = 0; k < 6; ++k) idx.push_back(base + ((sign ^ side) ? a[k] : r[k]));
            }
        }
}

// The finished mesh file, or empty when the template isn't the layout we know.
inline std::vector<uint8_t> build(const std::vector<uint8_t>& stock, const char* new_name) {
    gpspart::MeshLayout m;
    if (!gpspart::parse_mesh(stock, m)) return {};
    std::vector<gpspart::MeshVertex> verts;
    std::vector<uint32_t> idx;
    double bounds[6] = {1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    for (const Box& b : bundle()) {
        add_box(b, verts, idx);
        for (int j = 0; j < 3; ++j) {
            bounds[j] = b.lo[j] < bounds[j] ? b.lo[j] : bounds[j];
            bounds[3 + j] = b.hi[j] > bounds[3 + j] ? b.hi[j] : bounds[3 + j];
        }
    }
    std::string name(new_name);
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); out.insert(out.end(), c, c + n); };
    uint32_t u;
    put(stock.data(), 12);                                   // magic, version, (1)
    u = uint32_t(name.size()); put(&u, 4); put(name.data(), name.size());
    put(stock.data() + m.bounds - 88, 88);                   // attribute header + reserved bytes
    put(bounds, sizeof bounds);
    u = uint32_t(verts.size() * sizeof(gpspart::MeshVertex)); put(&u, 4); put(verts.data(), verts.size() * sizeof(gpspart::MeshVertex));
    u = uint32_t(idx.size() * 4); put(&u, 4); put(idx.data(), idx.size() * 4);
    put(stock.data() + m.trailer, 8);
    return out;
}

}  // namespace anycareer::cashmesh
