// MCCode Parts tab tests: reading a part definition's channels, names in code, wrapping and the guide's examples.
#undef NDEBUG
#include "../mccode_parts.h"
#include "../mccode_lang.h"
#include <cassert>
#include <iostream>

using namespace mccode;
using namespace mccode::parts;

int main() {
    // A made-up definition in the game's shape: channels sit in "data_descriptors".
    Part p{"sensor_x", "Test Sensor", "Sensors", {}, {}};
    assert(parse_part(R"({"id":"sensor_x","data_descriptors":[{"type":"f64","name":"level","documentation":"How full it is"},)"
                      R"({"type":"bool","name":"is_on"},{"name":""},{"type":"s32"}]})", p));
    assert(p.channels.size() == 2);
    assert(p.channels[0].name == "level" && p.channels[0].type == "fractional" && p.channels[0].doc == "How full it is");
    assert(p.channels[1].type == "bool" && p.channels[1].doc.empty());

    Part none{"plate", "Plate", "Building", {}, {}};
    assert(parse_part(R"({"id":"plate"})", none) && none.channels.empty());
    Part broken{"x", "X", "", {}, {}};
    assert(!parse_part("{not json", broken));

    std::vector<Part> list = {none, p, Part{"a", "Alpha", "Engines", {{"rps", "fractional", ""}}, {}}};
    sort_parts(list);
    assert(list.size() == 2 && list[0].name == "Alpha" && list[1].name == "Test Sensor");

    // The game's own type names carry a direction; "input" must not read as an integer.
    assert(type_label("type_f64_input") == "fractional" && type_dir("type_f64_input") == Dir::Set);
    assert(type_label("type_f64_output") == "fractional" && type_dir("type_f64_output") == Dir::Read);
    assert(type_label("type_bool_input") == "bool" && type_label("type_s32_output") == "integer");
    assert(type_dir("f64") == Dir::Unknown);

    // A mod's definition file, in the shape GpsPart and AnyEngineParts write.
    auto mod = parse_definition_file(R"({"definitions": [)"
        R"({"id": "gps_sensor", "name": "GPS Sensor", "category": "sensor", "data_descriptors": [)"
        R"({"type": "type_f64_output", "name": "north_angle"}, {"type": "type_f64_input", "name": "waypoint_x"}]},)"
        R"({"id": "bracket", "data_descriptors": []}]})", "GpsPart");
    assert(mod.size() == 2 && mod[0].name == "GPS Sensor" && mod[0].source == "GpsPart" && mod[0].channels.size() == 2);
    assert(mod[0].channels[1].dir == Dir::Set && mod[1].name == "bracket" && mod[1].category == "GpsPart");
    assert(parse_definition_file(R"({"wear": 1})", "X").empty() && parse_definition_file("{oops", "X").empty());
    std::vector<Part> merged = {Part{"gps_sensor", "Old", "sensor", {}, {}}, Part{"wheel", "Wheel", "wheels", {{"rps", "fractional", ""}}, {}}};
    merge_mod_parts(merged, mod);
    assert(merged.size() == 3 && merged[0].name == "GPS Sensor");
    sort_parts(merged);
    assert(merged.size() == 2);

    assert(type_label("Integer") == "integer" && type_label("analogue") == "analogue" && type_label("weird") == "weird");
    assert(code_name("Gas Pressure Sensor") == "Gas_Pressure_Sensor");
    assert(code_name("Valve (T)") == "Valve_T");
    assert(code_name("3x3 Turbo") == "Part3x3_Turbo");

    auto w = wrap("one two three four five", 9);
    assert(w.size() == 3 && w[0] == "one two" && w[1] == "three" && w[2] == "four five");
    assert(wrap("", 10).empty());

    // Every guide example compiles once it sits in an on_tick block (the $ marks only place the cursor).
    for (auto& line : setup_guide()) {
        if (!line.code) continue;
        std::string code = line.code;
        code.erase(std::remove(code.begin(), code.end(), '$'), code.end());
        if (code.rfind("on_tick", 0) != 0) code = "on_tick {\n\t" + code + "\n}\n";
        auto c = compile(code);
        if (!c.ok) { std::cerr << "guide example doesn't compile:\n" << code << "\n"; return 1; }
    }
    std::cout << "mccode_parts ok\n";
}
