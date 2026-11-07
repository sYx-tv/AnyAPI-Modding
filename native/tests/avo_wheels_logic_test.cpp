// AnyVehicleOverhaul wheels: catalogue, item entry rewrite, grip numbers and meshes.
#include "../avo_wheels_logic.h"
#include "../wheel_mesh.h"
#include <cassert>
#include <cmath>
#include <set>
#include <string>
using namespace avowheels;

// A stock-shaped wheel entry (field names as the game's item file uses them; values made up).
static const char* kStock = R"({"id": "stock_wheel", "name": "Car Wheel", "description": "x", "class": "wheel", "mesh_file": "meshes/components/a.mesh", "meshes_dynamic": [{"id": "damaged_wheel", "mesh_file": "b.mesh"}], "flags": ["is_component_accessory"], "floor": {"mesh_transform": {"t": [0, 0.19, 0]}, "physics_min": [-0.3, 0.0, -0.3], "physics_max": [0.3, 0.2, 0.3]}, "patterns": {"patterns": [{"id": "camo"}]}, "spawn_probability": 0.1, "loot_group": "mechanical", "category": "accessory"})";

int main() {
    // Catalogue: three kinds in three styles, unique stable ids.
    auto all = catalogue();
    assert(all.size() == 9);
    std::set<std::string> ids;
    for (auto& i : all) ids.insert(i.id);
    assert(ids.size() == 9);
    assert(item_id(kRace, kRace6) == "avo_wheel_race_6spoke");
    assert(item_id(kDragSlick, kDrag5) == "avo_wheel_drag_slick_5spoke");
    assert(item_id(kDragFront, kMesh) == "avo_wheel_drag_front_mesh");

    // Grip: race 1.5x, drag slick 2.5x, front runner stock, anything else not ours.
    assert(grip_of("avo_wheel_race_mesh") == 1.5);
    assert(grip_of("avo_wheel_drag_slick_6spoke") == 2.5);
    assert(grip_of("avo_wheel_drag_front_5spoke") == 1.0);
    assert(grip_of("wheel_rim_20") == 0 && grip_of("") == 0 && grip_of("avo_wheel_nope") == 0);
    Grip g = scaled({1.0, 2.0}, 2.5);
    assert(g.grip_friction == 2.5 && g.circle_mu == 5.0);
    assert(kGripFriction == 232 && kFrictionCircleMu == 248);

    // Entry rewrite keeps the stock fields it does not own and sets ours.
    for (auto& it : all) {
        std::string e = make_entry(kStock, it, "meshes/components/" + it.id + ".mesh", "meshes/components/" + it.id + "_rim.mesh");
        assert(!e.empty());
        assetjson::Value v = assetjson::Parser(e).parse();
        assert(v.get("id").text == it.id);
        assert(v.get("class").text == "wheel");
        assert(v.get("mesh_file").text == "meshes/components/" + it.id + ".mesh");
        assert(v.get("meshes_dynamic").array.size() == 2);
        assert(v.get("meshes_dynamic").array[1].get("mesh_file").text == "meshes/components/" + it.id + "_rim.mesh");
        assert(v.get("flags").array.size() == 1);
        assert(v.get("patterns").get("patterns").array.empty());
        assert(v.get("tech_tier").kind != 0);
        const double r = tyre(it.kind).radius + 0.01;
        assert(std::abs(v.get("floor").get("physics_max").array[0].number - r) < 1e-3);
        assert(v.get("name").text.find(style_name(it.style)) != std::string::npos);
    }
    assert(make_entry("not json", all[0], "a", "b").empty());

    // Meshes: well formed, the tyre at its radius, the inboard face at the stock wheels' y.
    for (int s = 0; s < kStyles; ++s)
        for (int k = 0; k < kKinds; ++k)
            for (int bare = 0; bare < 2; ++bare) {
                auto m = wheelmesh::game_wheel(s, k, !bare);
                assert(!m.v.empty() && m.i.size() % 3 == 0);
                for (uint32_t i : m.i) assert(i < m.v.size());
                double rmax = 0, ymin = 1e9, ymax = -1e9;
                for (auto& v : m.v) {
                    rmax = std::max(rmax, std::hypot(double(v.pos[0]), double(v.pos[2])));
                    ymin = std::min(ymin, double(v.pos[1])); ymax = std::max(ymax, double(v.pos[1]));
                    double n = std::sqrt(double(v.normal[0]) * v.normal[0] + double(v.normal[1]) * v.normal[1] + double(v.normal[2]) * v.normal[2]);
                    assert(std::abs(n - 1) < 1e-3);
                }
                const Tyre t = tyre(Kind(k));
                if (!bare) {
                    assert(std::abs(rmax - t.radius) < 0.005);
                    assert(std::abs(ymin - wheelmesh::kInboardY) < 0.003);
                    assert(ymax - ymin < t.width + 0.06);
                } else assert(rmax < t.radius - 0.05);
                auto file = pdmesh::encode(m, "t");
                assert(file.size() > 100);
            }
    return 0;
}
