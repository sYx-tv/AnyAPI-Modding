#pragma once
// AnyBattery pure logic: the Battery Pack definition, its capacity from its size, the charge kept between ticks and
// the saved charge file. No game or Windows dependencies, so tests/anybattery_logic_test.cpp runs it on any platform.
//
// Game facts this is built on (Anymaker 0.1.27, read from game.gcl and rom/data):
//   - A part's cell is 0.08 m. A zone with bounds_max [1, 1, 1] is 2 x 2 x 2 cells; stretching adds whole cells per
//     axis (m_extend_size, saved and replicated).
//   - The stock battery holder (class "battery") keeps its charge in a vehicle_electric_content, energy and capacity
//     in joules, which the electric network fills (alternators, solar panels) and drains (lights, motors...). The
//     holder copies both from the battery item in its slot; with no item, create and build_accessory set the
//     capacity to 0 and tick leaves the content alone. Damage (a crash) empties it on the next tick.
//   - The stock batteries hold 0.5 kWh per cell: battery_a is 2 x 2 x 2 cells and 4 kWh, battery_b 2 x 2 x 1 and 2 kWh.
//     The liquid tank holds 0.5 L per cell and stretches on all three axes ("mode_x/y/z": "stretch").
// The Battery Pack is that holder without a slot: shaped and stretched like the liquid tank, its capacity is its cells
// times the energy per cell, and its charge is the mod's own, saved in a file in the world's working folder.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace anybattery {

constexpr const char* kDefinitionId = "anybattery_pack";
constexpr const char* kMeshName = "anybattery_pack";        // rom/meshes/components/anybattery_pack.mesh
constexpr int kBaseCells = 2;                                // per axis, unstretched (zone 0..1)
constexpr int kMaxExtend = 14;                               // cells added per axis: up to 16 x 16 x 16 cells (1.28 m)
constexpr double kJoulesPerKwh = 3.6e6;
constexpr double kStockKwhPerCell = 0.5;                     // the stock batteries' energy density

inline bool is_pack_id(const char* id, size_t length) {
    size_t n = std::char_traits<char>::length(kDefinitionId);
    return id && length == n && std::char_traits<char>::compare(id, kDefinitionId, n) == 0;
}

// Cells of a pack from its extend size (cells added past 2 on each axis). Negative or silly values count as 0.
inline int axis_cells(int32_t extend) {
    int32_t e = extend < 0 ? -extend : extend;
    return kBaseCells + (e > kMaxExtend ? kMaxExtend : e);
}
inline int64_t cells(int32_t ex, int32_t ey, int32_t ez) { return int64_t(axis_cells(ex)) * axis_cells(ey) * axis_cells(ez); }

// ------------------------------------------------------------------------------------------- settings
enum DamageMode { kDrainAll = 0, kDrainHalf = 1, kNoDrain = 2, kDamageModes = 3 };
struct Settings {
    double kwh_per_cell = kStockKwhPerCell;   // 0.1 to 5
    DamageMode damage = kDrainAll;            // what a crash does to the charge (stock: empties it)
    double drain_percent_per_hour = 0;        // self-discharge, % of the charge per hour played (0 to 50)
    double start_percent = 0;                 // charge of a newly placed pack (0 to 100): dead by default
};
inline Settings clamp(Settings s) {
    auto c = [](double v, double lo, double hi, double fallback) { return std::isfinite(v) ? (v < lo ? lo : v > hi ? hi : v) : fallback; };
    s.kwh_per_cell = c(s.kwh_per_cell, 0.1, 5.0, kStockKwhPerCell);
    s.drain_percent_per_hour = c(s.drain_percent_per_hour, 0, 50, 0);
    s.start_percent = c(s.start_percent, 0, 100, 0);
    if (s.damage < kDrainAll || s.damage >= kDamageModes) s.damage = kDrainAll;
    return s;
}
inline double capacity_joules(int64_t cell_count, const Settings& s) { return double(cell_count) * s.kwh_per_cell * kJoulesPerKwh; }

// ------------------------------------------------------------------------------------------- charge between ticks
// What the mod remembers of one pack. The content is the game's (energy, capacity in joules); the network changes its
// energy between our ticks, and the game may zero its capacity (build_accessory with no item), which would also
// clamp the energy to 0. So: when the capacity is still ours, the content's energy is the truth; when it is not,
// ours is, and both are put back.
struct Pack {
    bool seen = false;
    double energy = 0;          // joules, as of the end of our last tick
};
struct Content { double energy = 0, capacity = 0; };

inline double clamp_energy(double e, double capacity) { return std::isfinite(e) ? (e < 0 ? 0 : e > capacity ? capacity : e) : 0; }

// Before the game's tick. `saved` is the charge from the save file, if any. Returns the energy before the tick, which
// after_tick needs when a crash drained it.
inline double before_tick(Pack& p, Content& c, double capacity, const Settings& s, const double* saved) {
    if (!p.seen) {
        p.seen = true;
        if (saved) p.energy = *saved;
        else if (c.capacity == capacity) p.energy = c.energy;      // already ours: a pack split off another vehicle
        else p.energy = capacity * s.start_percent / 100.0;        // newly placed
    } else if (c.capacity == capacity) {
        p.energy = c.energy;                                       // the network charged or used it
    }
    c.capacity = capacity;
    c.energy = p.energy = clamp_energy(p.energy, capacity);
    return c.energy;
}
// After the game's tick. `damaged` says the game's tick drained it for a crash; dt is the tick's length in seconds.
inline void after_tick(Pack& p, Content& c, bool damaged, double before, const Settings& s, double dt) {
    if (damaged) c.energy = s.damage == kNoDrain ? before : s.damage == kDrainHalf ? before * 0.5 : 0.0;
    if (s.drain_percent_per_hour > 0 && dt > 0 && dt < 1) c.energy *= 1.0 - s.drain_percent_per_hour / 100.0 * dt / 3600.0;
    c.energy = p.energy = clamp_energy(c.energy, c.capacity);
}
inline double charge_percent(const Content& c) { return c.capacity > 0 ? 100.0 * c.energy / c.capacity : 0.0; }

// ------------------------------------------------------------------------------------------- saved charge
// Kept in a small file inside the game's working folder, which the game copies into the save slot when it saves and
// back when it loads (as AnyEngineParts' turbo wear). Keyed by the vehicle's id and the part's id, both saved by the
// game. A pack that moves to another vehicle (split or weld) gets new ids; it keeps the charge it has in the game.
// File: {"version": 1, "charge": {"12:34": 1440000.0}}  (joules)
struct Key {
    int32_t vehicle = 0, component = 0;
    bool operator<(const Key& o) const { return vehicle != o.vehicle ? vehicle < o.vehicle : component < o.component; }
};
using ChargeMap = std::map<Key, double>;

inline std::string encode_charge(const ChargeMap& charge) {
    std::string out = "{\"version\": 1, \"charge\": {";
    bool first = true;
    char line[96];
    for (auto& [k, v] : charge) {
        if (!std::isfinite(v)) continue;
        std::snprintf(line, sizeof line, "%s\n  \"%d:%d\": %.1f", first ? "" : ",", int(k.vehicle), int(k.component), v < 0 ? 0.0 : v);
        out += line;
        first = false;
    }
    return out + "\n}}\n";
}
// Reads every "<vehicle>:<component>": joules pair; anything else is skipped. Negative values count as 0.
inline ChargeMap decode_charge(const std::string& text) {
    ChargeMap out;
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        size_t end = text.find('"', i + 1);
        if (end == std::string::npos) break;
        std::string key = text.substr(i + 1, end - i - 1);
        i = end + 1;
        char* p = nullptr;
        long vehicle = std::strtol(key.c_str(), &p, 10);
        if (key.empty() || !p || p == key.c_str() || *p != ':') continue;
        const char* second = p + 1;
        long component = std::strtol(second, &p, 10);
        if (!p || p == second || *p) continue;
        size_t colon = text.find_first_not_of(" \t\r\n", i);
        if (colon == std::string::npos || text[colon] != ':') continue;
        const char* start = text.c_str() + colon + 1;
        char* stop = nullptr;
        double value = std::strtod(start, &stop);
        if (stop == start || !std::isfinite(value)) continue;
        out[Key{int32_t(vehicle), int32_t(component)}] = value < 0 ? 0 : value;
    }
    return out;
}

// ------------------------------------------------------------------------------------------- definition
inline std::string description() {
    return "Battery pack. Stretch it to size like a liquid tank: 0.5 kWh per cell (set in Mod Settings), from 4 kWh. "
           "Placed empty: charge it from an alternator or solar panel on the same network. Fit electric ports on its "
           "bottom and sides. Damage empties it. Outputs charge_percent.";
}
inline std::string mesh_path() { return std::string("meshes/components/") + kMeshName + ".mesh"; }

// The liquid tank's shape and stretch (cell for cell) with the stock battery's class, density and data outputs, and
// electric ports on the bottom and the four sides (the top carries the terminals, as on the stock battery). No
// interaction zone: there is no battery item to fit.
inline std::string definition_json(const std::string& mesh) {
    std::string ext = std::to_string(kMaxExtend);
    return std::string("{\n  \"definitions\": [\n    {\n") +
        "      \"id\": \"" + kDefinitionId + "\",\n" +
        "      \"name\": \"Battery Pack\",\n" +
        "      \"description\": \"" + description() + "\",\n" +
        "      \"class\": \"battery\",\n" +
        "      \"zones\": [{\"bounds_max\": [1, 1, 1]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n" +
        "        {\"size\": [2, 2], \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [0, 0, 1], \"size\": [2, 2], \"dir\": 5, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [1, 0, 0], \"size\": [2, 2], \"dir\": 1, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"size\": [2, 2], \"dir\": 4, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"size\": [2, 2], \"dir\": 2, \"gender\": 2, \"type\": \"port\"}\n" +
        "      ],\n" +
        "      \"logic_nodes\": [],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"mode_x\": \"stretch\",\n" +
        "      \"mode_y\": \"stretch\",\n" +
        "      \"mode_z\": \"stretch\",\n" +
        "      \"interval\": [1, 1, 1],\n" +
        "      \"ext_max\": [" + ext + ", " + ext + ", " + ext + "],\n" +
        "      \"center_stretch\": [0.5, 0.5, 0.5],\n" +
        "      \"category\": \"electric\",\n" +
        "      \"tags_generated\": \"port,\",\n" +
        "      \"inspection_geometry\": [{\"p1\": [1.0, 1.0, 1.0], \"radius\": 0.375}],\n" +
        "      \"density\": 1200.0,\n" +
        "      \"inv_default_stack_size\": 20,\n" +
        "      \"mateable_port_types\": 3,\n" +
        "      \"data_descriptors\": [\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"energy_capacity\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"energy_store\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"electric_consumption\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"charge_percent\"}\n" +
        "      ],\n" +
        "      \"tech_tier\": 2,\n" +                 // from the first bunker, like the small stock battery
        "      \"spawn_probability\": 0.15,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }\n  ]\n}\n";
}

}  // namespace anybattery
