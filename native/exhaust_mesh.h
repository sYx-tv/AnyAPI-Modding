#pragma once
// AnyVehicleOverhaul exhaust part meshes, generated in code and written into rom/meshes/components at start-up.
// Tips and mufflers in the three looks Nate picked on 2026-11-04 (he liked all three, so all three ship):
//   Street   brushed double-wall tips, chambered oval muffler
//   Race     burnt titanium rolled tips, straight-through can
//   Classic  chrome slash-cut tips, glasspack
// The turbo muffler, resonators, cat and header collectors are dev-build placeholders in the same style until Nate
// picks their looks. Every part's pipe runs along z through cell (0, 0) (a small pipe: centre at x 0, y 0).
#include "turbo_pd_mesh.h"
#include <cmath>
#include <vector>

namespace exmesh {
using pdmesh::Mesh;
using pdmesh::Rgb;
using pdmesh::V3;
using pdmesh::kPi;

constexpr Rgb kBrushed{178, 181, 186}, kChrome{242, 244, 247}, kSoot{28, 26, 26}, kAlum{128, 130, 132}, kBlack{40, 40, 44};
constexpr Rgb kGold{196, 176, 128}, kPurple{140, 116, 150}, kBlue{104, 120, 160}, kMufflerGrey{110, 112, 116}, kRib{140, 142, 146};
constexpr Rgb kCatShield{150, 152, 150}, kDarkSteel{84, 86, 90};

struct Sec { double z, r; Rgb c; };
// A round tube along z centred (cx, cy) through sections; the last ring's z leans by slash * sin(angle) (a slash cut).
inline void tube(Mesh& m, double cx, double cy, const std::vector<Sec>& s, double slash_last = 0, int n = 28, bool inward = false) {
    auto P = [&](size_t j, int k) {
        double a = 2 * kPi * (k + 0.5) / n;
        double z = s[j].z + (j + 1 == s.size() ? slash_last * std::sin(a) : 0);
        return V3{cx + s[j].r * std::cos(a), cy + s[j].r * std::sin(a), z};
    };
    for (size_t j = 0; j + 1 < s.size(); ++j)
        for (int k = 0; k < n; ++k) {
            int q = (k + 1) % n;
            double a0 = 2 * kPi * (k + 0.5) / n, a1 = 2 * kPi * (q + 0.5) / n, sg = inward ? -1 : 1;
            V3 n0{sg * std::cos(a0), sg * std::sin(a0), 0}, n1{sg * std::cos(a1), sg * std::sin(a1), 0};
            uint32_t A = m.vertex(P(j, k), n0, s[j].c), B = m.vertex(P(j, q), n1, s[j].c), D = m.vertex(P(j + 1, q), n1, s[j + 1].c), E = m.vertex(P(j + 1, k), n0, s[j + 1].c);
            if (!inward) { m.tri(A, B, D); m.tri(A, D, E); } else { m.tri(A, D, B); m.tri(A, E, D); }
        }
}
// A flat ring (r0..r1) facing +z at z (leaning like a slash cut), or -z with facing -1.
inline void annulus(Mesh& m, double cx, double cy, double z, double r0, double r1, Rgb c, double slash = 0, int n = 28, double facing = 1) {
    for (int k = 0; k < n; ++k) {
        int q = (k + 1) % n;
        double a0 = 2 * kPi * (k + 0.5) / n, a1 = 2 * kPi * (q + 0.5) / n;
        m.face({{cx + r0 * std::cos(a0), cy + r0 * std::sin(a0), z + slash * std::sin(a0)}, {cx + r1 * std::cos(a0), cy + r1 * std::sin(a0), z + slash * std::sin(a0)},
                {cx + r1 * std::cos(a1), cy + r1 * std::sin(a1), z + slash * std::sin(a1)}, {cx + r0 * std::cos(a1), cy + r0 * std::sin(a1), z + slash * std::sin(a1)}},
               {0, 0, facing}, c);
    }
}
// The pipe sleeve and clamp band every part has at its inlet face (z -0.04).
inline void inlet(Mesh& m, double z0 = -0.040, double z1 = -0.024, Rgb c = kAlum) {
    tube(m, 0, 0, {{z0, 0.030, c}, {z1, 0.030, c}});
    annulus(m, 0, 0, z1, 0.022, 0.030, c);
    annulus(m, 0, 0, z0, 0.0, 0.030, kSoot, 0, 28, -1);   // the back face looks back (Nate 2026-11-06: tips see-through from behind)
}
inline void outlet(Mesh& m, double z0, double z1, Rgb c = kAlum) {
    tube(m, 0, 0, {{z0, 0.024, c}, {z1 - 0.012, 0.024, c}, {z1 - 0.012, 0.030, c}, {z1, 0.030, c}});
    annulus(m, 0, 0, z1, 0.0, 0.030, kSoot);
}

// ------------------------------------------------------------------------------------- tips
enum Look { kStreet, kRace, kClassic, kLookCount };
enum Layout { kSingle, kDual, kQuad, kSide, kLayoutCount };

// One tip barrel at (cx, cy) from z0 to z1 in a look; r scales the barrel.
inline void barrel(Mesh& m, double cx, double cy, double z0, double z1, int look, double r) {
    if (look == kStreet) {          // brushed, double wall, slight flare
        tube(m, cx, cy, {{z0, r * 0.82, kBrushed}, {z0 + 0.02, r * 0.82, kBrushed}, {z0 + 0.035, r, kBrushed}, {z1, r * 1.06, kBrushed}});
        tube(m, cx, cy, {{z1 - 0.07, r * 0.8, kSoot}, {z1, r * 0.8, kBrushed}}, 0, 28, true);
        annulus(m, cx, cy, z1, r * 0.8, r * 1.06, kBrushed);
        tube(m, cx, cy, {{z0 + 0.03, r * 0.62, kSoot}, {z1 - 0.02, r * 0.62, kSoot}}, 0, 20, true);
    } else if (look == kRace) {     // burnt titanium, big rolled lip
        tube(m, cx, cy, {{z0, r * 0.82, kBrushed}, {z0 + 0.03, r * 0.82, kGold}, {z0 + 0.07, r * 1.1, kPurple}, {z1 - 0.008, r * 1.16, kBlue},
                         {z1 - 0.003, r * 1.24, kBlue}, {z1, r * 1.2, kBlue}});
        tube(m, cx, cy, {{z1 - 0.06, r * 1.06, kSoot}, {z1, r * 1.08, kBlue}}, 0, 28, true);
        annulus(m, cx, cy, z1, r * 1.08, r * 1.2, kBlue);
    } else {                        // chrome, slash cut
        double slash = r * 0.9;
        tube(m, cx, cy, {{z0, r * 0.82, kChrome}, {z0 + 0.03, r * 0.82, kChrome}, {z0 + 0.06, r * 0.94, kChrome}, {z1 - slash, r * 0.94, kChrome}}, slash);
        tube(m, cx, cy, {{z1 - 0.08, r * 0.82, kSoot}, {z1 - slash, r * 0.82, kChrome}}, slash, 28, true);
        annulus(m, cx, cy, z1 - slash, r * 0.82, r * 0.94, kChrome, slash);
    }
}
// A tip part. Single 1x1x3 cells, dual 3x1x3 (barrels at x +-0.065), quad 3x3x3 (+-0.065, +-0.045), side 1x1x6.
inline Mesh tip(int look, int layout) {
    Mesh m;
    inlet(m);
    if (layout == kSingle) barrel(m, 0, 0, -0.024, 0.195, look, 0.033);
    else if (layout == kSide) {
        tube(m, 0, 0, {{-0.024, 0.026, kAlum}, {0.36, 0.026, kAlum}});
        Rgb shield = look == kRace ? kDarkSteel : look == kClassic ? kChrome : kBrushed;
        tube(m, 0, 0, {{0.04, 0.030, shield}, {0.05, 0.036, shield}, {0.31, 0.036, shield}, {0.32, 0.030, shield}});
        for (double z : {0.09, 0.18, 0.27}) tube(m, 0, 0, {{z, 0.038, kAlum}, {z + 0.008, 0.038, kAlum}});
        barrel(m, 0, 0, 0.34, 0.435, look, 0.031);
    } else {
        // a collector box from the inlet out to the barrels
        double w = 0.105, h = layout == kQuad ? 0.085 : 0.034;
        auto box = [&](double k) { return pdmesh::scaled(pdmesh::rounded(-w, -h, w, h, std::min(h, 0.03), 5), 0, 0, k); };
        auto start = pdmesh::scaled(pdmesh::rounded(-w, -h, w, h, std::min(h, 0.03), 5), 0, 0, 0.3);
        Rgb c = look == kClassic ? kChrome : look == kRace ? kDarkSteel : kBrushed;
        pdmesh::loft(m, 2, {{-0.026, start}, {0.03, box(1.0)}, {0.065, box(1.0)}}, c);
        double xs[2] = {-0.062, 0.062};
        double ys[2] = {-0.044, 0.044};
        for (double x : xs) {
            if (layout == kDual) barrel(m, x, 0, 0.065, 0.195, look, 0.03);
            else for (double y : ys) barrel(m, x, y, 0.065, 0.195, look, 0.027);
        }
    }
    return m;
}

// ------------------------------------------------------------------------------------- inline parts
enum Inline { kChambered, kStraight, kGlasspack, kTurboMuffler, kResonator, kHelmholtz, kCat, kHeaderEqual, kHeaderUnequal, kInlineCount };
// Length in cells along z (the part spans z -0.04 .. cells * 0.08 - 0.04).
inline int inline_cells(int k) { return k == kResonator || k == kCat ? 4 : k == kHeaderEqual || k == kHeaderUnequal ? 3 : k == kHelmholtz ? 4 : 6; }
inline Mesh inline_part(int k) {
    Mesh m;
    double end = inline_cells(k) * 0.08 - 0.04;
    inlet(m);
    if (k == kChambered || k == kTurboMuffler) {     // flat oval, stamped ribs (the turbo muffler is rounder, darker)
        bool turbo = k == kTurboMuffler;
        double x = turbo ? 0.10 : 0.112, y0 = turbo ? -0.112 : -0.108, y1 = 0.034, r = turbo ? 0.06 : 0.045;
        auto oval = [&](double s) { return pdmesh::scaled(pdmesh::rounded(-x, y0, x, y1, r, 6), 0, (y0 + y1) / 2, s); };
        Rgb body = turbo ? kDarkSteel : kMufflerGrey;
        tube(m, 0, 0, {{-0.024, 0.024, kAlum}, {0.03, 0.024, kAlum}});
        pdmesh::loft(m, 2, {{0.02, oval(0.55)}, {0.05, oval(0.97)}, {0.06, oval(1.0)}, {end - 0.08, oval(1.0)}, {end - 0.07, oval(0.97)}, {end - 0.04, oval(0.55)}}, body);
        if (!turbo) for (double z = 0.13; z < end - 0.1; z += 0.08) pdmesh::loft(m, 2, {{z, oval(1.02)}, {z + 0.012, oval(1.02)}}, kRib);
        outlet(m, end - 0.05, end);
    } else if (k == kStraight) {                     // brushed can, polished bands and rivets, carbon end cap
        tube(m, 0, 0, {{-0.024, 0.024, kBrushed}, {0.02, 0.026, pdmesh::kPolish}, {0.06, 0.07, kChrome}, {0.07, 0.072, kChrome}, {0.08, 0.072, kBrushed},
                       {end - 0.1, 0.072, kBrushed}, {end - 0.09, 0.073, kChrome}, {end - 0.07, 0.073, kChrome}, {end - 0.045, 0.045, kBlack}, {end - 0.04, 0.03, kBlack}});
        for (int i = 0; i < 12; ++i) {
            double a = 2 * kPi * (i + 0.5) / 12;
            for (double z : {0.072, end - 0.085}) m.prism(2, pdmesh::circle(0.073 * std::cos(a), 0.073 * std::sin(a), 0.003, 6), z, z + 0.004, kChrome);
        }
        outlet(m, end - 0.045, end, kBrushed);
    } else if (k == kGlasspack) {                    // long polished round, cone ends, two bands
        tube(m, 0, 0, {{-0.024, 0.024, kChrome}, {0.02, 0.026, kChrome}, {0.07, 0.056, kChrome}, {0.08, 0.057, kChrome}, {end - 0.11, 0.057, kChrome},
                       {end - 0.1, 0.056, kChrome}, {end - 0.045, 0.026, kChrome}});
        for (double z : {0.11, end - 0.14}) tube(m, 0, 0, {{z, 0.059, kBrushed}, {z + 0.012, 0.059, kBrushed}});
        outlet(m, end - 0.05, end, kChrome);
    } else if (k == kResonator) {                    // short round can
        tube(m, 0, 0, {{-0.024, 0.024, kBrushed}, {0.02, 0.026, kBrushed}, {0.05, 0.046, kBrushed}, {end - 0.07, 0.046, kBrushed}, {end - 0.045, 0.026, kBrushed}});
        tube(m, 0, 0, {{0.12, 0.048, kAlum}, {0.13, 0.048, kAlum}});
        outlet(m, end - 0.05, end, kBrushed);
    } else if (k == kHelmholtz) {                    // straight pipe with a tuned side chamber underneath
        tube(m, 0, 0, {{-0.024, 0.024, kBrushed}, {end - 0.04, 0.024, kBrushed}});
        m.prism(1, pdmesh::circle(0, 0.12, 0.012, 16), -0.05, -0.02, kBrushed);        // the neck (along y, at z 0.12)
        pdmesh::loft(m, 1, {{-0.12, pdmesh::circle(0, 0.12, 0.03, 24)}, {-0.11, pdmesh::circle(0, 0.12, 0.05, 24)}, {-0.06, pdmesh::circle(0, 0.12, 0.05, 24)},
                            {-0.05, pdmesh::circle(0, 0.12, 0.03, 24)}}, kMufflerGrey);
        outlet(m, end - 0.05, end, kBrushed);
    } else if (k == kCat) {                          // oval converter with a ribbed heat shield
        auto oval = [&](double s) { return pdmesh::scaled(pdmesh::rounded(-0.075, -0.05, 0.075, 0.03, 0.035, 6), 0, -0.01, s); };
        tube(m, 0, 0, {{-0.024, 0.024, kAlum}, {0.02, 0.024, kAlum}});
        pdmesh::loft(m, 2, {{0.015, oval(0.45)}, {0.05, oval(1.0)}, {end - 0.07, oval(1.0)}, {end - 0.04, oval(0.45)}}, kCatShield);
        for (double z = 0.07; z < end - 0.08; z += 0.025) pdmesh::loft(m, 2, {{z, oval(1.03)}, {z + 0.006, oval(1.03)}}, kRib);
        outlet(m, end - 0.045, end);
    } else {                                         // header collector: four primaries merging into one
        bool uneven = k == kHeaderUnequal;
        for (int i = 0; i < 4; ++i) {
            double a = kPi / 4 + kPi / 2 * i, x = 0.05 * std::cos(a), y = 0.05 * std::sin(a);
            double z0 = uneven ? -0.024 + 0.02 * (i % 2) : -0.024;
            double kink = uneven ? (i % 2 ? 0.012 : -0.01) : 0;
            std::vector<std::pair<double, pdmesh::Ring>> path;
            for (int j = 0; j <= 6; ++j) {
                double t = j / 6.0, z = z0 + (0.10 - z0) * t;
                double k2 = 1 - t * t, cx = x * k2 + kink * std::sin(t * kPi), cy = y * k2;
                path.push_back({z, pdmesh::circle(cx, cy, 0.017, 16)});
            }
            pdmesh::loft(m, 2, path, kBrushed);
        }
        tube(m, 0, 0, {{0.09, 0.036, kBrushed}, {0.13, 0.036, kBrushed}, {0.16, 0.026, kBrushed}});
        outlet(m, 0.15, end, kBrushed);
    }
    return m;
}

// ------------------------------------------------------------------------------------- intake parts (dev placeholders)
enum Intake { kVelocityStack, kItb, kCarb };
inline Mesh intake_part(int k) {
    Mesh m;
    inlet(m, -0.040, -0.024, kAlum);
    if (k == kVelocityStack) {                       // polished trumpet with a rolled lip
        tube(m, 0, 0, {{-0.024, 0.024, pdmesh::kPolish}, {0.04, 0.026, pdmesh::kPolish}, {0.08, 0.032, pdmesh::kPolish}, {0.1, 0.04, pdmesh::kPolish}, {0.11, 0.046, pdmesh::kPolish},
                       {0.115, 0.05, kChrome}, {0.118, 0.046, kChrome}});
    } else if (k == kItb) {                          // black throttle body with its shaft, a short alloy stack
        m.prism(2, pdmesh::rounded(-0.034, -0.034, 0.034, 0.034, 0.008, 4), -0.024, 0.03, kBlack);
        m.prism(0, pdmesh::circle(0, 0.003, 0.006, 10), -0.04, 0.04, kAlum);
        tube(m, 0, 0, {{0.03, 0.026, kAlum}, {0.08, 0.03, kAlum}, {0.105, 0.04, kAlum}, {0.11, 0.043, kChrome}});
    } else {                                         // carb body on the pipe, a big round chrome-lidded filter on top
        tube(m, 0, 0, {{-0.024, 0.024, kAlum}, {0.0, 0.024, kAlum}});
        m.prism(2, pdmesh::rounded(-0.05, -0.035, 0.05, 0.1, 0.01, 4), 0.0, 0.14, kGold);
        m.prism(1, pdmesh::circle(0, 0.08, 0.112, 40), 0.1, 0.112, kBlack);
        m.prism(1, pdmesh::circle(0, 0.08, 0.108, 40), 0.112, 0.16, kMufflerGrey);
        m.prism(1, pdmesh::circle(0, 0.08, 0.112, 40), 0.16, 0.172, kChrome);
        m.prism(1, pdmesh::circle(0, 0.08, 0.012, 12), 0.172, 0.185, kChrome);   // the wing nut
    }
    return m;
}

}  // namespace exmesh
