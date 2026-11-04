// Built engines: id mapping, the definitions file rewrite, and the rating bonus.
#include "../built_engine_logic.h"
#include "../enginesound_params.h"
#include "../turbo_health.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace built;
    bool b = false;
    assert(stock_id("built_v_engine", &b) == "v_engine" && b);
    assert(stock_id("v_engine", &b) == "v_engine" && !b);
    assert(stock_id("built_gearbox", &b) == "built_gearbox" && !b);
    assert(stock_id("built_", &b) == "built_" && !b);

    // A made-up file in the game's layout (not the game's data): an engine, a gearbox, odd spacing, a nested object
    // and an escaped quote, and one engine without a spawn_probability.
    const char* file = R"({
  "definitions": [
    {
      "id": "gearbox",
      "name": "Gearbox",
      "tech_tier": 1
    },
    {
      "id":"engine",
      "name": "Small \"Inline\" Engine",
      "description": "stock",
      "class": "engine",
      "zones": [{"bounds_max": [1, 1, 1]}, {"bounds_max": [2, 2, 2]}],
      "mesh_static": {"mesh_path": "meshes/components/x.mesh"},
      "tech_tier": 2,
      "spawn_probability": 0.3,
      "loot_group": "mechanical"
    },
    {
      "id": "opposed_engine_c",
      "name": "Large Boxer",
      "tech_tier": 3
    }
  ]
})";
    int n = 0;
    std::string out = definitions_file(file, &n);
    assert(n == 2);
    assert(out.find("\"id\": \"built_engine\"") != std::string::npos || out.find("\"id\":\"built_engine\"") != std::string::npos);
    assert(out.find("\"Built Small \\\"Inline\\\" Engine\"") != std::string::npos);
    assert(out.find("gearbox") == std::string::npos);
    assert(out.find("\"mesh_path\": \"meshes/components/x.mesh\"") != std::string::npos);
    assert(out.find("\"loot_group\": \"mechanical\"") != std::string::npos);
    assert(out.find("0.3") == std::string::npos && out.find("\"tech_tier\": 2") == std::string::npos);
    assert(out.find("\"id\": \"built_opposed_engine_c\"") != std::string::npos);
    // The output reads back: two objects, each with the five rewritten keys once.
    auto objs = definition_objects(out);
    assert(objs.size() == 2);
    for (auto o : objs) {
        int seen = 0;
        for (const Member& m : members(o)) {
            if (m.key == "id" || m.key == "name" || m.key == "description" || m.key == "tech_tier" || m.key == "spawn_probability") ++seen;
            if (m.key == "tech_tier") assert(o.substr(m.value_begin, m.value_end - m.value_begin) == "4");
        }
        assert(seen == 5);
    }
    assert(definitions_file("{\"definitions\": []}").empty() && definitions_file("garbage").empty() && definitions_file("{\"definitions\": [{\"id\": ").empty());

    // Ratings and sound follow the stock engine.
    auto stock = turbo::engine_rating("v_engine_b", 10, 9), built_r = turbo::engine_rating("built_v_engine_b", 16, 9);
    assert(stock.known && built_r.known && built_r.cylinders == stock.cylinders && std::abs(built_r.factor - stock.factor - kBuiltBonus) < 1e-9);
    assert(!turbo::engine_rating("built_gearbox", 13, 0).known);
    auto k = enginesound::engine_kind("built_opposed_engine", 20);
    assert(k.known && k.layout == enginesound::synth::kBoxer && k.size == enginesound::kSmall && !k.rotary);
    std::puts("built_engine_logic_test: ok");
}
