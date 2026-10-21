#pragma once
// Turbo and air filter wear, kept in a small file inside the game's working folder. The game copies that folder
// into the save slot when it saves and back out when it loads, so the file travels with each save without touching
// the save format (component save data is read by position; adding to it broke saves before).
//
// Keyed by the vehicle's id and the part's id, both saved by the game and stable across loads. A part that moves to
// another vehicle (split or weld) gets new ids and starts fresh. Pure: no game calls, tested in turbo_logic_test.cpp.
//
// File: {"version": 1, "wear": {"t12:34": 0.973, "f12:40": 0.8}}  (t = turbo condition, f = air filter condition)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace turbo {

struct WearKey {
    char kind = 't';            // 't' turbo, 'f' air filter
    int32_t vehicle = 0, component = 0;
    bool operator<(const WearKey& o) const {
        if (kind != o.kind) return kind < o.kind;
        if (vehicle != o.vehicle) return vehicle < o.vehicle;
        return component < o.component;
    }
};
using WearMap = std::map<WearKey, double>;

inline std::string encode_wear(const WearMap& wear) {
    std::string out = "{\"version\": 1, \"wear\": {";
    bool first = true;
    char line[96];
    for (auto& [k, v] : wear) {
        if (!std::isfinite(v)) continue;
        std::snprintf(line, sizeof line, "%s\n  \"%c%d:%d\": %.6f", first ? "" : ",", k.kind, int(k.vehicle), int(k.component), v);
        out += line;
        first = false;
    }
    return out + "\n}}\n";
}

// Reads every "<t|f><vehicle>:<component>": value pair; anything else is skipped. Values are clamped to 0-1.
inline WearMap decode_wear(const std::string& text) {
    WearMap out;
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        size_t end = text.find('"', i + 1);
        if (end == std::string::npos) break;
        std::string key = text.substr(i + 1, end - i - 1);
        i = end + 1;
        if (key.size() < 4 || (key[0] != 't' && key[0] != 'f')) continue;
        char* p = nullptr;
        long vehicle = std::strtol(key.c_str() + 1, &p, 10);
        if (!p || *p != ':') continue;
        long component = std::strtol(p + 1, &p, 10);
        if (!p || *p) continue;
        size_t colon = text.find_first_not_of(" \t\r\n", i);
        if (colon == std::string::npos || text[colon] != ':') continue;
        const char* start = text.c_str() + colon + 1;
        char* stop = nullptr;
        double value = std::strtod(start, &stop);
        if (stop == start || !std::isfinite(value)) continue;
        out[WearKey{key[0], int32_t(vehicle), int32_t(component)}] = value < 0 ? 0 : value > 1 ? 1 : value;
    }
    return out;
}

}  // namespace turbo
