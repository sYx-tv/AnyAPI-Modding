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
constexpr const char* kRigidWheelHub = "rigid_wheel_hub";
constexpr const char* kDriveCoupler = "drive_coupler";
constexpr double kPi = 3.141592653589793;

inline bool same_id(const char* id, size_t length, const char* want) {
    size_t n = std::char_traits<char>::length(want);
    return id && length == n && std::char_traits<char>::compare(id, want, n) == 0;
}
inline bool is_suspension_slider_id(const char* id, size_t length) { return same_id(id, length, kSuspensionSlider); }

// What a definition is to this mod, from its id.
enum class PartKind { other, suspension_slider, rigid_wheel_hub, drive_coupler };
inline PartKind part_kind(const char* id, size_t length) {
    if (same_id(id, length, kSuspensionSlider)) return PartKind::suspension_slider;
    if (same_id(id, length, kRigidWheelHub)) return PartKind::rigid_wheel_hub;
    if (same_id(id, length, kDriveCoupler)) return PartKind::drive_coupler;
    return PartKind::other;
}

// ------------------------------------------------------------------------------------------- rigid wheel hub
// A wheel's suspension travel, spring and steering come from code-side tuning (vehicle_element_definition.wheel,
// one global per stock wheel id), not from the JSON. The Rigid Wheel Hub uses a copy of the stock wheel's with the
// travel cut to almost nothing; the wheel stays a wheel (grip, steering, brake, tyres) but rides rigidly on its body.
constexpr double kHubSuspensionLength = 0.02;          // metres; enough for the wheel to keep finding the ground
inline double hub_suspension_length(double stock) {
    return std::isfinite(stock) && stock > 0 && stock < kHubSuspensionLength ? stock : kHubSuspensionLength;
}

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
// Stock meshes for now: the bearing looks like the hinge, the suspension like the rail, the hub like the wheel and
// the coupler like the torque interface.
// with_hub false leaves the hub out (its tuning could not be found, and a wheel without tuning would crash).
inline std::string definitions_json(bool with_hub = true) {
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
        "\"rail_slider_is_parent\": true},\n" +
        // Rigid wheel hub: the stock wheel (same meshes, faces and links); its tuning is swapped in code.
        (with_hub ? std::string("    ") + R"json({"id": )json" + "\"" + kRigidWheelHub + "\"" + R"json(, "name": "Rigid Wheel Hub", "description": "The wheel hub with no suspension: fits the same wheel items, drives from its torque face, steers up to 32 degrees from the steering links and brakes from the brake link. Mount it on a suspension slider for your own suspension, and carry drive across with a drive coupler.", "class": "wheel", "zones": [{"bounds_min": [-2, -1, 0], "bounds_max": [2, 5, 1]}, {"bounds_min": [-2, -1, 4], "bounds_max": [2, 2, 4]}, {"bounds_min": [-2, -1, 2], "bounds_max": [2, 4, 3]}], "interaction_zones": [{"bounds_min": [-0.1, 0.0, 0.36], "bounds_max": [0.1, 0.2, 0.43999999999999995]}], "surfaces": [{"pos": [0, 3, 0], "size": [1, 3], "dir": 4, "gender": 2, "type": "port"}, {"id": "torque", "dir": 4, "type": "torque"}, {"pos": [0, 2, 0], "dir": 4, "gender": 2, "type": "mechanical"}, {"pos": [2, 0, 0], "dir": 4, "gender": 2, "type": "mechanical"}, {"pos": [-2, 0, 0], "dir": 4, "gender": 2, "type": "mechanical"}, {"pos": [0, 1, 0], "dir": 4, "gender": 2, "type": "port"}], "logic_nodes": [{"id": "steering_pos", "pos": [-2, 0, 0], "direction": 4, "type": "mechanical_in"}, {"id": "steering_neg", "pos": [2, 0, 0], "direction": 4, "type": "mechanical_in"}, {"id": "brake", "pos": [0, 2, 0], "direction": 4, "type": "mechanical_in"}], "mesh_static": {"mesh_path": "meshes/components/wheel_hub_a_base.mesh"}, "meshes_dynamic": [{"path": "meshes/components/wheel_hub_a_pivot.mesh", "pos": [0.0, 0.1, 0.29]}, {"path": "meshes/components/wheel_hub_a_hub.mesh", "pos": [0.0, 0.1, 0.29]}, {"path": "meshes/components/wheel_hub_a_axel.mesh", "preview_rot": [1.0, 0.0, 0.0, 0.0, 0.9410143691718643, -0.3383666015020959, 0.0, 0.3383666015020959, 0.9410143691718643]}, {"path": "meshes/components/wheel_hub_a_arm_upper.mesh", "pos": [0.0, 0.23, -0.01]}, {"path": "meshes/components/wheel_hub_a_arm_lower.mesh", "pos": [0.0, -0.07, -0.01]}, {"path": "meshes/components/wheel_hub_a_piston_body.mesh", "pos": [0.0, 0.4, 0.08], "preview_rot": [1.0, 0.0, 0.0, 0.0, 0.9643004949084248, -0.26481041429552377, 0.0, 0.26481041429552377, 0.9643004949084248]}, {"path": "meshes/components/wheel_hub_a_piston_rod.mesh", "pos": [0.0, 0.0, 0.19334572112937815], "preview_rot": [1.0, 0.0, 0.0, 0.0, 0.9590627745303383, -0.28319356368069093, 0.0, 0.28319356368069093, 0.9590627745303383]}, {"path": "meshes/components/wheel_hub_a_steering_rod_lower.mesh", "pos": [-0.16, 0.0, 0.0], "preview_rot": [0.9983978348009168, -0.02001176238692802, -0.052927240916283966, 3.469446951953614e-18, 0.9353725756816859, -0.35366388656831893, 0.05658412732243231, 0.3530972585970867, 0.9338739542927519]}, {"path": "meshes/components/wheel_hub_a_steering_rod_upper.mesh", "pos": [-0.14, 0.1, 0.27], "preview_rot": [0.9961681854826986, -0.03016191147901514, -0.08209266305852102, 0.00039294106730445867, 0.9401837523456422, -0.3406675174163011, 0.08745737149849309, 0.3393298850988521, 0.9365929944481618]}, {"path": "meshes/components/wheel_hub_a_steering_rod_lower.mesh", "pos": [0.16, 0.0, 0.0], "preview_rot": [0.9996822551623706, 0.009501766319265737, 0.023347487023038505, -0.0003589226234638672, 0.9315051221906235, -0.3637280282122645, -0.025204362480079717, 0.3636040755677054, 0.931212551645717]}, {"path": "meshes/components/wheel_hub_a_steering_rod_upper.mesh", "pos": [0.14, 0.1, 0.27], "preview_rot": [0.9939989241345727, 0.03542335431483986, 0.10349553028221868, 0.0005109523272550748, 0.9446025951661288, -0.32821589865991263, -0.1093886545626721, 0.32629913143387324, 0.9389158636845308]}, {"path": "meshes/components/wheel_hub_a_hub_b.mesh", "pos": [0.0, 0.1, 0.29]}], "bounds_override_min": [-8, -8, 0], "bounds_override_max": [8, 6, 9], "category": "wheel", "tags_generated": "interactable,mechanical,port,torque,", "inspection_geometry": [], "mateable_port_types": 2, "data_descriptors": [{"type": "type_f64_output", "name": "brake_factor"}, {"type": "type_f64_output", "name": "rps"}], "tech_tier": 2, "spawn_probability": 0.5, "loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []}})json" + ",\n" : std::string()) +
        // Drive coupler: the stock torque interface; its tick never drops the connection.
        "    " + R"json({"id": )json" + "\"" + kDriveCoupler + "\"" + R"json(, "name": "Drive Coupler", "description": "A torque interface that stays joined. Connects face to face with another drive coupler or torque interface like the stock one, then keeps the drive joined however far the bodies move, so drive can cross a suspension slider or a bearing.", "class": "interface_torque", "zones": [{}], "interaction_zones": [], "surfaces": [{"dir": 2}, {}, {"dir": 1}, {"dir": 3}, {"dir": 5}, {"id": "torque", "dir": 4, "type": "torque"}], "logic_nodes": [], "mesh_static": {"mesh_path": "meshes/components/interface_torque_b.mesh"}, "meshes_dynamic": [], "category": "torque", "tags_generated": "torque,", "inspection_geometry": [{"shape": 1, "p0": [0.0, 0.0, -0.5], "p1": [0.0, 0.0, 0.5], "radius": 0.009}], "data_descriptors": [], "tech_tier": 2, "spawn_probability": 0.5, "loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []}, "landing_gear_retractable_fold_direction": [1.0, 0.0, 0.0]})json" + "\n" +
        "  ]\n}\n";
}

}  // namespace suspension
