#include "../anyaeronautics_logic.h"
#include <cassert>
#include <cmath>
#include <string>
using namespace anyaeronautics;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e; }

int main() {
    // Ids and channels.
    assert(size_of_id("anyaero_nozzle_s", 16) == 0 && size_of_id("anyaero_nozzle_m", 16) == 1 && size_of_id("anyaero_nozzle_l", 16) == 2);
    assert(size_of_id("anyaero_nozzle_s", 10) < 0 && size_of_id("jet_engine_exhaust", 18) < 0);
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
    std::string json = definitions_json(stock, &error, &id);
    assert(!json.empty() && id == "jet_exhaust_a");
    for (const Size& s : kSizes) assert(json.find(std::string("\"id\": \"") + s.id + "\"") != std::string::npos);
    size_t count = 0;
    for (size_t at = json.find("\"class\": \"jet_engine_exhaust\""); at != std::string::npos; at = json.find("\"class\": \"jet_engine_exhaust\"", at + 1)) ++count;
    assert(count == 3);
    assert(json.find("jet_exhaust_a.mesh") != std::string::npos && json.find("\"mass\": 12.5") != std::string::npos);
    assert(json.find("\"name\": \"temperature\"") != std::string::npos && json.find("\"name\": \"thrust\"") != std::string::npos && json.find("\"name\": \"spool\"") != std::string::npos);
    assert(json.find("\"id\": \"jet_exhaust_a\"") == std::string::npos && json.find("\"name\": \"Jet Exhaust\"") == std::string::npos);
    assetjson::Value parsed = assetjson::Parser(json).parse();
    assert(parsed.get("definitions").array.size() == 3);

    assert(definitions_json(R"({"definitions": [{"id": "a", "class": "engine"}]})", &error).empty() && !error.empty());
    assert(definitions_json("not json", &error).empty());
    return 0;
}
