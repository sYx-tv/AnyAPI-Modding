#pragma once
// Built engines: forged-internals versions of the game's nine engines (inline, V and boxer, each small, medium and
// large), as their own parts. Pure logic: no game access, tested in tests/built_engine_logic_test.cpp.
//
// Each one is the stock engine's own definition: same blocks, ports, stretch, power, fuel and sound. Only the id
// ("built_" + the stock id), the name ("Built " + the stock name), the description and the loot change, so the mod
// ships none of the game's data: it reads the stock entries from the game's rom/data/vehicle_component_definitions.json
// when the game starts and writes the built ones next to the Rotary's definition.
//
// What "built" buys: the engine takes kBuiltBonus more boost factor before it knocks (turbo_health.h engine_rating),
// (about 13 psi more). EngineSound and the turbos treat it as the stock engine it came from.
//
// Getting it: tech_tier 4 loot (after the 3rd bunker), the stock engine's loot group, spawn 0.075.
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace built {

constexpr const char* kPrefix = "built_";
constexpr size_t kPrefixLength = 6;
constexpr double kBuiltBonus = 1.0;          // added to the stock engine's boost rating
constexpr int kTechTier = 4;
constexpr double kSpawnProbability = 0.075;
inline constexpr const char* kStockIds[10] = {"engine", "engine_block_b", "engine_block_c",
                                              "v_engine", "v_engine_b", "v_engine_c",
                                              "opposed_engine", "opposed_engine_b", "opposed_engine_c",
                                              "rotary_engine"};   // the mod's own Rotary (Nate 2026-11-04): built from rotary_logic.h's entry

inline bool is_stock_engine(std::string_view id) {
    for (const char* s : kStockIds) if (id == s) return true;
    return false;
}
// The stock id a part id stands for: "built_v_engine" -> "v_engine" (built = true); any other id comes back as is.
inline std::string_view stock_id(std::string_view id, bool* is_built = nullptr) {
    bool b = id.size() > kPrefixLength && id.compare(0, kPrefixLength, kPrefix) == 0 && is_stock_engine(id.substr(kPrefixLength));
    if (is_built) *is_built = b;
    return b ? id.substr(kPrefixLength) : id;
}

// ------------------------------------------------------------------------------------- a small JSON reader
// Just enough to walk the definitions file: spans of values and an object's top-level keys. It never decodes
// strings beyond comparing them, and keeps every other byte as the game wrote it.
inline size_t skip_ws(std::string_view s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}
// The index one past the value that starts at i, or npos when it is malformed.
inline size_t value_end(std::string_view s, size_t i) {
    i = skip_ws(s, i);
    if (i >= s.size()) return std::string_view::npos;
    char c = s[i];
    if (c == '"') {
        for (++i; i < s.size(); ++i) {
            if (s[i] == '\\') { ++i; continue; }
            if (s[i] == '"') return i + 1;
        }
        return std::string_view::npos;
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        for (; i < s.size(); ++i) {
            char d = s[i];
            if (d == '"') { size_t e = value_end(s, i); if (e == std::string_view::npos) return e; i = e - 1; continue; }
            if (d == '{' || d == '[') ++depth;
            else if (d == '}' || d == ']') { if (--depth == 0) return i + 1; }
        }
        return std::string_view::npos;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') ++i;
    return i;
}
struct Member { std::string_view key; size_t value_begin = 0, value_end = 0; };   // offsets into the object text
// The top-level members of the object `o` (which starts with '{'); empty when it is malformed.
inline std::vector<Member> members(std::string_view o) {
    std::vector<Member> out;
    size_t i = skip_ws(o, 0);
    if (i >= o.size() || o[i] != '{') return {};
    i = skip_ws(o, i + 1);
    if (i < o.size() && o[i] == '}') return out;
    while (i < o.size()) {
        if (o[i] != '"') return {};
        size_t ke = value_end(o, i);
        if (ke == std::string_view::npos) return {};
        Member m;
        m.key = o.substr(i + 1, ke - i - 2);
        i = skip_ws(o, ke);
        if (i >= o.size() || o[i] != ':') return {};
        m.value_begin = skip_ws(o, i + 1);
        m.value_end = value_end(o, m.value_begin);
        if (m.value_end == std::string_view::npos) return {};
        out.push_back(m);
        i = skip_ws(o, m.value_end);
        if (i < o.size() && o[i] == ',') { i = skip_ws(o, i + 1); continue; }
        if (i < o.size() && o[i] == '}') return out;
        return {};
    }
    return {};
}
// The objects of the definitions array (each as its text) in a definitions file.
inline std::vector<std::string_view> definition_objects(std::string_view file) {
    std::vector<std::string_view> out;
    std::vector<Member> top = members(file.substr(skip_ws(file, 0)));
    std::string_view body = file.substr(skip_ws(file, 0));
    for (const Member& m : top) {
        if (m.key != "definitions") continue;
        std::string_view a = body.substr(m.value_begin, m.value_end - m.value_begin);
        if (a.empty() || a[0] != '[') return {};
        size_t i = skip_ws(a, 1);
        while (i < a.size() && a[i] != ']') {
            size_t e = value_end(a, i);
            if (e == std::string_view::npos) return {};
            if (a[i] == '{') out.push_back(a.substr(i, e - i));
            i = skip_ws(a, e);
            if (i < a.size() && a[i] == ',') i = skip_ws(a, i + 1);
        }
    }
    return out;
}
inline std::string_view string_value(std::string_view o, const Member& m) {
    std::string_view v = o.substr(m.value_begin, m.value_end - m.value_begin);
    return v.size() >= 2 && v.front() == '"' ? v.substr(1, v.size() - 2) : std::string_view{};
}

// ------------------------------------------------------------------------------------- built definitions
inline std::string description(std::string_view stock_name, bool rotary = false) {
    if (rotary)
        return "A " + std::string(stock_name) + " built for boost: ported housings, stronger apex and side seals, a lower "
               "compression ratio. Everything else is the stock Rotary (power, fuel, size, sound); it takes about 1.0 more "
               "boost factor (about 13 psi) before it knocks.";
    return "A " + std::string(stock_name) + " built for boost: forged pistons and rods, a stronger head gasket and "
           "studs, a lower compression ratio. Everything else is the stock engine (power, fuel, size, sound); it takes "
           "about 1.0 more boost factor (about 13 psi) before it knocks.";
}
// The stock definition `o` rewritten as its built version, or empty when `o` is not one of the nine engines.
inline std::string built_definition(std::string_view o) {
    std::vector<Member> ms = members(o);
    if (ms.empty()) return {};
    std::string_view id, name;
    for (const Member& m : ms) {
        if (m.key == "id") id = string_value(o, m);
        else if (m.key == "name") name = string_value(o, m);
    }
    if (!is_stock_engine(id)) return {};
    // The name stays as the file spells it (already escaped JSON text).
    std::string stock_name = name.empty() ? std::string(id) : std::string(name);
    struct Set { const char* key; std::string value; bool done; };
    Set sets[] = {
        {"id", "\"" + std::string(kPrefix) + std::string(id) + "\"", false},
        {"name", "\"Built " + stock_name + "\"", false},
        {"description", "\"" + description(stock_name, id == "rotary_engine") + "\"", false},
        {"tech_tier", std::to_string(kTechTier), false},
        {"spawn_probability", "0.075", false},
    };
    std::string out;
    size_t at = 0;
    for (const Member& m : ms) {
        for (Set& s : sets) {
            if (m.key != s.key) continue;
            out.append(o.substr(at, m.value_begin - at));
            out += s.value;
            at = m.value_end;
            s.done = true;
        }
    }
    size_t close = o.rfind('}');
    out.append(o.substr(at, close - at));
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r' || out.back() == '\t')) out.pop_back();
    for (Set& s : sets) if (!s.done) out += std::string(",\n      \"") + s.key + "\": " + s.value;
    out += "\n    }";
    return out;
}
// The built engines file made from the game's definitions file: {"definitions": [...]} with one entry per stock engine
// found (normally 9), or empty when none is found.
// `extra_file` (another definitions file, the Rotary's) adds its engines too.
inline std::string definitions_file(std::string_view stock_file, int* count = nullptr, std::string_view extra_file = {}) {
    std::string entries;
    int n = 0;
    for (std::string_view file : {stock_file, extra_file})
        for (std::string_view o : definition_objects(file)) {
            std::string b = built_definition(o);
            if (b.empty()) continue;
            if (n++) entries += ",\n";
            entries += "    " + b;
        }
    if (count) *count = n;
    return n ? "{\n  \"definitions\": [\n" + entries + "\n  ]\n}\n" : std::string{};
}

}  // namespace built
