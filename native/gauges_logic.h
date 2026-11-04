#pragma once
// AnyVehicleOverhaul gauges and the Boost Controller: small sensor parts of the game's altitude sensor class, added
// with the Dyno's definitions (dyno_mod.cpp owns that class's hooks). Pure logic: no game access, tested in
// tests/dyno_logic_test.cpp.
//
//   Boost Controller  turns the boost up (or down) for every turbo on its vehicle: wire a psi value into target_psi
//                     (a microcontroller, a dial, a switch). Turbos hold it if their exhaust can spin them that fast.
//                     scramble (above 0.5) adds scramble_psi on top, for a push-to-pass button.
//   Boost Gauge       reads the highest boost on its vehicle, in psi, kPa and bar, its target and the peak.
//   Oil Gauge         reads the hottest engine's oil: pressure (psi), temperature, level, and the coolant temperature.
// The game's engines have no oil pump model, so oil pressure is worked out the way a real gauge would read it: it
// rises with RPM, reads high on cold thick oil, sags on hot thin oil and drops off when the sump runs under half full
// (where the game starts to wear the engine).
#include <algorithm>
#include <cmath>
#include <string>

namespace gauges {

enum Kind { kNone = 0, kController = 1, kBoostGauge = 2, kOilGauge = 3 };
constexpr const char* kControllerId = "boost_controller";
constexpr const char* kBoostGaugeId = "boost_gauge";
constexpr const char* kOilGaugeId = "oil_gauge";
inline Kind kind_of_id(const char* id, size_t n) {
    auto is = [&](const char* want) { size_t w = std::char_traits<char>::length(want); return id && n == w && std::char_traits<char>::compare(id, want, n) == 0; };
    return is(kControllerId) ? kController : is(kBoostGaugeId) ? kBoostGauge : is(kOilGaugeId) ? kOilGauge : kNone;
}

constexpr int kMaxSlots = 8;
struct ChannelInfo { const char* name; bool input; };
// Data channels by part; new channels go at the end of a list (saved links refer to them by position).
enum ControllerChannel { kTargetPsi, kScramble, kScramblePsi, kControllerBoostPsi, kTargetSet, kControllerCount };
inline constexpr ChannelInfo kControllerChannels[kControllerCount] = {
    {"target_psi", true},       // boost (psi over the air outside) every turbo on the vehicle holds; 0 = their springs
    {"scramble", true},         // above 0.5: add scramble_psi
    {"scramble_psi", true},     // extra psi while scramble is on (0 or unwired: 5)
    {"boost_psi", false},       // the highest boost on the vehicle now, psi
    {"target_set", false},      // the target it is asking for now, psi (0: none)
};
enum BoostGaugeChannel { kBoostPsi, kBoostKpa, kBoostBar, kBoostTarget, kPeakPsi, kResetPeak, kBoostGaugeCount };
inline constexpr ChannelInfo kBoostGaugeChannels[kBoostGaugeCount] = {
    {"boost_psi", false},       // the highest boost on the vehicle, psi over the air outside
    {"boost_kpa", false},
    {"boost_bar", false},
    {"target_psi", false},      // what the turbos' wastegates are holding for, psi
    {"peak_psi", false},        // highest boost since the last reset
    {"reset_peak", true},       // above 0.5: forget the peak
};
enum OilGaugeChannel { kOilPressurePsi, kOilTemperature, kOilLevel, kCoolantTemperature, kEngineTemperature, kOilGaugeCount };
inline constexpr ChannelInfo kOilGaugeChannels[kOilGaugeCount] = {
    {"oil_pressure_psi", false},
    {"oil_temperature", false},     // C
    {"oil_level", false},           // 0-1 of the sump (under 0.5 the engine wears)
    {"coolant_temperature", false}, // C
    {"engine_temperature", false},  // C (over 160 the engine takes damage)
};
inline int channel_count(Kind k) { return k == kController ? kControllerCount : k == kBoostGauge ? kBoostGaugeCount : k == kOilGauge ? kOilGaugeCount : 0; }
inline const ChannelInfo* channels(Kind k) {
    return k == kController ? kControllerChannels : k == kBoostGauge ? kBoostGaugeChannels : k == kOilGauge ? kOilGaugeChannels : nullptr;
}
inline int channel_index(Kind k, const char* name, size_t length) {
    const ChannelInfo* c = channels(k);
    for (int i = 0; c && i < channel_count(k); ++i)
        if (std::char_traits<char>::length(c[i].name) == length && std::char_traits<char>::compare(c[i].name, name, length) == 0) return i;
    return -1;
}

// The psi a Boost Controller asks for (0: none, the turbos keep their springs).
constexpr double kDefaultScramblePsi = 5.0;
constexpr double kMaxTargetPsi = 100.0;
inline double controller_target(const double slots[kMaxSlots]) {
    double target = std::isfinite(slots[kTargetPsi]) ? std::clamp(slots[kTargetPsi], 0.0, kMaxTargetPsi) : 0.0;
    if (target > 0 && std::isfinite(slots[kScramble]) && slots[kScramble] > 0.5) {
        double extra = std::isfinite(slots[kScramblePsi]) && slots[kScramblePsi] > 0 ? slots[kScramblePsi] : kDefaultScramblePsi;
        target = std::min(kMaxTargetPsi, target + extra);
    }
    return target;
}

// Oil pressure (psi) for a sump level (volume / capacity), oil temperature (C) and engine speed (RPM).
inline double oil_pressure_psi(double level, double oil_c, double rpm) {
    if (!std::isfinite(level) || !std::isfinite(rpm) || rpm < 300) return 0.0;
    double t = std::isfinite(oil_c) ? oil_c : 90.0;
    double feed = std::clamp(2.0 * level, 0.0, 1.0);                 // a sump under half full starves the pickup
    double base = 10.0 + 60.0 * std::min(1.0, std::abs(rpm) / 6000.0);
    double thick = 1.0 + 0.3 * std::clamp((90.0 - t) / 70.0, 0.0, 1.0);   // cold oil reads high
    double thin = std::clamp(1.0 - (t - 130.0) / 100.0, 0.4, 1.0);        // very hot oil reads low
    return base * thick * thin * feed;
}

constexpr double kPsiPerKpa = 0.1450377;

// The part definitions (entries for the Dyno's definitions file): 1 x 1 x 1 sensors with data ports on every face but
// the bottom, the stock compass sensor's look until they get their own meshes. Starter loot tier (3).
inline std::string definition_entry(Kind k, const std::string& mesh_path) {
    const char* id = k == kController ? kControllerId : k == kBoostGauge ? kBoostGaugeId : kOilGaugeId;
    const char* name = k == kController ? "Boost Controller" : k == kBoostGauge ? "Boost Gauge" : "Oil Gauge";
    const char* description =
        k == kController ? "Electronic boost controller for every turbo on this vehicle. Wire a psi value into target_psi (a microcontroller, a dial or a "
                           "switch; 0 keeps the wastegate springs' 12.8 psi). Turbos hold it if their exhaust can spin them that fast; each engine has its "
                           "own limit and knocks past it. scramble adds scramble_psi (default 5) on top. Outputs boost_psi and target_set."
        : k == kBoostGauge ? "Boost gauge for this vehicle: boost_psi, boost_kpa and boost_bar over the air outside, target_psi and peak_psi "
                             "(reset_peak above 0.5 clears it). Wire it to a display or a microcontroller."
                           : "Oil gauge for this vehicle's hottest engine: oil_pressure_psi, oil_temperature, oil_level (0-1; under 0.5 the engine wears), "
                             "coolant_temperature and engine_temperature. Wire it to a display or a microcontroller.";
    std::string descriptors;
    const ChannelInfo* c = channels(k);
    for (int i = 0; i < channel_count(k); ++i) {
        if (!descriptors.empty()) descriptors += ",\n        ";
        descriptors += std::string("{\"type\": \"") + (c[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + c[i].name + "\"}";
    }
    return std::string("    {\n") +
        "      \"id\": \"" + id + "\",\n" +
        "      \"name\": \"" + name + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"altitude_sensor\",\n" +
        "      \"zones\": [{\"bounds_max\": [0, 0, 0]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n"
        "        {\"dir\": 2, \"gender\": 2, \"type\": \"port\"},\n"
        "        {\"gender\": 2, \"type\": \"port\"},\n"
        "        {\"dir\": 1, \"gender\": 2, \"type\": \"port\"},\n"
        "        {\"dir\": 4, \"gender\": 2, \"type\": \"port\"},\n"
        "        {\"dir\": 5, \"gender\": 2, \"type\": \"port\"}\n"
        "      ],\n" +
        "      \"logic_nodes\": [],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh_path + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"sensor\",\n" +
        "      \"tags_generated\": \"port,\",\n" +
        "      \"inspection_geometry\": [],\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [\n        " + descriptors + "\n      ],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.15,\n" +
        "      \"loot_group\": \"electronics\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []},\n" +
        "      \"liquid_pump_wattage\": 1000.0,\n" +
        "      \"liquid_pump_max_output_rps\": 50.0,\n" +
        "      \"gas_pump_wattage\": 1000.0,\n" +
        "      \"gas_pump_max_output_rps\": 50.0\n" +
        "    }";
}

}  // namespace gauges
