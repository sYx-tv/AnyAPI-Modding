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

    // Thrust: spool squared, clamped, never negative or NaN.
    assert(near(thrust_newtons(10000, 1), 10000) && near(thrust_newtons(10000, 0.5), 2500) && near(thrust_newtons(10000, 0), 0));
    assert(near(thrust_newtons(10000, 2), 10000) && near(thrust_newtons(10000, -1), 0) && near(thrust_newtons(10000, NAN), 0) && near(thrust_newtons(-5, 1), 0));

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
       "surfaces": [{"dir": 4, "gender": 1, "type": "jet"}],
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
