// Exhaust parts (exhaust_logic.h, exhaust_mesh.h): ids, flow, sound tune and that every mesh fits its part's cells.
#include "../exhaust_logic.h"
#include "../exhaust_mesh.h"
#include "../avo_looks_mesh.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace exhaust;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e; }
static void fits(const pdmesh::Mesh& m, const Box& b, const char* what) {
    const double h = 0.04, slack = 0.002;           // a cell is 0.08 m, centred on its index
    for (auto& v : m.v) {
        bool ok = v.pos[0] >= b.x0 * 0.08 - h - slack && v.pos[0] <= b.x1 * 0.08 + h + slack && v.pos[1] >= b.y0 * 0.08 - h - slack &&
                  v.pos[1] <= b.y1 * 0.08 + h + slack && v.pos[2] >= b.z0 * 0.08 - h - slack && v.pos[2] <= b.z1 * 0.08 + h + slack;
        if (!ok) { std::fflush(stdout); std::fprintf(stderr, "%s: vertex (%.3f %.3f %.3f) outside its cells\n", what, v.pos[0], v.pos[1], v.pos[2]); assert(false); }
    }
}
int main() {
    // ids round trip
    for (int l = 0; l < kLookCount; ++l)
        for (int y = 0; y < kLayoutCount; ++y) {
            std::string id = tip_id(l, y);
            int a = -1, b = -1;
            assert(tip_from_id(id.data(), id.size(), &a, &b) && a == l && b == y);
            fits(exmesh::tip(l, y), tip_box(y), id.c_str());
        }
    for (int k = 0; k < kInlineCount; ++k) {
        assert(inline_from_id(kInlineIds[k], std::strlen(kInlineIds[k])) == k);
        assert(inline_box(k).z1 + 1 == exmesh::inline_cells(k));
        fits(exmesh::inline_part(k), inline_box(k), kInlineIds[k]);
    }
    assert(inline_from_id("turbo_3", 7) == -1 && !tip_from_id("exhaust_tip_x", 13));
    // flow: towards the low side, never past level, a turbo muffler flows less than a straight-through
    double m = inline_move(kStraight, 30, 10, 10, 10, 1.0 / 60);   // 3.0 vs 1.0
    assert(m > 0 && m <= 10 + 1e-9);
    assert(inline_move(kTurboMuffler, 30, 10, 10, 10, 1.0 / 60) < m);
    assert(inline_move(kStraight, 10, 10, 30, 10, 1.0 / 60) < 0);
    assert(near(inline_move(kHeaderEqual, 30, 10, 10, 10, 10.0), 10.0));   // capped at evening out (2.0 each side)
    assert(inline_move(kCat, 0, 0, 10, 10, 1.0 / 60) == 0);
    // sound: stock pipes keep the preset; a turbo muffler is quieter than a straight-through; a cat calms pops
    SoundTune base{60, 120, 0.4, 0.5, 0.2, 0.3, 0.0, 1.0};
    Chain none;
    SoundTune s0 = apply(base, none, -1);
    assert(near(s0.muffler, 0.5) && near(s0.crackle, 0.2));
    Chain turbo_m; turbo_m.count[kTurboMuffler] = 1; turbo_m.outlets = 1;
    Chain straight; straight.count[kStraight] = 1; straight.outlets = 1;
    Chain cat = straight; cat.count[kCat] = 1;
    Chain unequal = straight; unequal.count[kHeaderUnequal] = 1;
    assert(apply(base, turbo_m, -1).muffler > apply(base, straight, -1).muffler);
    assert(apply(base, cat, -1).crackle < apply(base, straight, -1).crackle);
    assert(near(apply(base, unequal, -1).uneven, 1.0));
    assert(apply(base, straight, 0.9).crackle >= 0.9);
    assert(near(header_power(true, false), 1.04) && near(header_power(false, true), 1.03) && near(header_power(false, false), 1.0));
    // the definitions are one list of well-formed entries
    std::string e = entries(true);
    int depth = 0, objects = 0;
    for (char c : e) { if (c == '{' || c == '[') ++depth; if (c == '}' || c == ']') --depth; assert(depth >= 0); if (c == '{' && depth == 1) ++objects; }
    assert(depth == 0 && objects == int(kLookCount) * int(kLayoutCount) + int(kInlineCount) - 2 + int(kIntakeCount) + (kHeaderMax - kHeaderMin + 1));
    // race headers are exhaust manifolds: inside the part (but the flange bolts' heads and port rims, 2 mm), the
    // layout matches the definition's, and every primary is the same length
    for (int n = kHeaderMin; n <= kHeaderMax; ++n) {
        assert(avolook::header_cells_x(n) == header_len(n) && avolook::header_cells_z(n) == header_depth(n) && avolook::header_out_z_cell(n) == header_out_z(n) &&
               avolook::header_cells_y(n) == kHeaderDrop + 1);
        avolook::HeaderLayout L = avolook::header_layout(n);
        assert(int(L.paths.size()) == n);
        double l0 = avolook::path_length(L.paths[0]);
        for (auto& p : L.paths) {
            double l = avolook::path_length(p);
            if (std::abs(l - l0) > 0.006) { std::printf("header %d-1: primary %.3f m vs %.3f m\n", n, l, l0); assert(false); }
        }
        for (int look = 0; look < 3; ++look) {
            pdmesh::Mesh m = avolook::header(n, look);
            for (auto& v : m.v)
                assert(v.pos[0] > -0.042 && v.pos[0] < (header_len(n) - 1) * 0.08 + 0.042 && v.pos[1] > -kHeaderDrop * 0.08 - 0.042 && v.pos[1] < 0.042 && v.pos[2] > -0.046 &&
                       v.pos[2] < header_depth(n) * 0.08 - 0.038);
        }
    }
    assert(e.find("header_collector_equal") == std::string::npos && e.find("\"id\": \"header_4_1\"") != std::string::npos);
    assert(header_from_id("header_6_1", 10) == 6 && header_from_id("header_1_1", 10) == 1 && header_from_id("header_4_2", 10) == 0 && header_from_id("header_7_1", 10) == 0);
    {
        std::string h = header_entry(4, "x");
        assert(h.find("\"class\": \"exhaust_manifold\"") != std::string::npos && h.find("\"mateable_port_types\": 64") != std::string::npos);
        assert(h.find("{\"pos\": [0, 0, 0], \"dir\": 4, \"gender\": 1, \"type\": \"port\"}") != std::string::npos);   // the one male port
        assert(h.find("{\"id\": \"exhaust\", \"pos\": [9, -1, 0], \"direction\": 1, \"type\": \"gas\", \"size\": 1}") != std::string::npos);
        assert(h.find("stretch") == std::string::npos && h.find("\"gender\": 2") == std::string::npos);
        assert(header_entry(6, "x").find("\"pos\": [13, -1, 1], \"direction\": 1") != std::string::npos);
        int sub = 0, lay = 0;
        assert(role_of("header_1_1", 10, "exhaust_manifold", 16, &sub, &lay) == kInlinePart && sub == kHeaderEqual);
    }
    // the walk: engine 1 (node 0 air, node 1 exhaust) -> header 2 -> junction 3 -> turbo 4 hot side -> cat 5 -> muffler 6 -> dual tip 7
    //   and junction 3 -> stock vent 8; the air side: engine node 0 -> turbo 4 compressor (node 1)
    std::vector<Part> parts = {{1, kEngine}, {2, kInlinePart, kHeaderUnequal}, {3, kPass}, {4, kTurbo}, {5, kInlinePart, kCat},
                               {6, kInlinePart, kStraight}, {7, kTip, kRace, kDual}, {8, kVent}};
    std::vector<Link> links = {{1, 1, 2, 0}, {2, 1, 3, 0}, {3, 1, 4, kTurboHotIn}, {4, kTurboHotOut, 5, 0}, {5, 1, 6, 0}, {6, 1, 7, 0}, {3, 2, 8, 0},
                               {1, 0, 4, 1}};
    Walk ex = walk(parts, links, 1, 1);
    assert(!ex.intake && ex.chain.turbos == 1 && ex.chain.has(kCat) && ex.chain.has(kStraight) && ex.chain.has(kHeaderUnequal));
    assert(ex.outlets.size() == 2 && ex.chain.tips[kRace] == 1 && ex.chain.any());
    Walk air = walk(parts, links, 1, 0);
    assert(air.intake && air.outlets.empty());
    int sub = 0, lay = 0;
    assert(role_of("turbocharger_6", 14, "gas_pump_centrifugal", 20, &sub, &lay) == kTurbo);
    assert(role_of("turbo_air_filter", 16, "gas_vent", 8, &sub, &lay) == kAirFilter);
    assert(role_of("exhaust_tip_classic_quad", 24, "gas_vent", 8, &sub, &lay) == kTip && sub == kClassic && lay == kQuad);
    assert(role_of("gas_vent_a", 10, "gas_vent", 8, &sub, &lay) == kVent);
    assert(node_side("exhaust", 7) == 1 && node_side("air", 3) == -1 && node_side("", 0) == 0);
    Chain stock; stock.outlets = 2; stock.turbos = 1;
    assert(!stock.any());
    // intake parts
    for (int k = 0; k < kIntakeCount; ++k) {
        assert(intake_from_id(kIntakeIds[k], std::strlen(kIntakeIds[k])) == k);
        fits(exmesh::intake_part(k), intake_box(k), kIntakeIds[k]);
        assert(role_of(kIntakeIds[k], std::strlen(kIntakeIds[k]), "gas_vent", 8, &sub, &lay) == kIntakePart);
    }
    int stack[kIntakeCount] = {1, 0, 0}, itb[kIntakeCount] = {0, 1, 0}, nothing[kIntakeCount] = {};
    assert(near(intake_power(stack, 6000), 1.05) && near(intake_power(stack, 2000), 0.98) && intake_power(stack, 4000) > 1.0);
    assert(near(intake_power(itb, 2000), 1.04) && near(intake_power(nothing, 6000), 1.0));
    { std::vector<Part> p2 = {{1, kEngine}, {2, kIntakePart, kCarb}}; std::vector<Link> l2 = {{1, 0, 2, 0}};
      Walk w2 = walk(p2, l2, 1, 0); assert(w2.intake && w2.outlets.empty()); }
    std::printf("exhaust_logic ok\n");
    return 0;
}
