#pragma once
// Turbo pure logic: sizes, the part definitions, the data channels and the per-tick turbo model.
// No game or Windows dependencies, so tests/turbo_logic_test.cpp runs it on any platform.
//
// Game facts this model is built on (Anymaker 0.1.24, read from game.gcl):
//   - Gas pressure is content volume / capacity, so 1.0 is ambient. The game's part descriptions call 1.0
//     "9.8 kPa" (impeller: 0.1 per rps = "1 kPa per rps"), and this mod uses the same scale for its outputs.
//   - Engine power multiplier = remap(clamp(average air manifold pressure, 1, 10), 1..10 -> 1..2), so full
//     boost is an intake pressure of 10 ("98 kPa"). Exhaust pressure above 4 ("39 kPa") cuts power and
//     the engine stops at 20 ("196 kPa").
//   - Engines burn max_watts = 400 W per volume unit; air use is about 0.36 L/s per unit at 1x, and the
//     exhaust is the air plus the burnt fuel, at 300 C.
//   - The stock impeller moves min(content, 0.4 * net pressure * dt) litres per tick from inlet to outlet,
//     with head 0.1 per rps. A turbo here is the same pump with its head set by its shaft speed, plus a turbine
//     that lets exhaust through its hot side.
//
// The turbo is a real shaft (2026-10-15, Nate: "make them behave as close to real life as you possibly can"). Exhaust
// through the turbine pushes it (the work the gas gives up expanding, at the turbine's efficiency for its blade speed),
// the compressor and the bearings hold it back, and its inertia sets how fast it can change speed. The compressor's
// pressure ratio goes with the square of the shaft speed, so lag is not a fixed time: more exhaust spools it faster,
// a bigger (heavier, lower-pressure) turbo spools slower, and a turbo too big for its engine never gets going. A
// built-in wastegate holds full boost once the shaft has more than it needs. The game's scales are mapped onto real
// ones for this:
//   - Boost side: 1 game unit = 9.8 kPa, so full boost (9 units over the inlet, 88 kPa) is a pressure ratio of 1.87.
//   - Exhaust side: 1 game unit = 0.5 bar, so the 4 units where engines start to lose power is 2.5 bar absolute.
//   - A game litre of gas is a litre of outside air (1.2 g); the game's 300 C exhaust stands for a real engine's 900 C.
//   - The game's pipes hold far more air at full boost than real ones (pressure 10 is 10 pipe-fulls, where a real
//     pipe at a ratio of 1.87 holds 1.87), so the compressor pays for a tenth of the air that only fills its pipe.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace turbo {

constexpr int kMinSize = 2, kMaxSize = 6;
// The turbos on offer (Nate, 2026-10-15): the 3x3 (small pipe) and the 6x6 (large pipe), the stock Small and Large
// Impellers' faces. The 2x2 and 4x4 were removed (the superchargers take their place), and the 5x5 grew to the 6x6
// so its large pipes sit dead centre (a 2-cell pipe has no middle on a 5-cell face). Every turbo is 2 cells deep, like
// the stock Large Impeller: a 2-cell pipe on the top or bottom face spans both, so it is centred too (Nate tried a
// 3-deep 6x6 and picked 2 deep). The tables below keep the other sizes' numbers for the sounds, which voice blow-off
// valves and superchargers by size.
constexpr int kTurboSizes[] = {3, 6};
inline bool turbo_offered(int n) { return n == 3 || n == 6; }
constexpr const char* kIdPrefix = "turbocharger_";
constexpr double kKpaPerUnit = 9.8;       // the game's own scale (see above)
constexpr double kMaxHead = 9.0;          // ambient 1 + 9 = 10: the engine's full 2x boost
// The game's engines stop at a pressure of 10 (2x power). With compound turbos (a second stage breathing the first
// stage's boost) AnyEngineParts lets them go on to this pressure: 19 is two full stages, 3x power. One turbo still
// tops out at 10.
constexpr double kStockEnginePressure = 10.0;
constexpr double kMaxEnginePressure = 19.0;
constexpr double kAmbientC = 20.0;
constexpr double kPsiPerKpa = 0.1450377;
constexpr double kAntilagSpool = 0.8;     // antilag holds the shaft at this spool off throttle
constexpr double kAntilagFlow = 0.85;     // exhaust flow (fraction of rated) below which antilag fires (off throttle)
constexpr double kAntilagMinFlow = 0.03;  // exhaust flow (fraction of rated) that shows the engine is running
constexpr double kSurgeSpool = 0.15;      // below this spool a backflow is just boost bleeding off, not surge
constexpr double kSurgeTau = 0.8;         // surge stalls the wheel: shaft speed decay time constant, seconds
// Real-world scales (see the header comment).
constexpr double kAmbientKpa = 101.325;
constexpr double kAmbientK = 293.15;
constexpr double kFullBoostRatio = 1.0 + kMaxHead * kKpaPerUnit / kAmbientKpa;   // 1.87: a stage at full shaft speed
constexpr double kHotBarPerUnit = 0.5;     // exhaust side, real bar per game unit
constexpr double kKgPerLitre = 0.0012;
constexpr double kCpAir = 1005.0, kCpExhaust = 1150.0;    // J/kg K
constexpr double kAirExponent = 0.2857, kExhaustExponent = 0.248;   // (gamma - 1) / gamma, air and exhaust
constexpr double kExhaustTemperatureScale = 3.0;           // game exhaust C -> real exhaust C
constexpr double kFillShare = 0.1;         // share of the pipe-filling air the compressor pays for
constexpr double kOverspeed = 1.12;        // the shaft's speed limit, over its full-boost speed
constexpr double kTurbineEfficiency = 0.72, kCompressorEfficiency = 0.74;   // at their best
constexpr double kBestBladeSpeedRatio = 0.7;               // turbine tip speed over the gas's spouting speed, at best
constexpr double kBypassTau = 0.08;        // the built-in wastegate's actuator, seconds
constexpr double kTwoStepBand = 150.0;     // two-step: RPM below its limit where the fuel comes back
constexpr double kMaxTwoStepRpm = 8000.0;  // the game's fuel cut
// A turbo worn to 0% blows: the shaft stops and the wrecked compressor wheel blocks most of the intake. The engine still
// breathes through it, but only this share of a working turbo's flow: about a third of the air a matched engine (one
// whose exhaust spools it fully) wants at full revs without boost, so it idles and crawls but makes about a third of
// its power at most: limp mode. Replace the turbo to get the power back.
constexpr double kBlownFlowShare = 0.12;

// One turbo size: N x N face, 2 cells thick (cold side cell x = 0, hot side cell x = 1).
struct Spec {
    int size = 0;
    double rated_flow = 0;        // exhaust L/s through the turbine at a 2-unit pressure drop (its size, like an A/R)
    double turbine_k = 0;         // Stodola flow constant for that
    double compressor_coeff = 0;  // L/s per unit of net head on the cold side, at the outside air's density
    double design_flow = 0;       // L/s of air (at its inlet's density) where the compressor is most efficient, full speed
    double bypass_coeff = 0;      // built-in wastegate, wide open: L/s per unit of pressure drop
    double max_rpm = 0;           // shaft rpm at full boost
    double shaft_full = 0;        // the same, rad/s
    double inertia = 0;           // kg m^2 of the shaft, both wheels on it
    double tip_speed = 0;         // m/s, turbine tip at full-boost speed
    double friction = 0;          // N m of bearing drag at full-boost speed
    double spool_kj = 0;          // the shaft's energy at full-boost speed
    bool large_pipe = false;
};
// The turbine's swallowing capacity scales with the face area and the shaft's energy with size: the 6x6 suits the
// 4-rotor (it holds full boost from about 2,500 RPM there, steady), the 3x3 the 2-rotor. At its rated flow the hot side
// drops 2 units (1 bar), under the 4 units where engines lose power; more than that and the built-in wastegate opens.
inline Spec spec(int n) {
    n = std::clamp(n, kMinSize, kMaxSize);
    // 2x2 to 6x6; the 6x6 has the 5x5's numbers (it grew for its pipes, not its power).
    static constexpr double rated[] = {10, 22, 40, 65, 65};
    static constexpr double energy_kj[] = {0.3, 0.6, 1.0, 1.5, 1.5};
    static constexpr double rpm[] = {220000, 160000, 120000, 90000, 90000};
    Spec s;
    s.size = n;
    s.rated_flow = rated[n - kMinSize];
    double a = 1.0 + 2.0 * kHotBarPerUnit, x = a * a - 1.0;     // a 2-unit drop into the outside air
    s.turbine_k = s.rated_flow * std::sqrt(x + 0.075) / x;
    s.compressor_coeff = s.rated_flow * 1.2;
    s.design_flow = s.rated_flow * 2.0;
    s.bypass_coeff = s.rated_flow * 0.6;
    s.max_rpm = rpm[n - kMinSize];
    s.shaft_full = s.max_rpm * 6.283185307179586 / 60.0;
    s.spool_kj = energy_kj[n - kMinSize];
    s.inertia = 2.0 * s.spool_kj * 1000.0 / (s.shaft_full * s.shaft_full);
    // Blade speed: at full speed on that 2-unit drop with 300 C game exhaust the turbine runs at its best ratio.
    double dh = kCpExhaust * (273.15 + kExhaustTemperatureScale * 300.0) * (1.0 - std::pow(a, -kExhaustExponent));
    s.tip_speed = kBestBladeSpeedRatio * std::sqrt(2.0 * dh);
    s.friction = 0.03 * kTurbineEfficiency * kKgPerLitre * s.rated_flow * dh / s.shaft_full;   // 3% of that power
    s.large_pipe = n >= 4;
    return s;
}
inline std::string definition_id(int n) { return std::string(kIdPrefix) + char('0' + n); }
// Returns 3 or 6 for a turbo definition id, else 0.
inline int size_from_id(const char* id, size_t length) {
    size_t p = std::char_traits<char>::length(kIdPrefix);
    if (!id || length != p + 1 || std::char_traits<char>::compare(id, kIdPrefix, p) != 0) return 0;
    int n = id[p] - '0';
    return turbo_offered(n) ? n : 0;
}

// Gas logic nodes, in definition order. 0 and 1 are the stock impeller's own (inlet, outlet); 2 and 3 are
// the hot side the mod adds at runtime.
enum Node { kColdIn, kColdOut, kHotIn, kHotOut, kNodeCount };

// Data channels.
// New channels go at the end: saved data links refer to them by position.
enum Channel { kIntakePressure, kExhaustPressure, kPowerMultiplier, kSpool, kRpm, kExhaustFlow, kIntakeFlow, kExhaustTemperature, kSurge, kBoost, kBoostPsi, kAntilagFiring, kBoostLimit,
               kOilFlow, kTurboTemperature, kCondition, kAirFiltered, kBoostAirTemperature, kDrivePower, kBypass, kChannelCount };
constexpr int kFirstInput = kBoostLimit;
struct ChannelInfo { const char* name; bool input; };
inline constexpr ChannelInfo kChannels[kChannelCount] = {
    {"intake_pressure", false},     // compressor outlet, kPa on the game's scale (98 = full boost)
    {"exhaust_pressure", false},    // turbine inlet, same scale (39 starts to cut engine power)
    {"power_multiplier", false},    // engine power factor the intake pressure gives, 1-2 (up to 3 with compounds)
    {"spool", false},               // 0-1
    {"rpm", false},
    {"exhaust_flow", false},        // L/s through the hot side
    {"intake_flow", false},         // L/s through the cold side
    {"exhaust_temperature", false}, // C
    {"surge", false},               // 1 while the compressor surges (boost flowing back, no blow-off valve), else 0
    {"boost", false},               // boost over the air outside, kPa (game scale; full boost is about 88)
    {"boost_psi", false},           // the same in psi (full boost is about 12.8)
    {"antilag", false},             // 1 while antilag (from a wastegate) is firing to hold the spool, else 0
    {"boost_limit", true},          // input: cap boost (over the air outside, kPa, game scale, like `boost`); 0 = no cap
    {"oil_flow", false},            // L/s of oil through the turbo (0.5 or more cools it fully)
    {"turbo_temperature", false},   // C; above 250 it wears fast
    {"condition", false},           // 1 new, 0 blown; below 0.5 it loses boost
    {"air_filter", false},          // 1 with an air filter on the compressor intake, else 0
    {"boost_air_temperature", false},   // C, the boost air after any intercooler (what the engine breathes)
    {"drive_power", false},         // kW a supercharger takes from the engine through its belt (0 on a turbo)
    {"wastegate", false},           // 0-1, how far the turbo's built-in wastegate is open (it holds full boost)
};
inline int channel_index(const char* name, size_t length) {
    for (int i = 0; i < kChannelCount; ++i) {
        const char* n = kChannels[i].name;
        if (std::char_traits<char>::length(n) == length && std::char_traits<char>::compare(n, name, length) == 0) return i;
    }
    return -1;
}

// Where the ports sit, in cells. Axial ports use the cell at the centre of the face (rounded down on even
// sizes, as the stock large impeller does); tangential ports sit at the top far corner (cold outlet, +z) and
// bottom near corner (hot inlet, -z), where the scaled housings put their outlet stubs.
// A large pipe (4x4 and up) is 2 x 2 cells, anchored at its low corner: across the two axes other than the one it
// faces (y and z for the axial ports; x and y for the z-facing ones, so it takes both of the part's cells along x).
// The stock large impeller (turbo_d) anchors its +z outlet one cell below the top the same way.
struct Port { int x, y, z, dir; };
inline Port port(int n, Node node) {
    int c = (n - 1) / 2, top = n - 1;
    bool large = spec(n).large_pipe;
    switch (node) {
    case kColdIn: return {0, c, c, 0};                          // x_neg: front, axial
    case kColdOut: return {0, large ? top - 1 : top, top, 5};   // z_pos
    case kHotIn: return {large ? 0 : 1, 0, 0, 4};              // z_neg
    default: return {1, c, c, 1};                               // x_pos: back, axial
    }
}
// The cells a port's pipe end covers on the part's face: 1 x 1 for a small pipe, 2 x 2 for a large one.
inline bool port_covers(int n, Node node, int x, int y, int z) {
    Port p = port(n, node);
    int span = spec(n).large_pipe ? 2 : 1;
    auto in = [&](int v, int lo) { return v >= lo && v < lo + span; };
    if (p.dir <= 1) return x == p.x && in(y, p.y) && in(z, p.z);
    if (p.dir <= 3) return y == p.y && in(x, p.x) && in(z, p.z);
    return z == p.z && in(x, p.x) && in(y, p.y);
}

inline std::string fmt(double v) {
    char b[32]; std::snprintf(b, sizeof b, "%g", v); return b;
}
inline std::string description(int n) {
    Spec s = spec(n);
    return "Exhaust-driven turbocharger, " + std::to_string(n) + "x" + std::to_string(n) + "x2. Cold side (front): compressor intake in "
        "the middle of the face, compressor outlet at the top facing sideways - feed an air manifold. Hot side (back): turbine "
        "exhaust out in the middle - vent it; turbine inlet at the bottom facing the other side, from the exhaust manifold. Exhaust spins its shaft "
        "and boost climbs with the square of the shaft's speed, so the more exhaust, the quicker it spools. A built-in wastegate holds full boost "
        "(88 kPa, double engine power). " + std::string(n >= 6 ? "Suits a 3- or 4-rotor or a long small engine; on a small one it barely spools. "
                                                                  : "Suits a 1- or 2-rotor or a short small engine; on a big one it chokes the exhaust. ") +
        (s.large_pipe ? "Large pipe." : "Small pipe.") +
        " Underneath: oil in (front) and oil out (back) - circulate engine oil through it with an oil cooler and oil filter. "
        "It wears with use: much faster without oil flow, without an air filter on its intake, or running hot. Its Condition "
        "shows in the tooltip; at 0% it blows and chokes the engine's air (limp mode) until you replace it.";
}

// Oil: feed in under the compressor housing, drain out under the turbine housing, both under the centre. They come
// after the four gas nodes (saved links refer to logic nodes by position, so the old ones keep their meaning).
// No stock part puts a gas node and a liquid node in one cell, so on the 2x2 (where the centre row holds the gas
// intake and the turbine nodes) they sit one row further back, in cells of their own.
inline Port oil_port(int n, bool in) { return {in ? 0 : 1, 0, n == 2 ? 1 : (n - 1) / 2, 2}; }
inline std::string oil_node(int n, bool in) {
    Port p = oil_port(n, in);
    return "{\"id\": \"" + std::string(in ? "oil_in" : "oil_out") + "\", \"pos\": [" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + "], \"direction\": 2, \"type\": \"liquid\"}";
}

// One part definition: a resized stock impeller (class gas_pump_centrifugal) with a hot side.
inline std::string definition_entry(int n, const std::string& mesh_path) {
    Spec s = spec(n);
    int top = n - 1;
    std::string pipe = s.large_pipe ? ", \"size\": 1" : "";
    auto node = [&](Node which) {
        Port p = port(n, which);
        return "{\"pos\": [" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + "], \"direction\": " + std::to_string(p.dir) + ", \"type\": \"gas\"" + pipe + "}";
    };
    // Mounting faces for a data port: every front and back face cell except under the axial pipes (2 x 2 cells for a
    // large pipe). The torque mount the impeller class asks for sits under the front far corner, clear of the oil
    // feed (on the 2x2, whose oil feed takes that cell, under the front near corner).
    std::string surfaces = "{\"id\": \"torque\", \"pos\": [0, 0, " + std::to_string(n == 2 ? 0 : top) + "], \"dir\": 2, \"type\": \"torque\"}";
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < n; ++y)
            for (int z = 0; z < n; ++z) {
                if (port_covers(n, x ? kHotOut : kColdIn, x, y, z)) continue;
                surfaces += ",\n        {\"pos\": [" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "], \"dir\": " + std::to_string(x) + ", \"gender\": 2, \"type\": \"port\"}";
            }
    std::string descriptors;
    for (int i = 0; i < kChannelCount; ++i)
        descriptors += std::string(i ? ",\n        " : "") + "{\"type\": \"" + (kChannels[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + kChannels[i].name + "\"}";
    std::string box = "[1, " + std::to_string(top) + ", " + std::to_string(top) + "]";
    return std::string("    {\n") +
        "      \"id\": \"" + definition_id(n) + "\",\n" +
        "      \"name\": \"Turbo " + std::to_string(n) + "x" + std::to_string(n) + "\",\n" +
        "      \"description\": \"" + description(n) + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [{\"bounds_min\": [0, 0, 0], \"bounds_max\": " + box + "}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n        " + surfaces + "\n      ],\n" +
        "      \"logic_nodes\": [\n        " + node(kColdIn) + ",\n        " + node(kColdOut) + ",\n        " + node(kHotIn) + ",\n        " + node(kHotOut) + ",\n        " +
            oil_node(n, true) + ",\n        " + oil_node(n, false) + "\n      ],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh_path + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags\": \"torque\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [0.0, 0.0, 0.0], \"p1\": [1.0, " + std::to_string(top) + ".0, " + std::to_string(top) + ".0]}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [\n        " + descriptors + "\n      ],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string mesh_path(int n) { return "meshes/components/turbo_" + definition_id(n) + ".mesh"; }
constexpr const char* kStockMeshPath = "meshes/components/turbo_a.mesh";
// ------------------------------------------------------------------------------------------- model
struct Inputs {
    double pressure[kNodeCount]{};   // content volume / capacity of each node's content
    double volume[kNodeCount]{};     // litres currently in each node's content
    double exhaust_temperature = kAmbientC;
    double intake_temperature = kAmbientC;   // C of the air reaching the compressor (outside air, or a first stage's boost)
    double boost_capacity = 0;       // litres the whole boost pipe holds (0: only this part's outlet is known)
    int feeders = 1;                 // compressors pushing into that pipe (twins share its filling)
    double dt = 0;
    bool bov_venting = false;        // a blow-off valve on the boost pipe is venting this tick
    double bov_spring_kpa = 0;       // its Stiffness setting (the stiffest, if several vent)
    bool antilag = false;            // a wastegate on the exhaust has its antilag input on
    bool two_step = false;           // the engine is on its launch limiter (two-step): unburnt fuel reaches the exhaust
};
inline double blow_off_surge_through(double spring_kpa);   // below, with the blow-off valve
struct Step {
    double turbine_move = 0;     // litres to move this tick, hot in -> hot out (negative: back), wheel and wastegate
    double compressor_move = 0;  // litres to move this tick, cold in -> cold out (negative: back)
    double head = 0;             // pressure the compressor adds at no flow
    double power = 0;            // W the compressor takes from its shaft (or belt)
    double turbine_power = 0;    // W the exhaust gives the shaft
    double pressure_ratio = 1;   // real outlet over inlet pressure
    double efficiency = kCompressorEfficiency;   // the compressor's, where it is running (sets how hot the air gets)
};
struct State {
    double spool = 0;             // share of full boost the shaft can make: (speed / full-boost speed)^2, at most 1
    double speed = 0;             // shaft speed over its full-boost speed (up to kOverspeed)
    double bypass = 0;            // built-in wastegate opening, 0-1
    double exhaust_flow = 0;     // smoothed L/s
    double intake_flow = 0;      // smoothed L/s
    double last_boost_pressure = NAN;   // the boost pipe's pressure at the start of the last tick
    double fill = 0;             // L/s this compressor's share of the boost pipe filled by, last tick
    bool surging = false;
    bool antilag_firing = false;  // antilag is lighting fuel in the exhaust to hold the spool (throttle off)
    double health = 1;            // share of its head the turbo can still make (wear, clogged filter; turbo_health.h)
    bool blown = false;           // worn to 0%: no boost, a choked intake (kBlownFlowShare)
    double slots[kChannelCount]{};
};

inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline double finite_or(double v, double d) { return std::isfinite(v) ? v : d; }
// Move up to `want` litres from a content holding `from_volume` (want < 0 moves back, capped by `back_volume`).
inline double capped(double want, double from_volume, double back_volume) {
    return want >= 0 ? std::min(want, std::max(0.0, from_volume)) : -std::min(-want, std::max(0.0, back_volume));
}
// The engine's boost factor for an intake pressure: the game's remap(pressure, 1, 10, 1, 2), carried on past 10 up to
// kMaxEnginePressure for compounds.
inline double power_multiplier(double intake_pressure) {
    return 1.0 + (std::clamp(finite_or(intake_pressure, 1.0), 1.0, kMaxEnginePressure) - 1.0) / 9.0;
}
// Real absolute pressure (kPa) of boost-side air at a game pressure, and real bar of exhaust.
inline double boost_kpa(double p) { return kAmbientKpa + kKpaPerUnit * (finite_or(p, 1.0) - 1.0); }
inline double exhaust_bar(double p) { return 1.0 + kHotBarPerUnit * (std::max(0.0, finite_or(p, 1.0)) - 1.0); }
// How much denser (by temperature) the compressor's inlet air is than outside air: it moves that much more.
inline double inlet_density(double inlet_c) { return kAmbientK / (273.15 + std::clamp(finite_or(inlet_c, kAmbientC), -50.0, 600.0)); }

// The compressor's head (pressure units over its inlet, at no flow) for its shaft speed: a centrifugal stage makes a
// pressure RATIO that climbs with the square of its speed, 1.87 at full-boost speed, so a stage breathing boosted air
// adds more units than one breathing outside air. Capped by the boost limit (boost over the outside air, as the
// `boost` output reads it, so a second stage makes only the rest) and by the engine's 3x pressure.
inline double head_for(const State& st, double inlet_pressure) {
    double p_in = std::max(1.0, finite_or(inlet_pressure, 1.0));
    double speed = std::max(0.0, finite_or(st.speed, 0.0));
    double head = boost_kpa(p_in) / kKpaPerUnit * (kFullBoostRatio - 1.0) * speed * speed * std::clamp(finite_or(st.health, 1.0), 0.0, 1.0);
    head = std::min(head, std::max(0.0, kMaxEnginePressure - p_in));
    double limit = finite_or(st.slots[kBoostLimit], 0.0);
    if (limit > 0) head = std::clamp(limit / kKpaPerUnit - (p_in - 1.0), 0.0, head);
    return head;
}
// Exhaust through the turbine wheel, L/s: Stodola's ellipse law in real pressures (flow ~ sqrt(P_in^2 - P_out^2)),
// softened near no flow so a small manifold stays steady. Negative flows back.
inline double turbine_flow(const Spec& s, double p_in, double p_out) {
    double a = exhaust_bar(p_in), b = exhaust_bar(p_out), x = a * a - b * b;
    return s.turbine_k * x / std::sqrt(std::abs(x) + 0.075);
}
// The compressor's efficiency where it runs: best near 3/4 of its design flow for its speed, falling off towards
// surge (too little flow for its speed) and choke (too much).
inline double compressor_efficiency(const Spec& s, double inlet_lps, double speed) {
    double phi = std::max(0.0, inlet_lps) / (s.design_flow * std::max(speed, 0.2));
    return std::max(0.4, kCompressorEfficiency * (1.0 - 0.8 * (phi - 0.75) * (phi - 0.75)));
}
// Watts to push `lps` of air (game litres) from inlet to outlet pressure at that efficiency.
inline double compressor_watts(double lps, double p_in, double p_out, double inlet_c, double efficiency) {
    double ratio = boost_kpa(p_out) / std::max(1.0, boost_kpa(p_in));
    if (!(lps > 0) || !(ratio > 1)) return 0.0;
    return kKgPerLitre * lps * kCpAir * (273.15 + finite_or(inlet_c, kAmbientC)) * (std::pow(ratio, kAirExponent) - 1.0) / std::max(0.1, efficiency);
}
// The pipe-filling share of a compressor's flow costs a tenth of its real work (header comment): the air it pays for.
inline double working_flow(State& st, const Inputs& in, double flow_lps, double dt) {
    double p = std::max(0.0, finite_or(in.pressure[kColdOut], 1.0));
    double own = p > 1e-6 ? std::max(0.0, finite_or(in.volume[kColdOut], 0.0)) / p : 0.0;
    double capacity = in.boost_capacity > 0 && std::isfinite(in.boost_capacity) ? in.boost_capacity : own;
    st.fill = std::isfinite(st.last_boost_pressure) ? (p - st.last_boost_pressure) * capacity / dt / std::max(1, in.feeders) : 0.0;
    st.last_boost_pressure = p;
    return std::clamp(flow_lps - (1.0 - kFillShare) * std::max(0.0, st.fill), 0.0, std::max(0.0, flow_lps));
}

inline Step step(State& st, const Spec& s, const Inputs& in) {
    Step out;
    double dt = std::clamp(finite_or(in.dt, 0.0), 0.0, 0.25);
    if (dt <= 0) return out;
    double p[kNodeCount];
    for (int i = 0; i < kNodeCount; ++i) p[i] = std::max(0.0, finite_or(in.pressure[i], 1.0));
    bool dead = st.blown || !(st.health > 0);     // the shaft no longer turns
    st.speed = std::clamp(finite_or(st.speed, 0.0), 0.0, kOverspeed);
    double omega = st.speed * s.shaft_full;
    // Hot side: exhaust through the turbine wheel, and round it through the built-in wastegate.
    double wheel = turbine_flow(s, p[kHotIn], p[kHotOut]);
    double around = clamp01(st.bypass) * s.bypass_coeff * (p[kHotIn] - p[kHotOut]);
    out.turbine_move = capped((wheel + around) * dt, in.volume[kHotIn], in.volume[kHotOut]);
    // Only exhaust going the right way drives the turbine: gas flowing back out of a pressurised hot side after the
    // engine stops must not spool it up.
    double total = wheel + around;
    double wheel_lps = out.turbine_move > 0 && total > 1e-9 ? out.turbine_move / dt * std::max(0.0, wheel) / total : 0.0;
    double k = std::min(1.0, dt / 0.15);
    st.exhaust_flow += (std::max(0.0, out.turbine_move) / dt - st.exhaust_flow) * k;
    // The work the exhaust can give up expanding across the wheel (J/kg).
    double expansion = exhaust_bar(p[kHotIn]) / std::max(1e-3, exhaust_bar(p[kHotOut]));
    double exhaust_k = std::max(kAmbientK, 273.15 + kExhaustTemperatureScale * finite_or(in.exhaust_temperature, kAmbientC));
    double work = expansion > 1 ? kCpExhaust * exhaust_k * (1.0 - std::pow(expansion, -kExhaustExponent)) : 0.0;
    double exhaust_kg = dead ? 0.0 : kKgPerLitre * wheel_lps;
    // Cold side: a centrifugal compressor with the head of its speed. Denser (cooler) inlet air moves more.
    out.head = head_for(st, p[kColdIn]);
    double density = inlet_density(in.intake_temperature);
    double net = out.head - (p[kColdOut] - p[kColdIn]);
    out.compressor_move = capped(s.compressor_coeff * density * (st.blown ? kBlownFlowShare : 1.0) * net * dt, in.volume[kColdIn], in.volume[kColdOut]);
    double flow = out.compressor_move / dt;
    double paid = working_flow(st, in, flow, dt);
    out.pressure_ratio = std::max(1.0, finite_or(boost_kpa(p[kColdOut]) / std::max(1.0, boost_kpa(p[kColdIn])), 1.0));
    out.efficiency = compressor_efficiency(s, flow / density, st.speed);
    out.power = compressor_watts(paid, p[kColdIn], p[kColdOut], in.intake_temperature, out.efficiency);
    // The built-in wastegate holds the shaft at the speed that makes the target boost at this flow (the head the
    // compressor needs is the target plus its droop at this flow), and never past its speed limit. The target is full
    // boost (a ratio of 1.87 over its inlet) up to the engine's 3x pressure, or the boost limit input.
    double target_out = std::min(kMaxEnginePressure, 1.0 + (boost_kpa(p[kColdIn]) * kFullBoostRatio - kAmbientKpa) / kKpaPerUnit);
    double limit = finite_or(st.slots[kBoostLimit], 0.0);
    if (limit > 0) target_out = std::min(target_out, 1.0 + limit / kKpaPerUnit);
    double per_speed2 = boost_kpa(p[kColdIn]) / kKpaPerUnit * (kFullBoostRatio - 1.0) * std::max(0.05, std::clamp(finite_or(st.health, 1.0), 0.0, 1.0));
    double need = (std::max(0.0, target_out - p[kColdIn]) + std::max(0.0, flow) / (s.compressor_coeff * density)) / per_speed2;
    double cap = s.shaft_full * std::min(kOverspeed, std::sqrt(std::max(0.0, need)));
    // Antilag: when the exhaust alone would let the shaft slow (off throttle), fuel burnt in the manifold gives the
    // turbine what it needs to hold its speed. It needs the engine running (some exhaust to light), or on its two-step
    // limiter, which sends its fuel into the exhaust instead of burning it in the cylinders.
    double hold = std::sqrt(kAntilagSpool) * s.shaft_full;
    st.antilag_firing = !dead && in.antilag &&
                        (in.two_step || (st.exhaust_flow < kAntilagFlow * s.rated_flow && st.exhaust_flow > kAntilagMinFlow * s.rated_flow));
    // Shaft, in a few small steps: turbine torque falls off as the blade speed passes the gas's best (a radial
    // turbine's torque is highest stalled and gone at about twice its best blade speed).
    const int kSteps = 4;
    double h = dt / kSteps, spare = 0.0, r = s.tip_speed / s.shaft_full;
    double spout = work > 0 ? std::sqrt(2.0 * work) : 0.0;
    for (int i = 0; i < kSteps; ++i) {
        double turbine = 0.0;
        if (spout > 0 && exhaust_kg > 0) {
            double u = r * omega / spout, runaway = 2.0 * kBestBladeSpeedRatio;
            turbine = std::max(0.0, kTurbineEfficiency * exhaust_kg * work * 4.0 * r / (runaway * spout) * (1.0 - u / runaway));
        }
        double load = out.power / std::max(omega, 0.05 * s.shaft_full);
        double friction = omega > 0 ? s.friction * (0.3 + 0.7 * omega / s.shaft_full) : 0.0;
        if (st.antilag_firing && omega < hold) turbine = std::max(turbine, load + friction + s.inertia * (hold - omega) / 0.3);
        double next = std::max(0.0, omega + (turbine - load - friction) / s.inertia * h);
        if (next > cap && turbine > 0) {
            if (omega > cap) { spare = 1.0; next = std::max(cap, omega - (load + friction) / s.inertia * h); }   // all of it goes round
            else { spare = std::max(spare, clamp01((turbine - load - friction) / turbine)); next = cap; }
        }
        out.turbine_power = turbine * next;
        omega = next;
    }
    if (dead) omega *= std::exp(-dt / 0.3);
    // The wastegate opens by the share of the wheel's exhaust the shaft could not use.
    double want = spare > 0 ? clamp01(spare * std::max(0.0, total) / std::max(1e-6, s.bypass_coeff * std::max(0.05, p[kHotIn] - p[kHotOut]))) : 0.0;
    st.bypass += (want - st.bypass) * std::min(1.0, dt / kBypassTau);
    // Surge: boost pushing back through a spinning compressor (throttle off, no blow-off valve). The flow stalls and
    // reverses in pulses, which slows the wheel hard. A venting blow-off valve takes that boost instead.
    st.surging = false;
    // A stiff spring holds that much boost against the wheel even while it vents, so some surge gets through: none
    // up to kBlowOffSoftSpring, all of it at kBlowOffMaxSpring.
    double through = in.bov_venting ? blow_off_surge_through(in.bov_spring_kpa) : 1.0;
    if (out.compressor_move < 0 && through <= 0) out.compressor_move = 0;
    else if (out.compressor_move < 0 && st.spool > kSurgeSpool) {
        st.surging = true;
        out.compressor_move *= through;
        omega *= std::exp(-dt * through / kSurgeTau);
    }
    st.speed = std::clamp(finite_or(omega / s.shaft_full, 0.0), 0.0, kOverspeed);
    st.spool = std::min(1.0, st.speed * st.speed);
    double intake_lps = std::abs(out.compressor_move) / dt;
    st.intake_flow += (intake_lps - st.intake_flow) * k;
    // Outputs (inputs keep whatever the data link wrote).
    st.slots[kIntakePressure] = p[kColdOut] * kKpaPerUnit;
    st.slots[kExhaustPressure] = p[kHotIn] * kKpaPerUnit;
    st.slots[kPowerMultiplier] = power_multiplier(p[kColdOut]);
    st.slots[kSpool] = st.spool;
    st.slots[kRpm] = st.speed * s.max_rpm;
    st.slots[kExhaustFlow] = st.exhaust_flow;
    st.slots[kIntakeFlow] = st.intake_flow;
    st.slots[kExhaustTemperature] = finite_or(in.exhaust_temperature, kAmbientC);
    st.slots[kSurge] = st.surging ? 1.0 : 0.0;
    st.slots[kBoost] = (p[kColdOut] - 1.0) * kKpaPerUnit;
    st.slots[kBoostPsi] = st.slots[kBoost] * kPsiPerKpa;
    st.slots[kAntilagFiring] = st.antilag_firing ? 1.0 : 0.0;
    st.slots[kBypass] = st.bypass;
    return out;
}

// ------------------------------------------------------------------------------------------- superchargers
// A centrifugal supercharger: the stock Impeller's class, driven the same way, by a stock Pulley Wheel on its torque
// spot belted to an Engine Wheel. Belts are fixed ratios: the pulley turns 2.5, 3.875 or 6.675 times as fast as the
// engine with the small, medium or large Engine Wheel. No turbine and no lag: its impeller is geared to the pulley, so
// it turns with the engine at every instant, and its pressure ratio climbs with the square of its speed, like any
// centrifugal compressor (so boost builds with RPM and peaks at the top: that is what a real centrifugal supercharger
// does; a Roots or screw blower would be flat). On the medium Engine Wheel it reaches a turbo's full boost at
// kBlowerFullBoostRpm; the large wheel overdrives it (full boost from about 4,000 RPM), the small one underdrives it
// (about half boost at the fuel cut), which is how a real supercharger is tuned: by its pulley ratio. Squeezing the air
// takes the same real work as a turbo's compressor, which the belt takes from the engine like any other load. No oil or
// wear: its gearbox carries its own oil.
//   small: 2 x 2 x 2 cells; large: 4 x 4 x 4. Both: a large-pipe intake in the middle of the front face (z-), the
//   outlet on top (y+; small pipe on the small, large on the large), the torque spot on the back face (z+) in the
//   bottom corner cell, data ports on both sides.
enum Blower { kNoBlower = 0, kBlowerSmall = 1, kBlowerLarge = 2 };
constexpr const char* kBlowerSmallId = "supercharger_small";
constexpr const char* kBlowerLargeId = "supercharger_large";
constexpr double kBeltSmall = 2.5, kBeltMedium = 3.875, kBeltLarge = 6.675;   // pulley turns per engine turn, by Engine Wheel
constexpr double kBlowerFullBoostRpm = 7000;   // engine RPM of full boost on the medium Engine Wheel
constexpr double kBlowerFullBoostRps = kBlowerFullBoostRpm / 60.0 * kBeltMedium;   // the pulley's speed then
constexpr double kBlowerDriveEfficiency = 0.95;   // its step-up gears and the belt
inline Blower blower_from_id(const char* id, size_t length) {
    auto is = [&](const char* want) { return std::char_traits<char>::length(want) == length && std::char_traits<char>::compare(id, want, length) == 0; };
    return !id ? kNoBlower : is(kBlowerSmallId) ? kBlowerSmall : is(kBlowerLargeId) ? kBlowerLarge : kNoBlower;
}
inline const char* blower_id(bool large) { return large ? kBlowerLargeId : kBlowerSmallId; }
inline int blower_cells(bool large) { return large ? 4 : 2; }
inline Spec blower_spec(bool large) {
    Spec s;
    s.size = blower_cells(large);
    s.rated_flow = large ? 44 : 16;          // L/s it moves at full boost against a hungry engine, like a turbo of that flow
    s.compressor_coeff = 1.2 * s.rated_flow;
    s.design_flow = 2.0 * s.rated_flow;
    s.max_rpm = large ? 45000 : 65000;       // impeller speed at full boost (the rpm output)
    s.large_pipe = large;
    return s;
}
// Its impeller's speed over its full-boost speed, for a pulley speed (rev/s). Past full boost (the large wheel near the
// top) its bypass holds full boost, as a real supercharger's does.
inline double blower_speed(double pulley_rps) { return std::min(1.0, std::abs(finite_or(pulley_rps, 0.0)) / kBlowerFullBoostRps); }
// Share of full boost at a pulley speed: a centrifugal compressor's head goes with the square of its speed.
inline double blower_spool(double pulley_rps) { double r = blower_speed(pulley_rps); return r * r; }
struct BlowerStep { double compressor_move = 0, head = 0, drive_joules = 0, pressure_ratio = 1, efficiency = kCompressorEfficiency; };
// One tick. The cold-side ports only (kColdIn, kColdOut); pulley_rps is the speed of its torque spot.
inline BlowerStep blower_step(State& st, const Spec& s, const Inputs& in, double pulley_rps) {
    BlowerStep out;
    double dt = std::clamp(finite_or(in.dt, 0.0), 0.0, 0.25);
    if (dt <= 0) return out;
    double p_in = std::max(0.0, finite_or(in.pressure[kColdIn], 1.0)), p_out = std::max(0.0, finite_or(in.pressure[kColdOut], 1.0));
    st.speed = blower_speed(pulley_rps);             // geared to the pulley: no lag at all
    st.spool = st.speed * st.speed;
    out.head = head_for(st, p_in);
    double density = inlet_density(in.intake_temperature);
    double net = out.head - (p_out - p_in);
    out.compressor_move = capped(s.compressor_coeff * density * net * dt, in.volume[kColdIn], in.volume[kColdOut]);
    // Throttle off, the boost pushes back through the wheel in surges, unless a blow-off valve on the pipe takes it.
    if (out.compressor_move < 0 && in.bov_venting) out.compressor_move = 0;
    st.surging = out.compressor_move < 0 && st.spool > kSurgeSpool;
    double flow = out.compressor_move / dt;
    double paid = working_flow(st, in, flow, dt);
    out.pressure_ratio = std::max(1.0, finite_or(boost_kpa(p_out) / std::max(1.0, boost_kpa(p_in)), 1.0));
    out.efficiency = compressor_efficiency(s, flow / density, st.speed);
    out.drive_joules = compressor_watts(paid, p_in, p_out, in.intake_temperature, out.efficiency) / kBlowerDriveEfficiency * dt;
    double k = std::min(1.0, dt / 0.15);
    st.intake_flow += (std::abs(out.compressor_move) / dt - st.intake_flow) * k;
    st.slots[kIntakePressure] = p_out * kKpaPerUnit;
    st.slots[kPowerMultiplier] = power_multiplier(p_out);
    st.slots[kSpool] = st.spool;
    st.slots[kRpm] = std::abs(finite_or(pulley_rps, 0.0)) / kBlowerFullBoostRps * s.max_rpm;
    st.slots[kIntakeFlow] = st.intake_flow;
    st.slots[kSurge] = st.surging ? 1.0 : 0.0;
    st.slots[kBoost] = (p_out - 1.0) * kKpaPerUnit;
    st.slots[kBoostPsi] = st.slots[kBoost] * kPsiPerKpa;
    st.slots[kCondition] = 1.0;
    st.slots[kDrivePower] = out.drive_joules / dt / 1000.0;
    return out;
}
inline std::string blower_mesh_path(bool large) { return std::string("meshes/components/turbo_") + blower_id(large) + ".mesh"; }
inline std::string blower_entry(bool large, const std::string& mesh_path) {
    const int n = blower_cells(large), top = n - 1;
    std::string description = std::string("Belt-driven centrifugal supercharger, ") + std::to_string(n) + "x" + std::to_string(n) + "x" + std::to_string(n) +
        ". Bolt a Pulley Wheel onto its torque spot (the back, bottom corner) and belt it to an Engine Wheel, as you would an Impeller. "
        "Intake: the large pipe in the middle of the front face, open to the air. Outlet on top: feed an air manifold, or a turbo's intake to twincharge. "
        "No lag: it turns with the engine, and boost climbs with the square of engine speed. On the medium Engine Wheel it reaches full boost (88 kPa, double engine power) near " + fmt(kBlowerFullBoostRpm) +
        " RPM; the large wheel spins it faster (full boost from about 4000 RPM), the small one slower. Driving it takes power from the engine through the belt. " +
        (large ? "Large pipe outlet." : "Small pipe outlet.");
    std::string surfaces = "{\"id\": \"torque\", \"pos\": [0, 0, " + std::to_string(top) + "], \"dir\": 5, \"type\": \"torque\"}";
    for (int side = 0; side < 2; ++side)                     // data ports on both sides
        for (int y = 0; y < n; ++y)
            for (int z = 0; z < n; ++z)
                surfaces += ",\n        {\"pos\": [" + std::to_string(side ? top : 0) + ", " + std::to_string(y) + ", " + std::to_string(z) + "], \"dir\": " + std::to_string(side) + ", \"gender\": 2, \"type\": \"port\"}";
    // A large pipe is 2 x 2 cells anchored at its low corner: the intake across x and y (centred on the even face),
    // the large outlet across x and z.
    std::string in = large ? "{\"pos\": [1, 1, 0], \"direction\": 4, \"type\": \"gas\", \"size\": 1}" : "{\"pos\": [0, 0, 0], \"direction\": 4, \"type\": \"gas\", \"size\": 1}";
    std::string out = large ? "{\"pos\": [2, 3, 0], \"direction\": 3, \"type\": \"gas\", \"size\": 1}" : "{\"pos\": [1, 1, 0], \"direction\": 3, \"type\": \"gas\"}";
    std::string descriptors;
    for (int i = 0; i < kChannelCount; ++i)
        descriptors += std::string(i ? ",\n        " : "") + "{\"type\": \"" + (kChannels[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + kChannels[i].name + "\"}";
    std::string box = "[" + std::to_string(top) + ", " + std::to_string(top) + ", " + std::to_string(top) + "]";
    return std::string("    {\n") +
        "      \"id\": \"" + blower_id(large) + "\",\n" +
        "      \"name\": \"" + (large ? "Supercharger (Large)" : "Supercharger (Small)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [{\"bounds_min\": [0, 0, 0], \"bounds_max\": " + box + "}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n        " + surfaces + "\n      ],\n" +
        "      \"logic_nodes\": [\n        " + in + ",\n        " + out + "\n      ],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh_path + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags\": \"torque\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [0.0, 0.0, 0.0], \"p1\": " + box + "}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [\n        " + descriptors + "\n      ],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}

// ------------------------------------------------------------------------------------------- valves
// Two companion parts, also of the impeller's class (two gas nodes: in at the bottom, out at the top, one cell):
//   Blow-off valve: sits on the boost pipe. When boost in the pipe is higher than the turbo can hold (throttle
//     off), and the excess passes its spring setting (Properties Tool, kPa), it vents the pipe to the air. That
//     stops the compressor surging. It sits inline: turbo -> inlet, outlet ("Air") -> the air manifold; the gas
//     passes straight through. Small (small pipe) and large (large pipe) versions.
//   Wastegate: sits between the exhaust manifold and the downpipe or a vent. Its "open" input (0-1, from a data
//     link or microcontroller) sets how much exhaust bypasses the turbine, which caps boost. Small and large.
enum Valve { kNoValve, kBlowOff, kWastegate, kBlowOffLarge, kWastegateLarge, kAirFilter, kAirFilterLarge, kIntercooler };
constexpr const char* kBlowOffId = "turbo_blow_off_valve";
constexpr const char* kBlowOffLargeId = "turbo_blow_off_valve_large";
constexpr const char* kWastegateId = "turbo_wastegate";
constexpr const char* kWastegateLargeId = "turbo_wastegate_large";
constexpr const char* kAirFilterId = "turbo_air_filter";
constexpr const char* kAirFilterLargeId = "turbo_air_filter_large";
constexpr const char* kIntercoolerId = "turbo_intercooler";
constexpr const char* kIntercoolerRoundId = "turbo_intercooler_round";
constexpr double kBlowOffCoeff = 60.0;          // L/s per unit of excess boost while open (small; large x2.5)
constexpr double kBlowOffLargeFactor = 2.5;
constexpr double kWastegateCoeff = 40.0;        // L/s per unit of pressure drop, fully open (small; large x2.5)
constexpr double kBlowOffDefaultKpa = 5.0;      // spring setting: excess boost (kPa) before it opens
// Off throttle the spool keeps easing down towards idle, so the trapped boost keeps creeping a hair over the
// spring and the valve lets out a puff now and then for as long as the throttle stays shut. Those puffs still
// stop the surge, but the valve only counts as open (sound and the `open` output) while it really dumps air.
constexpr double kBlowOffAudibleLps = 8.0;
// Stiffness: a soft spring (up to 15 kPa) stops surge completely; stiffer ones let more of it through.
constexpr double kBlowOffSoftSpring = 15.0, kBlowOffMaxSpring = 50.0;
inline double blow_off_surge_through(double spring_kpa) {
    double k = (finite_or(spring_kpa, kBlowOffDefaultKpa) - kBlowOffSoftSpring) / (kBlowOffMaxSpring - kBlowOffSoftSpring);
    return k < 0 ? 0 : k > 1 ? 1 : k;
}
inline bool blow_off_shows_open(bool vented_recently, double flow_lps) { return vented_recently && flow_lps > kBlowOffAudibleLps; }
constexpr double kBlowOffMaxKpa = 50.0;
inline Valve valve_from_id(const char* id, size_t length) {
    auto is = [&](const char* want) { return std::char_traits<char>::length(want) == length && std::char_traits<char>::compare(want, id, length) == 0; };
    return !id ? kNoValve : is(kBlowOffId) ? kBlowOff : is(kBlowOffLargeId) ? kBlowOffLarge : is(kWastegateId) ? kWastegate : is(kWastegateLargeId) ? kWastegateLarge
         : is(kAirFilterId) ? kAirFilter : is(kAirFilterLargeId) ? kAirFilterLarge : (is(kIntercoolerId) || is(kIntercoolerRoundId)) ? kIntercooler : kNoValve;
}

enum ValveChannel { kValveOpen, kValveFlow, kValvePressure, kValveAntilag, kValveTwoStep, kValveChannelCount };
inline constexpr const char* kValveChannelNames[kValveChannelCount] = {"open", "flow", "pressure", "antilag", "two_step"};
// Two-step launch limiter: holds an engine at `limit` RPM by cutting its fuel there and giving it back kTwoStepBand
// below, the bouncing rev limiter of a real launch control. `cutting` carries the state from tick to tick. A limit of
// 0 (or none) is off; past the game's own fuel cut it changes nothing.
inline bool two_step_cut(bool& cutting, double rpm, double limit) {
    if (!(limit > 0) || !std::isfinite(limit) || !std::isfinite(rpm)) return cutting = false;
    limit = std::min(limit, kMaxTwoStepRpm);
    if (std::abs(rpm) >= limit) cutting = true;
    else if (std::abs(rpm) < limit - kTwoStepBand) cutting = false;
    return cutting;
}
inline int valve_channel_index(const char* name, size_t length) {
    for (int i = 0; i < kValveChannelCount; ++i)
        if (std::char_traits<char>::length(kValveChannelNames[i]) == length && std::char_traits<char>::compare(kValveChannelNames[i], name, length) == 0) return i;
    return -1;
}
struct ValveState {
    double setting_kpa = kBlowOffDefaultKpa;   // blow-off spring (Properties Tool)
    double flow = 0;                          // smoothed L/s
    double venting_until = 0;                 // blow-off: seconds of game time it stays open after a lift
    double slots[kValveChannelCount]{};       // open (wastegate: input; blow-off: output 0/1), flow L/s, pressure kPa
};

// Blow-off: litres to vent this tick from the boost pipe, given the boost above what the turbo holds (pressure
// units), its spring setting and the litres in its inlet. 0 while the excess is under the spring.
inline double blow_off_vent(double excess, double setting_kpa, double inlet_volume, double dt, bool large = false) {
    if (!(dt > 0) || !(excess * kKpaPerUnit > std::max(0.5, finite_or(setting_kpa, kBlowOffDefaultKpa)))) return 0;   // 0.5 kPa: rounding, not boost
    return std::min(kBlowOffCoeff * (large ? kBlowOffLargeFactor : 1.0) * excess * dt, std::max(0.0, inlet_volume));
}
// Wastegate: litres to pass this tick, inlet -> outlet (negative: back), for an opening 0-1.
inline double wastegate_move(double open, double p_in, double p_out, double v_in, double v_out, double dt, bool large = false) {
    double o = clamp01(finite_or(open, 0.0));
    if (!(dt > 0) || o <= 0) return 0;
    return capped(kWastegateCoeff * (large ? kBlowOffLargeFactor : 1.0) * o * (finite_or(p_in, 1.0) - finite_or(p_out, 1.0)) * dt, v_in, v_out);
}

inline std::string blow_off_entry(bool large, const std::string& mesh) {
    std::string pipe = large ? ", \"size\": 1" : "";
    std::string last = large ? "1" : "0", top = large ? "1" : "0";
    // Data ports on both sides of every cell; the torque node (unused) underneath.
    std::string surfaces = "{\"id\": \"torque\", \"dir\": 2, \"type\": \"torque\"}";
    for (int z = 0; z <= (large ? 1 : 0); ++z)
        for (int y = 0; y <= (large ? 1 : 0); ++y) {
            surfaces += ", {\"pos\": [0, " + std::to_string(y) + ", " + std::to_string(z) + "], \"dir\": 0, \"gender\": 2, \"type\": \"port\"}";
            surfaces += ", {\"pos\": [" + top + ", " + std::to_string(y) + ", " + std::to_string(z) + "], \"dir\": 1, \"gender\": 2, \"type\": \"port\"}";
        }
    std::string box = large ? "[1, 1, 1]" : "[0, 0, 0]";
    std::string description = std::string("Inline blow-off valve for a Turbo, ") + (large ? "large pipe (the 6x6 turbo)" : "small pipe (the 3x3 turbo)") +
        ". Pipe the turbo's compressor outlet into its plain end and its blue-ringed Air end into the air manifold. When you lift "
        "off the throttle it vents the trapped boost out through its top so the turbo does not surge and keeps its spin. Paint the "
        "body and the cap separately. Properties Tool: Stiffness (kPa of extra boost before it opens) and Sound effect (0 whoosh, 1 flutter, 2 chirp, "
        "3 dump). Outputs open (0/1), flow (L/s) and pressure (kPa).";
    return std::string("    {\n") +
        "      \"id\": \"" + (large ? kBlowOffLargeId : kBlowOffId) + "\",\n" +
        "      \"name\": \"" + (large ? "Turbo Blow-Off Valve (Large)" : "Turbo Blow-Off Valve (Small)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [{\"bounds_min\": [0, 0, 0], \"bounds_max\": " + box + "}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [" + surfaces + "],\n" +
        "      \"logic_nodes\": [{\"pos\": [0, 0, 0], \"direction\": 4, \"type\": \"gas\"" + pipe + "}, {\"id\": \"air\", \"pos\": [0, 0, " + last + "], \"direction\": 5, \"type\": \"gas\"" + pipe + "}],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [0.0, 0.0, 0.0], \"p1\": " + (large ? "[1.0, 1.0, 1.0]" : "[0.0, 0.0, 0.0]") + "}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [{\"type\": \"type_f64_output\", \"name\": \"open\"}, {\"type\": \"type_f64_output\", \"name\": \"flow\"}, {\"type\": \"type_f64_output\", \"name\": \"pressure\"}],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string blow_off_mesh_path(bool large) { return std::string("meshes/components/turbo_") + (large ? kBlowOffLargeId : kBlowOffId) + ".mesh"; }

inline std::string wastegate_entry(bool large, const std::string& mesh) {
    std::string pipe = large ? ", \"size\": 1" : "";
    std::string descriptors = "{\"type\": \"type_f64_input\", \"name\": \"open\"}, {\"type\": \"type_f64_input\", \"name\": \"antilag\"}, "
        "{\"type\": \"type_f64_output\", \"name\": \"flow\"}, {\"type\": \"type_f64_output\", \"name\": \"pressure\"}, "
        "{\"type\": \"type_f64_input\", \"name\": \"two_step\"}";
    std::string description = std::string("Electronic wastegate for a Turbo, ") + (large ? "large pipe" : "small pipe") +
        ". Exhaust from the manifold goes in underneath and out the side, to a vent or the turbo's exhaust out. Its open input "
        "(0 to 1, from a data link or microcontroller) lets exhaust bypass the turbine, which caps boost. Its antilag input (1 on, "
        "0 off) burns fuel in the exhaust off throttle so the turbo stays spooled, with bangs and pops. Its two_step input "
        "(an engine RPM, 0 off) is launch control: the engine the turbo boosts holds at that RPM, and with antilag on the "
        "boost builds on the line. Outputs flow (L/s) and pressure (kPa). Paint the can and the cap separately.";
    // Data ports on the -x, -z and +z faces of every cell (not the -y inlet or the +x outlet face); the torque node the
    // impeller class asks for (unused) on top.
    std::string surfaces = std::string("{\"id\": \"torque\", \"pos\": [0, ") + (large ? "1" : "0") + ", 0], \"dir\": 3, \"type\": \"torque\"}";
    for (int x = 0; x <= (large ? 1 : 0); ++x)
        for (int y = 0; y <= (large ? 1 : 0); ++y)
            for (int z = 0; z <= (large ? 1 : 0); ++z) {
                std::string pos = "[" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "]";
                if (x == 0) surfaces += ", {\"pos\": " + pos + ", \"dir\": 0, \"gender\": 2, \"type\": \"port\"}";
                if (z == 0) surfaces += ", {\"pos\": " + pos + ", \"dir\": 4, \"gender\": 2, \"type\": \"port\"}";
                if (z == (large ? 1 : 0)) surfaces += ", {\"pos\": " + pos + ", \"dir\": 5, \"gender\": 2, \"type\": \"port\"}";
            }
    std::string box = large ? "[1, 1, 1]" : "[0, 0, 0]", side = large ? "[1, 0, 0]" : "[0, 0, 0]";
    return std::string("    {\n") +
        "      \"id\": \"" + (large ? kWastegateLargeId : kWastegateId) + "\",\n" +
        "      \"name\": \"" + (large ? "Turbo Wastegate (Large)" : "Turbo Wastegate (Small)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [{\"bounds_min\": [0, 0, 0], \"bounds_max\": " + box + "}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [" + surfaces + "],\n" +
        "      \"logic_nodes\": [{\"pos\": [0, 0, 0], \"direction\": 2, \"type\": \"gas\"" + pipe + "}, {\"pos\": " + side + ", \"direction\": 1, \"type\": \"gas\"" + pipe + "}],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [0.0, 0.0, 0.0], \"p1\": " + (large ? "[1.0, 1.0, 1.0]" : "[0.0, 0.0, 0.0]") + "}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [" + descriptors + "],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string wastegate_mesh_path(bool large) { return std::string("meshes/components/turbo_") + (large ? kWastegateLargeId : kWastegateId) + ".mesh"; }
constexpr const char* kValveMeshPath = "meshes/components/gas_port_a.mesh";
// Air filter: a cone filter for the compressor intake. It is a gas vent to the game (class gas_vent / gas_vent_b,
// so it draws outside air like one); a turbo whose intake pipe has one on it counts as filtered.
inline std::string air_filter_entry(bool large, const std::string& mesh) {
    // The small filter is 3 x 3 x 3 cells and the large 4 x 4 x 4 (as turbo_upkeep_mesh.h's air_filter_cells), its pipe in
    // the middle of the bottom face: the small pipe under cell 1, the large (2 x 2, anchored at its low corner) under
    // cells 1..2. Every face but the top takes other parts.
    const std::string n = large ? "4" : "3", last = large ? "3" : "2", size = "\"size\": [" + n + ", " + n + "]";
    std::string surfaces = "{" + size + "}, {\"pos\": [0, 0, " + last + "], " + size + ", \"dir\": 5}, {" + size + ", \"dir\": 4}, {\"pos\": [" + last +
        ", 0, 0], " + size + ", \"dir\": 1}, {" + size + ", \"dir\": 2}";
    std::string description = std::string("Cone air filter for a Turbo's compressor intake, ") + (large ? "large pipe (the 6x6 turbo)" : "small pipe (the 3x3 turbo)") +
        ". Pipe its neck to the turbo's intake (front centre). Clean air makes the turbo wear several times slower. It clogs slowly "
        "with use and a clogged filter costs a little boost; place a new one to replace it.";
    return std::string("    {\n") +
        "      \"id\": \"" + (large ? kAirFilterLargeId : kAirFilterId) + "\",\n" +
        "      \"name\": \"" + (large ? "Turbo Air Filter (Large)" : "Turbo Air Filter (Small)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"" + (large ? "gas_vent_b" : "gas_vent") + "\",\n" +
        "      \"zones\": [{\"bounds_max\": [" + last + ", " + last + ", " + last + "]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [" + surfaces + "],\n" +
        "      \"logic_nodes\": [{\"pos\": [1, 0, 1], \"direction\": 2, \"type\": \"gas\"" + (large ? ", \"size\": 1" : "") + "}],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"inspection_geometry\": [{\"p1\": [" + last + ".0, " + last + ".0, " + last + ".0]}],\n" +
        "      \"data_descriptors\": [],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string air_filter_mesh_path(bool large) { return std::string("meshes/components/turbo_") + (large ? kAirFilterLargeId : kAirFilterId) + ".mesh"; }

// Intercooler: grows like the stock radiator (wider in whole cells along x, its end tanks staying put; taller along y) and, like it, has no
// pipes of its own: gas ports snap onto any face, small or large. It is a stock gas tank underneath (class gas_tank),
// so every port on it shares one volume of air and the boost passes through; the turbo feeding it works out how much
// heat it takes out (turbo_health.h).
// Two of them, picked by Nate from five looks (turbo_upkeep_mesh.h): the black race core (turbo_intercooler) and the
// round-tank front mount (turbo_intercooler_round). Same part otherwise.
inline const char* intercooler_id(bool round) { return round ? kIntercoolerRoundId : kIntercoolerId; }
inline std::string intercooler_mesh_path(int tx = 0, int ty = 0, bool round = false) {
    return std::string("meshes/components/turbo_") + intercooler_id(round) + "_" + std::to_string(tx) + "_" + std::to_string(ty) + "_0.mesh";
}
inline std::string intercooler_entry(bool built, bool round = false) {
    std::string description = std::string("Air-to-air intercooler for a Turbo. Snap gas ports onto it (small or large) like a radiator: one for "
        "the turbo's boost, one to the engine's air intake. Compressing air heats it, and hot air makes less power and can knock "
        "the engine; the intercooler cools the boost back towards the outside air. Stretch it wider and taller for more cooling. "
        "Paint the tanks.");
    // One cell deep, 3 x 2 like the stock radiator, with its port faces.
    std::string surfaces = "{\"pos\": [-1, 0, 0], \"size\": [3, 2], \"dir\": 4, \"gender\": 2, \"type\": \"port\"}, "
        "{\"pos\": [-1, 0, 0], \"size\": [3, 2], \"dir\": 5, \"gender\": 2, \"type\": \"port\"}, "
        "{\"pos\": [-1, 1, 0], \"size\": [3, 1], \"dir\": 3, \"gender\": 2, \"type\": \"port\"}, "
        "{\"pos\": [-1, 0, 0], \"size\": [3, 1], \"dir\": 2, \"gender\": 2, \"type\": \"port\"}, "
        "{\"pos\": [1, 0, 0], \"size\": [1, 2], \"dir\": 1, \"gender\": 2, \"type\": \"port\"}, "
        "{\"pos\": [-1, 0, 0], \"size\": [1, 2], \"gender\": 2, \"type\": \"port\"}";
    return std::string("    {\n") +
        "      \"id\": \"" + intercooler_id(round) + "\",\n" +
        "      \"name\": \"" + (round ? "Turbo Intercooler (Round Tank)" : "Turbo Intercooler (Race Core)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_tank\",\n" +
        "      \"zones\": [{\"bounds_min\": [-1, 0, 0], \"bounds_max\": [1, 1, 0]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [" + surfaces + "],\n" +
        "      \"logic_nodes\": [],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + (built ? intercooler_mesh_path(0, 0, round) : std::string("meshes/components/radiator_a_0_0_0.mesh")) + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        // our tiles repeat both ways, so nothing stretches (the first x tile covers cells -1 and 0, see
        // turbo_upkeep_mesh.h); without them, fall back on the radiator's look and its own layout (its width
        // stretches, its tiles run along y)
        "      \"mode_x\": \"" + (built ? "tile" : "stretch") + "\",\n" +
        "      \"mode_y\": \"tile\",\n" +
        "      \"interval\": [1, 1, 0],\n" +
        "      \"center_stretch\": [0.0, 0.5, 0.0],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags_generated\": \"port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [-1.0, 0.0, 0.0], \"p1\": [1.0, 1.0, 0.0], \"radius\": 0.37}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 10,\n" +
        "      \"data_descriptors\": [],\n" +
        "      \"tech_tier\": 3,\n" +
        "      \"spawn_probability\": 0.1,\n" +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]]\n" +
        "    }";
}


// The definitions file: both turbo sizes, the two blow-off valves, the two wastegates, the two air filters and the
// intercooler. built[n - kMinSize] says whether that size's mesh was written (else the stock small impeller's look);
// bov_built[large], wg_built[large] and filter_built[large] the same for the valves and filters, cooler_built for the
// intercoolers' tiles (else the radiator's look).
inline std::string definitions_json(const bool built[kMaxSize - kMinSize + 1], const bool bov_built[2] = nullptr, const bool wg_built[2] = nullptr,
                                    const bool filter_built[2] = nullptr, const bool* cooler_built = nullptr, const bool blower_built[2] = nullptr) {
    std::string out = "{\n  \"definitions\": [\n";
    for (int n : kTurboSizes)
        out += definition_entry(n, built[n - kMinSize] ? mesh_path(n) : std::string(kStockMeshPath)) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += blow_off_entry(large, bov_built && bov_built[large] ? blow_off_mesh_path(large) : std::string(kValveMeshPath)) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += wastegate_entry(large, wg_built && wg_built[large] ? wastegate_mesh_path(large) : std::string(kValveMeshPath)) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += air_filter_entry(large, filter_built && filter_built[large] ? air_filter_mesh_path(large) : std::string(large ? "meshes/components/gas_port_b.mesh" : kValveMeshPath)) + ",\n";
    out += intercooler_entry(cooler_built && *cooler_built) + ",\n";
    out += intercooler_entry(cooler_built && *cooler_built, true) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += blower_entry(large, blower_built && blower_built[large] ? blower_mesh_path(large) : std::string(kStockMeshPath)) + (large ? "\n" : ",\n");
    return out + "  ]\n}\n";
}

// ------------------------------------------------------------------------------------------- boost setup
// What the Dyno calls an engine's (or a vehicle's) boost: the turbos and superchargers feeding it, and whether any
// turbo breathes another turbo's boost (a compound).
struct BoostSetup { int turbos = 0, superchargers = 0; bool compound = false; };
inline std::string boost_setup_name(const BoostSetup& b) {
    static const char* const kCount[] = {"", "Single", "Twin", "Triple", "Quad"};
    auto many = [](int n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); };
    int t = std::max(0, b.turbos), c = std::max(0, b.superchargers);
    if (t == 0 && c == 0) return "Naturally aspirated";
    if (t > 0 && c > 0) return t == 1 && c == 1 ? "Twincharged" : "Twincharged: " + many(t, "turbo") + ", " + many(c, "supercharger");
    if (c > 0) return c == 1 ? "Supercharged" : c <= 4 ? std::string(kCount[c]) + " supercharged" : many(c, "supercharger");
    if (b.compound && t > 1) return t == 2 ? "Compound turbo" : "Compound turbo: " + many(t, "turbo");
    return t <= 4 ? std::string(kCount[t]) + " turbo" : many(t, "turbo");
}

}  // namespace turbo
