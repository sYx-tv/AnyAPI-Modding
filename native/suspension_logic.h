#pragma once
// AnySuspension pure logic: the part definitions, the spring tuning and the saved rest positions.
// No game or Windows dependencies, so tests/suspension_logic_test.cpp runs it on any platform.
//
// Game facts this is built on (Anymaker 0.1.25, read from game.gcl, game.exe and rom/data):
//   - Parts are JSON definitions (rom/data/vehicle_component_definitions.json). A hinge is a pair: a pin
//     (class hinge_pin) and a knuckle (class hinge_knuckle) that name each other ("hinge_definition_id_other") and
//     join two bodies of one vehicle with a Jolt hinge. Its swing range comes from "hinge_constraint_limit_min/max"
//     (stock pin 0 to pi, knuckle -pi to 0). Jolt treats limits of -pi to pi as no limits: a free bearing.
//   - A rail (class rail) and a rail slider (class rail_slider) join two bodies with a Jolt slider constraint that
//     runs end to end on the rail. The game's slider has a motor: physics.constraint.slider.set_motor(frequency,
//     damping, target) puts it in position mode (game.exe: state 2, spring settings, target clamped to the limits).
//     The ballscrew slider uses it to hold position; the suspension uses it as a spring.
//   - A slider constraint's position is measured from where the slider sat when the constraint was made (the game
//     rebuilds it on every load and reconnect), and its limits are the rail's ends from there. So "position - lower
//     limit" is where the slider sits along the rail, which stays the same across loads: the saved rest position.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace suspension {

constexpr const char* kBearingPin = "bearing_pin";
constexpr const char* kBearingRing = "bearing_ring";
constexpr const char* kSuspensionRail = "suspension_rail";
constexpr const char* kSuspensionSlider = "suspension_slider";
constexpr double kPi = 3.141592653589793;

inline bool same_id(const char* id, size_t length, const char* want) {
    size_t n = std::char_traits<char>::length(want);
    return id && length == n && std::char_traits<char>::compare(id, want, n) == 0;
}
inline bool is_suspension_slider_id(const char* id, size_t length) { return same_id(id, length, kSuspensionSlider); }

// ------------------------------------------------------------------------------------------- spring tuning
// Each Suspension Slider has Stiffness and Damping rows in the Properties Tool, 0 to 1 in steps of 0.1 like the
// stock wheel's. Stiffness sets the ride frequency (1 to 4 Hz) and damping the fraction of critical (0.1 to 1).
constexpr double kDefaultStiffness = 0.5, kDefaultDamping = 0.5;          // 2.5 Hz, 0.55 of critical
constexpr double kMinFrequency = 1.0, kMaxFrequency = 4.0;
constexpr double kMinDamping = 0.1, kMaxDamping = 1.0;
constexpr double kGravity = 9.81;

inline double clamp(double v, double lo, double hi) { return std::isfinite(v) ? (v < lo ? lo : v > hi ? hi : v) : lo; }
inline double frequency_from_factor(double stiffness) { return kMinFrequency + (kMaxFrequency - kMinFrequency) * clamp(stiffness, 0, 1); }
inline double damping_from_factor(double damping) { return kMinDamping + (kMaxDamping - kMinDamping) * clamp(damping, 0, 1); }

// The spring is set by stiffness (N/m) and damping (N s/m) rather than by Jolt's frequency mode: Jolt measures that
// frequency against the constraint's effective mass, which is mostly the light wheel side, so a heavy body would sit
// on the bump stop. Here the load is the heavier body's mass shared between the springs that carry it.
struct Spring { double stiffness = 0, damping = 0; };
inline Spring spring_for(double sprung_mass, double frequency, double damping_ratio) {
    Spring s;
    double m = clamp(sprung_mass, 1.0, 1.0e6);
    double w = 2.0 * kPi * clamp(frequency, kMinFrequency, kMaxFrequency);
    s.stiffness = m * w * w;
    s.damping = 2.0 * clamp(damping_ratio, kMinDamping, kMaxDamping) * std::sqrt(s.stiffness * m);
    return s;
}
// How far the load pushes the slider off its rest position: g / w^2 (6 cm at 2 Hz).
inline double static_sag(double frequency) {
    double w = 2.0 * kPi * clamp(frequency, kMinFrequency, kMaxFrequency);
    return kGravity / (w * w);
}
// The heavier body is the sprung one; its weight is shared between every suspension slider that holds it up.
inline double sprung_share(double mass_a, double mass_b, int springs_on_heavier) {
    double heavy = std::isfinite(mass_a) && std::isfinite(mass_b) ? (mass_a > mass_b ? mass_a : mass_b) : 0;
    return heavy / double(springs_on_heavier > 0 ? springs_on_heavier : 1);
}

// The motor target (constraint coordinates) for a slider resting at `rail_rest` along the rail (rail coordinates),
// kept inside the rail's ends.
inline double target_from_rest(double rail_rest, double limit_min, double limit_max) {
    double t = rail_rest + limit_min;
    if (!(limit_max >= limit_min)) return t;
    return t < limit_min ? limit_min : t > limit_max ? limit_max : t;
}
inline double rest_from_position(double position, double limit_min) { return position - limit_min; }

// ------------------------------------------------------------------------------------------- saved slider settings
// Kept in a small file in the game's working folder, which the game copies into each save and back out on load
// (the way AnyEngineParts keeps turbo wear; adding to the game's own save data breaks it). Keyed by the slider's
// vehicle id and component id, both saved by the game. Rest is metres along the rail; stiffness and damping are the
// Properties Tool values. File: {"version": 2, "sliders": {"12:34": [0.150000, 0.5000, 0.5000]}}
struct RestKey {
    int32_t vehicle = 0, component = 0;
    bool operator<(const RestKey& o) const { return vehicle != o.vehicle ? vehicle < o.vehicle : component < o.component; }
};
struct SliderSave { double rest = 0, stiffness = kDefaultStiffness, damping = kDefaultDamping; };
using SaveMap = std::map<RestKey, SliderSave>;

inline std::string encode_sliders(const SaveMap& sliders) {
    std::string out = "{\"version\": 2, \"sliders\": {";
    bool first = true;
    char line[128];
    for (auto& [k, v] : sliders) {
        if (!std::isfinite(v.rest)) continue;
        std::snprintf(line, sizeof line, "%s\n  \"%d:%d\": [%.6f, %.4f, %.4f]", first ? "" : ",", int(k.vehicle), int(k.component), v.rest,
                      clamp(v.stiffness, 0, 1), clamp(v.damping, 0, 1));
        out += line;
        first = false;
    }
    return out + "\n}}\n";
}

// Reads every "<vehicle>:<component>": [rest, stiffness, damping] entry; anything else is skipped.
inline SaveMap decode_sliders(const std::string& text) {
    SaveMap out;
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        size_t end = text.find('"', i + 1);
        if (end == std::string::npos) break;
        std::string key = text.substr(i + 1, end - i - 1);
        i = end + 1;
        if (key.empty() || !(key[0] == '-' || (key[0] >= '0' && key[0] <= '9'))) continue;
        char* p = nullptr;
        long vehicle = std::strtol(key.c_str(), &p, 10);
        if (!p || *p != ':') continue;
        long component = std::strtol(p + 1, &p, 10);
        if (!p || *p) continue;
        size_t colon = text.find_first_not_of(" \t\r\n", i);
        if (colon == std::string::npos || text[colon] != ':') continue;
        size_t open = text.find_first_not_of(" \t\r\n", colon + 1);
        if (open == std::string::npos || text[open] != '[') continue;
        double v[3];
        const char* at = text.c_str() + open + 1;
        bool ok = true;
        for (int n = 0; n < 3 && ok; ++n) {
            char* stop = nullptr;
            v[n] = std::strtod(at, &stop);
            ok = stop != at && std::isfinite(v[n]);
            at = stop;
            while (ok && n < 2 && (*at == ' ' || *at == ',' || *at == '\n' || *at == '\r' || *at == '\t')) ++at;
        }
        if (!ok) continue;
        out[RestKey{int32_t(vehicle), int32_t(component)}] = SliderSave{v[0], clamp(v[1], 0, 1), clamp(v[2], 0, 1)};
    }
    return out;
}

// ------------------------------------------------------------------------------------------- definitions
// Stock meshes for now: the bearing looks like the hinge, the suspension like the rail.
inline std::string definitions_json() {
    std::string pi = "3.141592653589793";
    return std::string("{\n  \"definitions\": [\n") +
        // Bearing pin: the hinge pin with no swing limits.
        "    {\"id\": \"" + kBearingPin + "\", \"name\": \"Bearing\", "
        "\"description\": \"Place a bearing ring on it in the editor to start a body that spins freely all the way round, "
        "or interact to connect or release a ring within one voxel. Like the hinge pin with no stops.\", "
        "\"class\": \"hinge_pin\", \"zones\": [{\"is_physics\": false}], "
        "\"interaction_zones\": [{\"bounds_min\": [-0.04, -0.04, -0.04], \"bounds_max\": [0.04, 0.04, 0.04]}], "
        "\"surfaces\": [{\"dir\": 2}], \"logic_nodes\": [], \"mesh_static\": {\"mesh_path\": \"meshes/components/hinge_pin_a.mesh\"}, "
        "\"meshes_dynamic\": [], \"category\": \"connector\", \"tags_generated\": \"interactable,\", \"inspection_geometry\": [], "
        "\"data_descriptors\": [], \"tech_tier\": 2, \"spawn_probability\": 0.5, \"loot_group\": \"mechanical\", "
        "\"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1], [1, 0, 0, 0, 0, 1, 0, -1, 0], [1, 0, 0, 0, -1, 0, 0, 0, -1], [1, 0, 0, 0, 0, -1, 0, 1, 0]], "
        "\"force_emitter_element_rotor\": {\"blades\": []}, \"hinge_definition_id_other\": \"" + kBearingRing + "\", "
        "\"hinge_direction\": 1, \"hinge_constraint_limit_min\": -" + pi + ", \"hinge_constraint_limit_max\": " + pi + "},\n" +
        // Bearing ring: the hinge knuckle, paired with the bearing pin, no limits.
        "    {\"id\": \"" + kBearingRing + "\", \"name\": \"Bearing Ring\", "
        "\"description\": \"Pairs with a bearing: place one on the other in the editor to start a freely spinning body. "
        "Connect and release by interacting with the bearing.\", "
        "\"class\": \"hinge_knuckle\", \"zones\": [{\"bounds_min\": [0, 0, -1], \"is_physics\": false}], \"interaction_zones\": [], "
        "\"surfaces\": [{\"dir\": 2}], \"logic_nodes\": [], \"mesh_static\": {\"mesh_path\": \"meshes/components/hinge_knuckle_a.mesh\"}, "
        "\"meshes_dynamic\": [], \"category\": \"connector\", \"inspection_geometry\": [], \"data_descriptors\": [], \"tech_tier\": 2, "
        "\"spawn_probability\": 0.5, \"loot_group\": \"mechanical\", \"constraint_position\": [0, 0, -1], "
        "\"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1], [-1, 0, 0, 0, -1, 0, 0, 0, 1]], "
        "\"force_emitter_element_rotor\": {\"blades\": []}, \"hinge_definition_id_other\": \"" + kBearingPin + "\", "
        "\"hinge_direction\": 1, \"hinge_constraint_limit_min\": -" + pi + ", \"hinge_constraint_limit_max\": " + pi + "},\n" +
        // Suspension rail: the rail, paired with the suspension slider.
        "    {\"id\": \"" + kSuspensionRail + "\", \"name\": \"Suspension Rail\", "
        "\"description\": \"Place a suspension slider on it in the editor to start a sprung body. Stretch it to set the "
        "travel. The slider springs back to where you placed it; set its stiffness and damping with the Properties Tool.\", "
        "\"class\": \"rail\", \"zones\": [{\"is_physics\": false}], \"interaction_zones\": [], \"surfaces\": [{\"dir\": 2}], "
        "\"logic_nodes\": [], \"mesh_static\": {\"mesh_path\": \"meshes/components/linear_rail_a.mesh\"}, \"meshes_dynamic\": [], "
        "\"mode_z\": \"stretch\", \"interval\": [0, 0, 1], \"ext_max\": [10, 10, 32], \"category\": \"connector\", "
        "\"inspection_geometry\": [], \"inv_default_stack_size\": 10, \"data_descriptors\": [], \"tech_tier\": 2, "
        "\"spawn_probability\": 0.5, \"loot_group\": \"mechanical\", \"constraint_orientations\": [], "
        "\"force_emitter_element_rotor\": {\"blades\": []}, \"rail_definition_id_other\": \"" + kSuspensionSlider + "\"},\n" +
        // Suspension slider: the rail slider with a spring.
        "    {\"id\": \"" + kSuspensionSlider + "\", \"name\": \"Suspension Slider\", "
        "\"description\": \"Pairs with a suspension rail: place one on the other in the editor to start a sprung body. "
        "Springs back to where you placed it along the rail. Stiffness and damping in the Properties Tool. Interact to attach to a rail within one voxel, or to release.\", "
        "\"class\": \"rail_slider\", \"zones\": [{\"is_physics\": false}], "
        "\"interaction_zones\": [{\"bounds_min\": [-0.04, -0.04, -0.04], \"bounds_max\": [0.04, 0.04, 0.04]}], "
        "\"surfaces\": [{\"dir\": 3}, {}, {\"dir\": 1}], \"logic_nodes\": [], "
        "\"mesh_static\": {\"mesh_path\": \"meshes/components/linear_rail_b.mesh\"}, \"meshes_dynamic\": [], \"category\": \"connector\", "
        "\"tags_generated\": \"interactable,\", \"inspection_geometry\": [], \"data_descriptors\": [], \"tech_tier\": 2, "
        "\"spawn_probability\": 0.5, \"loot_group\": \"mechanical\", \"constraint_orientations\": [], "
        "\"force_emitter_element_rotor\": {\"blades\": []}, \"rail_slider_definition_id_other\": \"" + kSuspensionRail + "\", "
        "\"rail_slider_is_parent\": true}\n" +
        "  ]\n}\n";
}

}  // namespace suspension
