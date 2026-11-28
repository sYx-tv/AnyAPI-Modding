#include "../avo_gear_logic.h"
#include "../avo_gear_mesh.h"
#include "anyapi_asset_json.h"
#include <cassert>
#include <cmath>
#include <string>
using namespace avogear;
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e; }

int main() {
    assert(kSizeCount == 3 && size_of_id("avo_gear_trailing_m", 19) == 1 && size_of_id("avo_gear_trailing", 17) < 0 && size_of_id("moto_fork", 9) < 0);
    for (const Size& s : kSizes) {
        Layout l = layout(s);
        // The arm reaches the game's axle at mid travel, and within 3 cm (scaled) at both ends of the travel.
        for (double t : {0.0, 0.5, 1.0}) {
            double y = l.tyre_top.y - s.travel * t;
            Vec3 end = arm_end(l, arm_angle(l, y));
            assert(near(end.y, y, 1e-9));
            assert(std::abs(end.z - l.tyre_top.z) < (t == 0.5 ? 1e-9 : 0.03 * s.scale));
        }
        // The wheel clears the leg and the mount block (also at full steering lock: the assembly turns together).
        assert(l.tyre_top.z - s.radius > l.leg_z + l.leg_r);
        assert(l.tyre_top.y + s.radius < -0.04 || l.tyre_top.z - s.radius > 0.12);   // clear of the plate, fully compressed
        assert(l.tyre_top.y + s.radius < 0.04);                                         // and of the hull above it
        assert(l.fork_x > s.width / 2 + 0.01);
        assert(l.arm > s.travel);
        // Frames: no steering keeps the leg in place; steering turns the axle about the leg.
        Frames f0 = frames(s, l.tyre_top.y - s.travel / 2, 0, 0), f1 = frames(s, l.tyre_top.y - s.travel / 2, rad(30), 0);
        assert(near(f0.steer.m[11], 0) && finite(f0.tyre) && finite(f1.tyre));
        Vec3 a0 = column(f0.tyre, 3), a1 = column(f1.tyre, 3);
        assert(near(std::hypot(a0.x, a0.z - l.leg_z), std::hypot(a1.x, a1.z - l.leg_z), 1e-9) && std::abs(a1.x) > 0.05);
        assert(near(a0.y, a1.y, 1e-9));
        // The shock gets shorter as the suspension compresses.
        auto shock_len = [&](double y) { Vec3 lug = shock_lug(l, arm_angle(l, y)); return length(lug - l.shock_top); };
        assert(shock_len(l.tyre_top.y) < shock_len(l.tyre_top.y - s.travel));
        // Meshes build.
        assert(!gear_base(s).v.empty() && !gear_steer(s).v.empty() && !gear_arm(s).v.empty() && !gear_shock(s).v.empty() && !gear_tyre(s).v.empty());
    }
    assert(kSizes[0].radius < kSizes[1].radius && kSizes[1].radius < kSizes[2].radius);
    // Definitions: valid JSON, three wheel parts, links in the game's order, 12 moving meshes each.
    MeshPaths m;
    for (int i = 0; i < kSizeCount; ++i) { m.base[i] = "b" + std::to_string(i); m.steer[i] = "s"; m.arm[i] = "a"; m.shock[i] = "k"; m.tyre[i] = "t"; }
    m.hidden = "h";
    std::string json = definitions_json(m);
    assetjson::Value v = assetjson::Parser(json).parse();
    auto& defs = v.get("definitions").array;
    assert(defs.size() == 3);
    for (int i = 0; i < 3; ++i) {
        auto& d = defs[i];
        assert(d.get("id").text == kSizes[i].id && d.get("class").text == "wheel");
        auto& nodes = d.get("logic_nodes").array;
        assert(nodes.size() == 3 && nodes[0].get("id").text == "steering_pos" && nodes[1].get("id").text == "steering_neg" && nodes[2].get("id").text == "brake");
        assert(d.get("meshes_dynamic").array.size() == 12);
        bool torque = false;
        for (auto& s : d.get("surfaces").array) torque |= s.get("type").text == "torque";
        assert(torque);
        assert(d.get("bounds_override_min").array[1].number < d.get("bounds_override_max").array[1].number);
    }
    std::printf("avo_gear_logic: ok\n");
}
