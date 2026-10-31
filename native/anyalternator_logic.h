#pragma once
// AnyAlternator pure logic: the three bigger alternators (their definitions and numbers) and what one tick of one of
// them makes and costs. No game or Windows dependencies, so tests/anyalternator_logic_test.cpp runs it anywhere.
//
// Game facts this is built on (Anymaker 0.2.0, unchanged from 0.1.28; read from game.gcl and rom/data):
//   - The stock Alternator (alternator_motor, class "alternator") is 2 x 2 x 3 cells, 1500 W "alternator_wattage",
//     full output at 100 rps ("alternator_max_output_rps"). Its torque face is the single cell (0, 0) on its +z end.
//   - alternator.tick: factor = clamp(|rps| / max_rps, 0, 1); e = min(wattage x factor x dt, room in its own buffer);
//     the buffer gains e x 0.75 and the shaft is braked with torque worth e. So the shaft pays the full e and the
//     network gets three quarters of it, and with full batteries (no room) it neither charges nor drags. Its tooltip
//     and "power" output read m_display_power = e x 0.75 / dt; m_display_rps is the signed rps, written last.
//   - The buffer (m_electric_content, energy and capacity in joules) is sized from the wattage in create.
// The mod controls e through that room: before the game's tick it sets the buffer so the room is exactly the e it
// wants (cut-in, ramp, on/off), then after the tick puts the real energy back plus e x its own efficiency instead of
// e x 0.75. The game's own brake then charges the shaft exactly e, so a less efficient alternator drags harder for
// the same output.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace anyalternator {

constexpr double kStockEfficiency = 0.75;   // hard-coded in alternator.tick
constexpr double kMaxRps = 100;             // full output, as the stock alternator

struct Size {
    const char* id;          // definition id
    const char* mesh;        // rom/meshes/components/<mesh>.mesh
    const char* name;
    int width;               // cells across (x and y)
    int length;              // cells along the shaft (z)
    double output_watts;     // to the network at full output
    double efficiency;       // output / shaft power
    double cut_in_rps;       // no output (and no drag) below this
    double density;
    int tech_tier;
    double spawn_probability;
    double pitch;            // whine playback speed against the stock alternator's
    double wattage() const { return output_watts / efficiency; }   // shaft power at full output
    int shaft_cell() const { return (width - 1) / 2; }             // torque face cell (x = y), as near the middle as a cell gets
    int z_min() const { return 2 - length; }                       // the +z end stays at cell 1, as the stock one
};

// Nate (2026-10-29): 5 / 15 / 40 kW; cut-in, lower efficiency and more weight the bigger they are; Medium and Large
// from the first bunker (tier 2, as the stock alternator), Heavy at tier 3.
constexpr Size kSizes[] = {
    {"anyalternator_medium", "anyalternator_medium", "Medium Alternator", 3, 4, 5000, 0.70, 10, 600, 2, 0.15, 0.80},
    {"anyalternator_large", "anyalternator_large", "Large Alternator", 4, 6, 15000, 0.65, 20, 800, 2, 0.10, 0.65},
    {"anyalternator_heavy", "anyalternator_heavy", "Heavy Alternator", 5, 8, 40000, 0.60, 30, 1000, 3, 0.075, 0.50},
};
constexpr int kSizeCount = int(sizeof(kSizes) / sizeof(kSizes[0]));

// Which of ours a definition id is, or -1.
inline int size_of_id(const char* id, size_t length) {
    for (int i = 0; i < kSizeCount; ++i) {
        size_t n = std::strlen(kSizes[i].id);
        if (id && length == n && std::memcmp(id, kSizes[i].id, n) == 0) return i;
    }
    return -1;
}

// ------------------------------------------------------------------------------------------- one tick
// Fraction of full output at this shaft speed: nothing below the cut-in, then a straight line up to full at 100 rps.
inline double output_factor(const Size& s, double rps) {
    double r = std::fabs(rps);
    if (!std::isfinite(r) || r <= s.cut_in_rps) return 0;
    double f = (r - s.cut_in_rps) / (kMaxRps - s.cut_in_rps);
    return f > 1 ? 1 : f;
}
// The e (joules taken from the shaft this tick) the mod lets the game use: never more than the game's own
// wattage x |rps| / 100 x dt (so the game's min picks ours), nor more than the real room in the buffer.
inline double wanted_energy(const Size& s, double rps, bool enabled, double dt, double energy, double capacity) {
    if (!enabled || !(dt > 0) || !std::isfinite(dt)) return 0;
    double e = s.wattage() * output_factor(s, rps) * dt;
    double room = capacity - energy;
    if (!std::isfinite(room) || room < 0) room = 0;
    return e < room ? e : room;
}

// What the mod remembers between the two halves of a tick, and what it shows.
struct Tick {
    double energy = 0;     // the buffer's real energy before the tick
    double set_to = 0;     // what the buffer was set to (capacity - wanted e)
};
// Before the game's tick: makes the room exactly `wanted`.
inline Tick before_tick(double& energy, double capacity, double wanted) {
    Tick t{energy, capacity - wanted};
    energy = t.set_to;
    return t;
}
struct Result {
    double used = 0;        // e: joules the game took from the shaft
    double output_w = 0;    // power to the network
    double shaft_w = 0;     // power taken from the shaft
};
// After the game's tick: the buffer gained used x 0.75 over what we set; the real energy gets used x efficiency.
inline Result after_tick(const Size& s, const Tick& t, double& energy, double capacity, double dt) {
    Result r;
    double gained = energy - t.set_to;
    r.used = std::isfinite(gained) && gained > 0 ? gained / kStockEfficiency : 0;
    double e = t.energy + r.used * s.efficiency;
    energy = !std::isfinite(e) || e < 0 ? 0 : e > capacity ? capacity : e;
    if (dt > 0 && std::isfinite(dt)) { r.output_w = r.used * s.efficiency / dt; r.shaft_w = r.used / dt; }
    return r;
}
inline double load_percent(const Size& s, const Result& r) { return 100.0 * r.output_w / s.output_watts; }

// ------------------------------------------------------------------------------------------- definitions
inline std::string kw(double watts) {
    char b[32];
    std::snprintf(b, sizeof b, watts >= 10000 ? "%.0f" : "%.1f", watts / 1000.0);
    return b;
}
inline std::string description(const Size& s) {
    char b[400];
    std::snprintf(b, sizeof b,
                  "Driven by torque face. Charges the electric network through an electric port. Starts charging at %.0f rps "
                  "and peaks at %s kW at 100 rps, taking %s kW from the shaft (%.0f%% efficient). Input enabled: 0 stops "
                  "charging and drag. Outputs load_percent and shaft_power.",
                  s.cut_in_rps, kw(s.output_watts).c_str(), kw(s.wattage()).c_str(), s.efficiency * 100);
    return b;
}
inline std::string mesh_path(const Size& s) { return std::string("meshes/components/") + s.mesh + ".mesh"; }

// A port surface. Sizes are per face as the stock alternator lists them: x faces [z, y], y faces [x, z], z faces [x, y].
inline std::string port(int x, int y, int z, int dir, int a, int b) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "{\"pos\": [%d, %d, %d], \"size\": [%d, %d], \"dir\": %d, \"gender\": 2, \"type\": \"port\"}", x, y, z, a, b, dir);
    return buf;
}

// The stock alternator's definition, grown: the same class and ports on every side, the torque face on the +z end at
// (c, c), electric ports on the rest of that end.
inline std::string definition(const Size& s, const std::string& mesh) {
    const int n = s.width, l = s.length, c = s.shaft_cell(), z0 = s.z_min();
    std::string surfaces;
    auto add = [&](const std::string& x) { surfaces += (surfaces.empty() ? "        " : ",\n        ") + x; };
    char t[160];
    std::snprintf(t, sizeof t, "{\"id\": \"torque\", \"pos\": [%d, %d, 1], \"dir\": 5, \"type\": \"torque\"}", c, c);
    add(t);
    add(port(0, 0, z0, 0, l, n));          // -x
    add(port(n - 1, 0, z0, 1, l, n));      // +x
    add(port(0, 0, z0, 2, n, l));          // -y
    add(port(0, n - 1, z0, 3, n, l));      // +y
    add(port(0, 0, z0, 4, n, n));          // back
    if (c > 0) add(port(0, 0, 1, 5, n, c));                     // front, below the shaft row
    if (c > 0) add(port(0, c, 1, 5, c, 1));                     // front, the shaft row left of it
    if (c < n - 1) add(port(c + 1, c, 1, 5, n - 1 - c, 1));     // front, the shaft row right of it
    if (c < n - 1) add(port(0, c + 1, 1, 5, n, n - 1 - c));     // front, above the shaft row
    char head[512];
    std::snprintf(head, sizeof head,
                  "      \"zones\": [{\"bounds_min\": [0, 0, %d], \"bounds_max\": [%d, %d, 1]}],\n"
                  "      \"inspection_geometry\": [{\"shape\": 1, \"p0\": [%d.0, %d.0, %d.0], \"p1\": [%d.0, %d.0, 1.0]}, "
                  "{\"layer\": 1, \"shape\": 1, \"p0\": [%d.0, %d.0, %d.0], \"p1\": [%d.0, %d.0, 1.0]}],\n",
                  z0, n - 1, n - 1, c, c, z0, c, c, c, c, z0, c, c);
    char nums[256];
    std::snprintf(nums, sizeof nums,
                  "      \"density\": %.1f,\n      \"tech_tier\": %d,\n      \"spawn_probability\": %.3f,\n"
                  "      \"alternator_wattage\": %.1f,\n      \"alternator_max_output_rps\": %.1f,\n",
                  s.density, s.tech_tier, s.spawn_probability, s.wattage(), kMaxRps);
    return std::string("    {\n") +
        "      \"id\": \"" + s.id + "\",\n" +
        "      \"name\": \"" + s.name + "\",\n" +
        "      \"description\": \"" + description(s) + "\",\n" +
        "      \"class\": \"alternator\",\n" +
        head +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n" + surfaces + "\n      ],\n" +
        "      \"logic_nodes\": [],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"electric\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"mateable_port_types\": 3,\n" +
        "      \"data_descriptors\": [\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"rps\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"power\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"energy_capacity\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"energy_store\"},\n" +
        "        {\"type\": \"type_f64_input\", \"name\": \"enabled\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"load_percent\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"shaft_power\"}\n" +
        "      ],\n" +
        nums +
        "      \"loot_group\": \"electronics\",\n" +
        "      \"constraint_orientations\": [],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
// `meshes[i]`: the mesh path for size i (ours, or the stock alternator's when ours could not be written).
inline std::string definitions_json(const std::string meshes[kSizeCount]) {
    std::string out = "{\n  \"definitions\": [\n";
    for (int i = 0; i < kSizeCount; ++i) out += definition(kSizes[i], meshes[i]) + (i + 1 < kSizeCount ? ",\n" : "\n");
    return out + "  ]\n}\n";
}

}  // namespace anyalternator
