#pragma once
// Auto-split for bent windows: the game draws, collides and saves every plate as one flat, convex panel, so a window
// loop that bends is cut into flat convex pieces. The cuts become new struts (painted hidden by AnyHelpers) and each
// piece is placed as its own window. Pure logic on grid points, no game access; tested in tests/anymirror_split_test.cpp.
#include <cmath>
#include <cstdint>
#include <vector>

namespace anymirror_split {
struct P { int64_t x, y, z; };
inline P sub(P a, P b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline P cross(P a, P b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline int64_t dot(P a, P b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline bool zero(P a) { return !a.x && !a.y && !a.z; }

// Normal of the first non-degenerate corner of a loop, or zero when every point is on one line.
inline P loop_normal(const std::vector<P>& p, const std::vector<int>& idx) {
    size_t n = idx.size();
    for (size_t i = 0; i < n; ++i) {
        P a = p[idx[i]], b = p[idx[(i + 1) % n]], c = p[idx[(i + 2) % n]];
        P c3 = cross(sub(b, a), sub(c, b));
        if (!zero(c3)) return c3;
    }
    return {0, 0, 0};
}
// Every point of the loop lies on one plane (exact, integer).
inline bool planar(const std::vector<P>& p, const std::vector<int>& idx) {
    P nrm = loop_normal(p, idx);
    if (zero(nrm)) return false;
    for (int i : idx) if (dot(nrm, sub(p[i], p[idx[0]]))) return false;
    return true;
}
// Planar and convex: every corner turns the same way (straight corners allowed), and the loop winds once.
inline bool flat_convex(const std::vector<P>& p, const std::vector<int>& idx) {
    if (idx.size() < 3 || !planar(p, idx)) return false;
    P nrm = loop_normal(p, idx);
    size_t n = idx.size();
    for (size_t i = 0; i < n; ++i) {
        P a = p[idx[i]], b = p[idx[(i + 1) % n]], c = p[idx[(i + 2) % n]];
        if (dot(cross(sub(b, a), sub(c, b)), nrm) < 0) return false;
    }
    // Total turning must be one full turn, or a star-shaped loop would pass.
    double ax = double(nrm.x), ay = double(nrm.y), az = double(nrm.z), len = std::sqrt(ax * ax + ay * ay + az * az), turn = 0;
    for (size_t i = 0; i < n; ++i) {
        P a = p[idx[i]], b = p[idx[(i + 1) % n]], c = p[idx[(i + 2) % n]];
        P u = sub(b, a), v = sub(c, b);
        P cr = cross(u, v);
        double s = (cr.x * ax + cr.y * ay + cr.z * az) / len, d = double(dot(u, v));
        turn += std::atan2(s, d);
    }
    return std::fabs(turn - 6.283185307179586) < 1e-6;
}

struct Plan {
    std::vector<std::vector<int>> pieces;           // loop indices, each in the loop's own order
    std::vector<std::pair<int, int>> cuts;          // loop index pairs joined by a new strut
};

// Cuts the loop along diagonals until every piece is flat and convex. At each step the diagonal that leaves the most
// corners in flat convex pieces is taken (so the cuts follow the bends), then each side is cut again. Diagonals must lie
// inside the loop seen along its average (Newell) normal. `cut_ok(a, b)` says whether a strut between loop points a and
// b may be added. Empty pieces when the loop is already a flat convex plate (nothing to do) or cannot be cut.
template <class CutOk> Plan split(const std::vector<P>& p, CutOk&& cut_ok) {
    Plan plan;
    int n = int(p.size());
    if (n < 4) return plan;
    std::vector<int> all(n);
    for (int i = 0; i < n; ++i) all[i] = i;
    if (flat_convex(p, all)) return plan;
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; ++i) {
        const P& a = p[i]; const P& b = p[(i + 1) % n];
        nx += double(a.y - b.y) * double(a.z + b.z); ny += double(a.z - b.z) * double(a.x + b.x); nz += double(a.x - b.x) * double(a.y + b.y);
    }
    double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl < 1e-9) return plan;
    nx /= nl; ny /= nl; nz /= nl;
    double ux, uy, uz;
    if (std::fabs(nx) < 0.9) { ux = 0; uy = -nz; uz = ny; } else { ux = nz; uy = 0; uz = -nx; }
    double ul = std::sqrt(ux * ux + uy * uy + uz * uz); ux /= ul; uy /= ul; uz /= ul;
    double vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
    std::vector<double> X(n), Y(n);
    for (int i = 0; i < n; ++i) { X[i] = p[i].x * ux + p[i].y * uy + p[i].z * uz; Y[i] = p[i].x * vx + p[i].y * vy + p[i].z * vz; }
    auto orient = [&](double ax, double ay, double bx, double by, double cx, double cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
    auto crosses = [&](int a, int b, int c, int d) {   // proper crossing of segments ab and cd in projection
        double o1 = orient(X[a], Y[a], X[b], Y[b], X[c], Y[c]), o2 = orient(X[a], Y[a], X[b], Y[b], X[d], Y[d]);
        double o3 = orient(X[c], Y[c], X[d], Y[d], X[a], Y[a]), o4 = orient(X[c], Y[c], X[d], Y[d], X[b], Y[b]);
        return ((o1 > 1e-9 && o2 < -1e-9) || (o1 < -1e-9 && o2 > 1e-9)) && ((o3 > 1e-9 && o4 < -1e-9) || (o3 < -1e-9 && o4 > 1e-9));
    };
    auto inside = [&](const std::vector<int>& poly, double px, double py) {   // even-odd test in projection
        bool in = false; size_t m = poly.size();
        for (size_t i = 0, j = m - 1; i < m; j = i++) {
            double xi = X[poly[i]], yi = Y[poly[i]], xj = X[poly[j]], yj = Y[poly[j]];
            if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
        }
        return in;
    };
    std::vector<std::pair<int, int>> cuts;
    std::vector<std::vector<int>> pieces, todo{all};
    while (!todo.empty()) {
        std::vector<int> poly = todo.back(); todo.pop_back();
        if (flat_convex(p, poly)) { pieces.push_back(poly); continue; }
        int m = int(poly.size()), best_i = -1, best_j = -1, best_score = -1;
        if (m < 4) return Plan{};   // a non-flat triangle cannot happen; a straight one cannot be cut
        for (int i = 0; i < m; ++i)
            for (int j = i + 2; j < m; ++j) {
                if (i == 0 && j == m - 1) continue;
                int a = poly[i], b = poly[j];
                bool clear = true;
                for (int k = 0; k < m && clear; ++k) {
                    int c = poly[k], d = poly[(k + 1) % m];
                    if (c == a || c == b || d == a || d == b) continue;
                    clear = !crosses(a, b, c, d);
                }
                if (!clear || !inside(poly, (X[a] + X[b]) / 2, (Y[a] + Y[b]) / 2)) continue;
                std::vector<int> s1(poly.begin() + i, poly.begin() + j + 1), s2(poly.begin() + j, poly.end());
                s2.insert(s2.end(), poly.begin(), poly.begin() + i + 1);
                int score = (flat_convex(p, s1) ? int(s1.size()) : 0) + (flat_convex(p, s2) ? int(s2.size()) : 0);
                if (score > best_score && cut_ok(a, b)) { best_score = score; best_i = i; best_j = j; }
            }
        if (best_i < 0) return Plan{};   // nowhere to cut: leave the loop to the game
        std::vector<int> s1(poly.begin() + best_i, poly.begin() + best_j + 1), s2(poly.begin() + best_j, poly.end());
        s2.insert(s2.end(), poly.begin(), poly.begin() + best_i + 1);
        cuts.push_back({poly[best_i], poly[best_j]});
        todo.push_back(s1); todo.push_back(s2);
    }
    plan.pieces = pieces; plan.cuts = cuts;
    return plan;
}
}  // namespace anymirror_split
