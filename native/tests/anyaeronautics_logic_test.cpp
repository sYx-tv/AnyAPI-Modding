#include "../anyaeronautics_logic.h"
#include "../anyaeronautics_mesh.h"
#include <cstring>
#include <vector>
#include <cassert>
#include <cmath>
#include <string>
using namespace anyaeronautics;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e; }

int main() {
    // Ids and channels.
    assert(kSizeCount == 2 && size_of_id("anyaero_nozzle_cone", 19) == 0 && size_of_id("anyaero_nozzle_fighter", 22) == 1);
    assert(size_of_id("anyaero_nozzle_cone", 10) < 0 && size_of_id("jet_engine_exhaust", 18) < 0);
    assert(channel_index("thrust", 6) == kThrust && channel_index("spool", 5) == kSpool && channel_index("throttle", 8) < 0);

    // Thrust from fuel burn: the first test's full-throttle flow gives tens of kN; the fighter gets more from the same fuel.
    double cone = fuel_thrust_newtons(kSizes[0].tsfc, 0.00176), fighter = fuel_thrust_newtons(kSizes[1].tsfc, 0.00176);
    assert(cone > 40000 && cone < 55000 && fighter > cone && near(fighter / cone, 0.10 / 0.08, 1e-9));
    assert(near(fuel_thrust_newtons(0.1, 0.00032) / cone, 0.00032 / 0.00176, 1e-9));   // proportional to fuel
    assert(fuel_thrust_newtons(0.1, 0) == 0 && fuel_thrust_newtons(0.1, -1) == 0 && fuel_thrust_newtons(0.1, NAN) == 0 && fuel_thrust_newtons(0, 1) == 0);
    assert(fuel_thrust_newtons(0.1, 10) == kMaxNewtons);
    // Spool lag: rises toward the target over seconds, falls faster, never overshoots.
    double t = 0;
    for (int i = 0; i < 60; ++i) t = lag_thrust(t, 10000, 1.0 / 60);   // one second
    assert(t > 4000 && t < 6000);
    for (int i = 0; i < 600; ++i) t = lag_thrust(t, 10000, 1.0 / 60);
    assert(t > 9950 && t <= 10000);
    double down = lag_thrust(10000, 0, 0.5), up = lag_thrust(0, 10000, 0.5);
    assert(10000 - down > up);
    assert(lag_thrust(5, 7, 0) == 5 && lag_thrust(NAN, 7, 10) > 0 && lag_thrust(5, 7, 100) < 7);

    // Rev limiter: a cut at full speed with throttle open keeps the last flow; a real stop does not.
    double last = 0;
    assert(near(limiter_fuel(0.00248, 0.99, 1, last), 0.00248) && near(last, 0.00248));
    assert(near(limiter_fuel(0, 0.99, 1, last), 0.00248));
    assert(limiter_fuel(0, 0.99, 0, last) == 0 && limiter_fuel(0, 0.5, 1, last) == 0 && limiter_fuel(NAN, 0.3, 1, last) == 0);
    double fresh = 0; assert(limiter_fuel(0, 0.99, 1, fresh) == 0);

    // Direction: from the nozzle toward the engine, unit length; refuses degenerate or absurd spans.
    Vec3 d;
    assert(push_direction({0, 0, 0}, {0, 0, 0.4}, d) && near(d.z, 1) && near(d.x, 0));
    assert(push_direction({1, 2, 3}, {1, 2, 2}, d) && near(d.z, -1));
    assert(!push_direction({0, 0, 0}, {0, 0, 0}, d) && !push_direction({0, 0, 0}, {0, 0, 100}, d) && !push_direction({0, 0, 0}, {NAN, 0, 0}, d));

    // Definitions: three copies of the first stock jet exhaust with our ids, the stock outputs kept plus ours.
    std::string stock = R"({"definitions": [
      {"id": "engine_a", "name": "Engine", "class": "engine", "data_descriptors": []},
      {"id": "jet_exhaust_a", "name": "Jet Exhaust", "description": "x", "class": "jet_engine_exhaust", "mass": 12.5,
       "mesh_static": {"mesh_path": "meshes/components/jet_exhaust_a.mesh"},
       "surfaces": [{"pos":[-3,-1,3],"size":[1,3]},{"pos":[-1,3,3],"size":[3,1],"dir":3},{"pos":[3,-1,3],"size":[1,3],"dir":1},{"pos":[-2,0,-2],"size":[3,1]},{"pos":[2,0,-2],"size":[3,1],"dir":1},{"pos":[0,0,3],"dir":5,"gender":1,"type":"jet_engine"}],
       "data_descriptors": [{"type": "type_f64_output", "name": "temperature"}]}
    ]})";
    std::string error, id;
    std::string json = definitions_json(stock, {mesh_path(kSizes[0]), ""}, &error, &id);
    assert(!json.empty() && id == "jet_exhaust_a");
    for (const Size& s : kSizes) assert(json.find(std::string("\"id\": \"") + s.id + "\"") != std::string::npos);
    size_t count = 0;
    for (size_t at = json.find("\"class\": \"jet_engine_exhaust\""); at != std::string::npos; at = json.find("\"class\": \"jet_engine_exhaust\"", at + 1)) ++count;
    assert(count == 2);
    assert(json.find("anyaero_nozzle_cone_a.mesh") != std::string::npos && json.find("jet_exhaust_a.mesh") != std::string::npos);   // ours, then stock as fallback
    assert(json.find("\"mass\": 12.5") != std::string::npos);
    assert(json.find("\"name\": \"temperature\"") != std::string::npos && json.find("\"name\": \"thrust\"") != std::string::npos && json.find("\"name\": \"spool\"") != std::string::npos);
    assert(json.find("\"id\": \"jet_exhaust_a\"") == std::string::npos && json.find("\"name\": \"Jet Exhaust\"") == std::string::npos);
    assetjson::Value parsed = assetjson::Parser(json).parse();
    assert(parsed.get("definitions").array.size() == 2);
    assert(parsed.get("definitions").array[0].get("mesh_static").get("mesh_path").text == "meshes/components/anyaero_nozzle_cone_a.mesh");
    assert(parsed.get("definitions").array[1].get("mesh_static").get("mesh_path").text == "meshes/components/jet_exhaust_a.mesh");

    // Data ports: the left and right faces at the engine end take ports; the top, the rear faces and the jet face don't.
    {
        const assetjson::Value& def = parsed.get("definitions").array[0];
        assert(def.get("mateable_port_types").number == 2);
        const auto& f = def.get("surfaces").array;
        assert(f.size() == 6);
        assert(f[0].get("type").text == "port" && f[0].get("gender").number == 2 && f[2].get("type").text == "port");
        assert(f[1].get("type").kind == 'n' && f[3].get("type").kind == 'n' && f[4].get("type").kind == 'n');
        assert(f[5].get("type").text == "jet_engine" && f[5].get("gender").number == 1);
        assert(f[0].get("size").array.size() == 2);
    }
    // New readouts.
    assert(channel_index("thrust_percent", 14) == kThrustPercent && channel_index("thrust_to_weight", 16) == kThrustToWeight);
    assert(near(thrust_percent(fuel_thrust_newtons(0.08, kFullFuelFlow), 0.08, 0), 100) && near(thrust_percent(fuel_thrust_newtons(0.08, kFullFuelFlow) / 2, 0.08, 0), 50));
    assert(near(thrust_percent(fuel_thrust_newtons(0.08, 0.004), 0.08, 0.005), 80) && thrust_percent(0, 0.08, 0) == 0 && thrust_percent(1e9, 0.08, 0) == 100);
    assert(near(thrust_to_weight(9.81 * 1000, 1000), 1) && thrust_to_weight(100, 0) == 0 && thrust_to_weight(NAN, 10) == 0);
    assert(json.find("\"name\": \"thrust_to_weight\"") != std::string::npos);

    assert(definitions_json(R"({"definitions": [{"id": "a", "class": "engine"}]})", {}, &error).empty() && !error.empty());
    assert(definitions_json("not json", {}, &error).empty());

    // Meshes: a fake stock header; both nozzles stay inside the stock exhaust's footprint and point straight back.
    std::vector<uint8_t> fake;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); fake.insert(fake.end(), c, c + n); };
    uint32_t u;
    put("mesh", 4); u = 5; put(&u, 4); u = 1; put(&u, 4);
    const char* nm = "jet_exhaust_a"; u = uint32_t(std::strlen(nm)); put(&u, 4); put(nm, u);
    std::vector<uint8_t> header(88, 7); put(header.data(), header.size());
    double bounds[6]{}; put(bounds, sizeof bounds);
    MeshVertex v[3]{}; u = sizeof v; put(&u, 4); put(v, sizeof v);
    uint32_t idx[3] = {0, 1, 2}; u = sizeof idx; put(&u, 4); put(idx, sizeof idx);
    uint8_t trailer[8] = {1, 2, 3, 4, 5, 6, 7, 8}; put(trailer, 8);
    for (Look look : {Look::Cone, Look::Fighter}) {
        std::vector<uint8_t> mesh = build_nozzle_mesh(fake, look, "anyaero_nozzle_x_a");
        MeshLayout m;
        assert(!mesh.empty() && parse_mesh(mesh, m));
        double b[6]; std::memcpy(b, mesh.data() + m.bounds, sizeof b);
        assert(b[0] >= -0.2801 && b[3] <= 0.2801 && b[1] >= -0.2801 && b[4] <= 0.2801 && b[2] >= -0.411 && b[2] < -0.39 && near(b[5], 0.28, 1e-6));
        assert(std::memcmp(mesh.data() + mesh.size() - 8, trailer, 8) == 0 && mesh[16 + 18] == 7);
    }
    assert(build_nozzle_mesh({}, Look::Cone, "x").empty());
    return 0;
}
