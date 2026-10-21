#pragma once
// Rotary pure logic: the part definition, rotor count, power and the sound choice.
// No game or Windows dependencies, so tests/rotary_logic_test.cpp runs it on any platform.
//
// Game facts this is built on (Anymaker 0.1.24, read from game.gcl):
//   - An engine (class "engine") burns max_watts = 400 W per cell of its volume (m_volume, set once in create from
//     its zones, stretch included). Its shaft's inertia and friction scale with the same volume (0.02 and 0.04 per
//     cell). It makes no power below 6 rps or above 8000 rpm (133.3 rps).
//   - The client hears it through m_factor_speed = clamp(rps * 60 / 8000, 0, 1) and m_factor_power (load, 0-1),
//     both replicated as one byte, and a replicated sound index (the Properties Tool's "Sound effect" row, 0-2).
//   - The small engine ("engine") is 3 x 6 cells across and 2 cells long, stretched along z in steps of 2 cells
//     ("mode_z": "tile", "interval": [0, 0, 2]). Its mesh is three tiles: <name>_0_0_0 (front cell), _0_0_1 (two
//     middle cells, repeated) and _0_0_2 (back cell).
// The rotary is that engine, cell for cell: same zones, ports, manifolds, gearboxes and engine wheels, with its own
// housing mesh, 25% more power per cell and its own sound. One rotor per 2 cells, 1 to 4 rotors.
#include <cmath>
#include <cstdint>
#include <string>

namespace rotary {

constexpr const char* kDefinitionId = "rotary_engine";
constexpr const char* kMeshBase = "rotary_engine";          // rom/meshes/components/rotary_engine_0_0_N.mesh
constexpr int kMinRotors = 1, kMaxRotors = 4;
constexpr int kCellsPerRotor = 2;                            // the stock stretch interval
constexpr double kPowerFactor = 1.25;                        // power per cell over the small engine
constexpr double kMaxRps = 8000.0 / 60.0;                    // the game's engine speed for factor_speed 1
constexpr double kStartRps = 6.0;                            // the game's engine start speed

inline bool is_rotary_id(const char* id, size_t length) {
    size_t n = std::char_traits<char>::length(kDefinitionId);
    return id && length == n && std::char_traits<char>::compare(id, kDefinitionId, n) == 0;
}

// The engine's volume with the rotary's extra power; volumes are whole cells (36 per rotor), so the result is exact.
inline int32_t boosted_volume(int32_t stock_volume) {
    if (stock_volume <= 0 || stock_volume > (1 << 24)) return stock_volume;
    return int32_t(std::lround(stock_volume * kPowerFactor));
}

// Rotors from the component's extend size along z (cells added past the 2-cell minimum).
inline int rotors_from_extend(int32_t extend_z) {
    int32_t cells = extend_z < 0 ? -extend_z : extend_z;
    int n = 1 + int(cells / kCellsPerRotor);
    return n < kMinRotors ? kMinRotors : n > kMaxRotors ? kMaxRotors : n;
}

// Sound choice: the engine's own "Sound effect" row (replicated sound index). 0 is what a new engine has.
enum Port { kBridgePort = 0, kStreetPort = 1, kPeripheralPort = 2, kPortCount = 3 };
inline Port port_from_index(int32_t index) {
    return index == kStreetPort ? kStreetPort : index == kPeripheralPort ? kPeripheralPort : kBridgePort;
}
inline const char* port_name(Port p) {
    return p == kStreetPort ? "street port" : p == kPeripheralPort ? "peripheral port" : "bridge port";
}

inline double factor_to_rps(double factor) {
    double f = std::isfinite(factor) ? (factor < 0 ? 0 : factor > 1 ? 1 : factor) : 0;
    return f * kMaxRps;
}

// ------------------------------------------------------------------------------------------- definition
inline std::string description() {
    return "Rotary engine. Consumes fuel and air. Produces exhaust. Requires 6 rps to start. Fit air manifold, fuel manifold, "
           "input and output oil manifolds, input and output coolant manifolds. Max temp 160C. Stretch it like the small "
           "engine: one rotor per 2 cells, 1 to 4 rotors, each making 25% more power than the same length of small engine. "
           "Sound effect: 0 bridge port, 1 street port, 2 peripheral port. Compatible with small engine wheel, small gearbox.";
}

inline std::string mesh_path() { return std::string("meshes/components/") + kMeshBase + "_0_0_0.mesh"; }

// The small engine's definition (Anymaker 0.1.24), cell for cell, with the rotary's id, name, mesh, length limit
// and loot settings. ext_max limits the stretch to 3 more rotors (6 cells).
inline std::string definition_json(const std::string& mesh) {
    return std::string("{\n  \"definitions\": [\n    {\n") +
        "      \"id\": \"" + kDefinitionId + "\",\n" +
        "      \"name\": \"Rotary Engine\",\n" +
        "      \"description\": \"" + description() + "\",\n" +
        "      \"class\": \"engine\",\n" +
        "      \"zones\": [{\"bounds_min\": [-1, -1, 0], \"bounds_max\": [1, 4, 1]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n" +
        "        {\"pos\": [-1, 3, 0], \"size\": [2, 2], \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [-1, 2, 1], \"size\": [3, 3], \"dir\": 5, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [1, 3, 0], \"size\": [2, 2], \"dir\": 1, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [-1, 2, 0], \"size\": [3, 3], \"dir\": 4, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [-1, 4, 0], \"size\": [3, 2], \"dir\": 3, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [-1, 0, 0], \"size\": [2, 1], \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [1, 0, 0], \"size\": [2, 1], \"dir\": 1, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"pos\": [0, -1, 0], \"size\": [1, 2], \"dir\": 2, \"gender\": 2, \"type\": \"port\"},\n" +
        "        {\"id\": \"torque_1\", \"dir\": 4, \"gender\": 1, \"type\": \"torque_engine\"},\n" +
        "        {\"id\": \"torque_0\", \"pos\": [0, 0, 1], \"dir\": 5, \"gender\": 1, \"type\": \"torque_engine\"}\n" +
        "      ],\n" +
        "      \"logic_nodes\": [],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"mode_z\": \"tile\",\n" +
        "      \"interval\": [0, 0, 2],\n" +
        "      \"ext_max\": [0, 0, " + std::to_string((kMaxRotors - 1) * kCellsPerRotor) + "],\n" +
        "      \"category\": \"engine\",\n" +
        "      \"tags\": \"port,torque_engine\",\n" +
        "      \"tags_generated\": \"port,torque_engine,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [-1.0, 0.0, 0.0], \"p1\": [1.0, 0.0, 1.0]}, {\"layer\": 1, \"p0\": [-1.0, 3.0, 0.0], \"p1\": [1.0, 3.0, 1.0]}],\n" +
        "      \"density\": 2000.0,\n" +
        "      \"mateable_port_types\": 498,\n" +
        "      \"data_descriptors\": [\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"rps\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"throttle\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"temperature\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"fuel_flow\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"air_flow\"},\n" +
        "        {\"type\": \"type_f64_output\", \"name\": \"torque\"}\n" +
        "      ],\n" +
        "      \"tech_tier\": 4,\n" +       // after the 3rd bunker (turbo_logic.h kLateTier)
        "      \"spawn_probability\": 0.15,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }\n  ]\n}\n";
}

}  // namespace rotary
