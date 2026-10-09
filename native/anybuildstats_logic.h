#pragma once
// AnyBuildStats tallies. Pure and platform-free so it is unit tested without the game.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace anybuildstats {
struct Tally { std::string name; int count = 0; };

struct Stats {
    int32_t vehicle_id = -1;
    int components = 0, nodes = 0, edges = 0, plates = 0;
    double motor_watts = 0, alternator_watts = 0;
    std::vector<Tally> categories;   // most common first
};

// Readable label from a definition category or class id: "fuel_tank" -> "Fuel tank". Empty stays empty.
inline std::string pretty(const char* raw) {
    std::string out;
    for (const char* p = raw; p && *p && out.size() < 40; ++p) {
        char c = *p == '_' || *p == '-' || *p == '.' ? ' ' : *p;
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');
    return out;
}

inline void count(std::vector<Tally>& tallies, const std::string& name) {
    const std::string key = name.empty() ? "Other" : name;
    for (auto& t : tallies) if (t.name == key) { ++t.count; return; }
    tallies.push_back({key, 1});
}
inline void sort_tallies(std::vector<Tally>& tallies) {
    std::stable_sort(tallies.begin(), tallies.end(), [](const Tally& a, const Tally& b) { return a.count != b.count ? a.count > b.count : a.name < b.name; });
}

// "1,234" style grouping for whole numbers.
inline std::string grouped(long long value) {
    bool negative = value < 0;
    unsigned long long v = negative ? 0ull - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
    std::string digits = std::to_string(v), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return negative ? "-" + out : out;
}

inline std::string power_text(double watts) {
    char buf[32];
    if (watts >= 1e6) snprintf(buf, sizeof buf, "%.2f MW", watts / 1e6);
    else if (watts >= 1000) snprintf(buf, sizeof buf, "%.1f kW", watts / 1000);
    else snprintf(buf, sizeof buf, "%.0f W", watts);
    return buf;
}

// Plain-text spec sheet for the clipboard.
inline std::string sheet(const Stats& s, double mass_kg, bool mass_valid, const double size_m[3], int bodies) {
    std::string out = "Build stats (AnyBuildStats)\n";
    char buf[160];
    if (mass_valid) { out += "Mass: " + grouped(static_cast<long long>(mass_kg + 0.5)) + " kg\n"; }
    snprintf(buf, sizeof buf, "Size: %.2f m (X) x %.2f m (Z) x %.2f m high\n", size_m[0], size_m[2], size_m[1]); out += buf;
    if (bodies > 1) { snprintf(buf, sizeof buf, "Bodies: %d (part counts are for the main body)\n", bodies); out += buf; }
    out += "Components: " + grouped(s.components) + ", edges: " + grouped(s.edges) + ", plates: " + grouped(s.plates) + ", nodes: " + grouped(s.nodes) + "\n";
    if (s.motor_watts > 0) out += "Electric motors: " + power_text(s.motor_watts) + "\n";
    if (s.alternator_watts > 0) out += "Alternators: " + power_text(s.alternator_watts) + "\n";
    if (s.motor_watts > 0 && mass_valid && mass_kg > 0) { snprintf(buf, sizeof buf, "Motor power to weight: %.1f kW/t\n", s.motor_watts / mass_kg); out += buf; }
    for (auto& t : s.categories) out += "  " + t.name + ": " + grouped(t.count) + "\n";
    return out;
}
}  // namespace anybuildstats
