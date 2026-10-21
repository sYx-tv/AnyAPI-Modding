#pragma once
// Dyno part mesh: the stock compass sensor body with three orange bars of rising height on its top (a little bar
// chart) and a rising orange curve on its front face, so it reads apart from the compass and the GPS Sensor. Built at
// start-up from the game's own compass_sensor_a.mesh (layout and parser in gpspart_mesh.h), so no game asset ships
// with the mod. The added faces are two-sided, so their winding cannot hide them.
#include "gpspart_mesh.h"
#include <algorithm>
#include <cmath>

namespace dyno {

inline std::vector<uint8_t> build_dyno_mesh(const std::vector<uint8_t>& stock, const char* new_name) {
    using gpspart::MeshVertex;
    gpspart::MeshLayout m;
    if (!gpspart::parse_mesh(stock, m)) return {};
    std::vector<MeshVertex> verts(m.vertices);
    std::memcpy(verts.data(), stock.data() + m.vertex_bytes + 4, m.vertices * sizeof(MeshVertex));
    std::vector<uint32_t> idx(m.indices);
    std::memcpy(idx.data(), stock.data() + m.index_bytes + 4, m.indices * 4);
    double bounds[6];
    std::memcpy(bounds, stock.data() + m.bounds, sizeof bounds);
    float top = -1e9f, front = -1e9f;
    for (auto& v : verts) { top = std::max(top, v.pos[1]); front = std::max(front, v.pos[2]); }
    float face_lo = 1e9f, face_hi = -1e9f;
    for (auto& v : verts) if (v.pos[2] >= front - 1e-4f) { face_lo = std::min(face_lo, v.pos[1]); face_hi = std::max(face_hi, v.pos[1]); }

    const uint8_t orange[4] = {255, 122, 42, 255};
    auto quad = [&](const float a[3], const float b[3], const float c[3], const float d[3], const float n[3]) {
        for (int side = 0; side < 2; ++side) {
            uint32_t base = uint32_t(verts.size());
            const float* p[4] = {a, b, c, d};
            for (int k = 0; k < 4; ++k) {
                MeshVertex v{};
                std::memcpy(v.pos, p[k], sizeof v.pos);
                std::memcpy(v.rgba, orange, 4);
                for (int j = 0; j < 3; ++j) v.normal[j] = side ? -n[j] : n[j];
                verts.push_back(v);
            }
            if (side) { for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) idx.push_back(base + i); }
            else { for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i); }
        }
    };
    // Three bars on the top, rising left to right (x), each a closed box.
    float y0 = top, ymax = top;
    const float bar_w = 0.008f, gap = 0.004f, depth = 0.010f, heights[3] = {0.008f, 0.014f, 0.020f};
    for (int i = 0; i < 3; ++i) {
        float x0 = -1.5f * bar_w - gap + i * (bar_w + gap), x1 = x0 + bar_w, z0 = -depth * 0.5f, z1 = depth * 0.5f, y1 = y0 + heights[i];
        ymax = std::max(ymax, y1);
        const float up[3] = {0, 1, 0}, fw[3] = {0, 0, 1}, bk[3] = {0, 0, -1}, rt[3] = {1, 0, 0}, lt[3] = {-1, 0, 0};
        float a[3] = {x0, y1, z0}, b[3] = {x1, y1, z0}, c[3] = {x1, y1, z1}, d[3] = {x0, y1, z1};
        quad(a, b, c, d, up);
        float f0[3] = {x0, y0, z1}, f1[3] = {x1, y0, z1}, f2[3] = {x1, y1, z1}, f3[3] = {x0, y1, z1};
        quad(f0, f1, f2, f3, fw);
        float k0[3] = {x0, y0, z0}, k1[3] = {x0, y1, z0}, k2[3] = {x1, y1, z0}, k3[3] = {x1, y0, z0};
        quad(k0, k1, k2, k3, bk);
        float r0[3] = {x1, y0, z0}, r1[3] = {x1, y1, z0}, r2[3] = {x1, y1, z1}, r3[3] = {x1, y0, z1};
        quad(r0, r1, r2, r3, rt);
        float l0[3] = {x0, y0, z0}, l1[3] = {x0, y0, z1}, l2[3] = {x0, y1, z1}, l3[3] = {x0, y1, z0};
        quad(l0, l1, l2, l3, lt);
    }
    // Front face: a power-curve stripe rising from bottom left and flattening at the top right.
    float z = front + 0.0005f, lo = face_lo + (face_hi - face_lo) * 0.2f, hi = face_lo + (face_hi - face_lo) * 0.8f;
    const float fwd[3] = {0, 0, 1}, half = 0.0025f;
    const int segments = 8;
    for (int i = 0; i < segments; ++i) {
        auto curve = [&](int k, float& x, float& y) {
            float t = float(k) / segments;
            x = -0.028f + 0.056f * t;
            y = lo + (hi - lo) * float(std::sin(std::min(1.0, t * 1.3) * 1.5707963));
        };
        float xa, ya, xb, yb;
        curve(i, xa, ya); curve(i + 1, xb, yb);
        float p0[3] = {xa, ya - half, z}, p1[3] = {xb, yb - half, z}, p2[3] = {xb, yb + half, z}, p3[3] = {xa, ya + half, z};
        quad(p0, p1, p2, p3, fwd);
    }
    bounds[4] = std::max(bounds[4], double(ymax));
    bounds[5] = std::max(bounds[5], double(z));

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

}  // namespace dyno
