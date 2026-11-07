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
#include <vector>

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
// The game's engines stop at a pressure of 10 (2x power). AnyVehicleOverhaul carries the engine's factor on past it
// with no boost cap (Nate, 2026-11-04): boost is whatever the turbos can push into what the engine swallows, and each
// engine's own rating (engine_rating, turbo_health.h) decides what that boost does to it. kMaxEnginePressure is only a
// safety net against runaway numbers (pressure 60 is about 590 kPa, 85 psi, a 6.8x factor): no real setup gets there.
constexpr double kStockEnginePressure = 10.0;
constexpr double kMaxEnginePressure = 60.0;
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
constexpr double kOverspeed = 1.12;        // the shaft's speed limit over its default (wastegate spring) boost speed,
                                           // before 2026-11; now each size has its own (Spec::max_speed)
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
// Boost: the built-in wastegate's spring holds kFullBoostRatio (88 kPa, 12.8 psi) unless a boost target asks for more
// (`boost_target_psi`, or a Boost Controller on the boost pipe). The compressor can make up to max_ratio, at
// max_speed (its shaft's speed limit over its spring-boost speed): what a real compressor wheel of that size can do.
// Whether it gets there is down to the exhaust: past the spring boost the shaft needs a lot more energy.
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
    double max_ratio = kFullBoostRatio;   // the compressor's best pressure ratio, at max_speed
    double max_speed = 1.0;       // shaft speed limit, over the spring-boost speed
    bool large_pipe = false;
    bool twin_scroll = false;     // divided turbine housing: more pulse energy at low flow, a little less at the top
};
// The shaft speed (over the spring-boost speed) a stage needs for a pressure ratio: head goes with speed squared.
inline double speed_for_ratio(double ratio) {
    return std::sqrt(std::max(0.0, ratio - 1.0) / (kFullBoostRatio - 1.0));
}
// The turbine's swallowing capacity scales with the face area and the shaft's energy with size: the 6x6 suits the
// 4-rotor (it holds full boost from about 2,500 RPM there, steady), the 3x3 the 2-rotor. At its rated flow the hot side
// drops 2 units (1 bar), under the 4 units where engines lose power; more than that and the built-in wastegate opens.
// Twin scroll (Nate 2026-11-04): the divided housing keeps the cylinders' exhaust pulses apart, so at low exhaust flow
// the turbine gets more of their energy (torque up to kTwinPulseGain more, fading out by the rated flow), and the
// divider's extra wall costs a little of the top end (best ratio kTwinTopEnd lower).
constexpr double kTwinPulseGain = 0.6, kTwinTopEnd = 0.97;
inline double twin_pulse_gain(const Spec& s, double exhaust_lps) {
    if (!s.twin_scroll || !(s.rated_flow > 0)) return 1.0;
    double x = std::clamp(exhaust_lps / s.rated_flow, 0.0, 1.0);
    return 1.0 + kTwinPulseGain * (1.0 - x);
}
inline Spec spec(int n) {
    n = std::clamp(n, kMinSize, kMaxSize);
    // 2x2 to 6x6; the 6x6 has the 5x5's numbers (it grew for its pipes, not its power).
    static constexpr double rated[] = {10, 22, 40, 65, 65};
    static constexpr double energy_kj[] = {0.3, 0.6, 1.0, 1.5, 1.5};
    static constexpr double rpm[] = {220000, 160000, 120000, 90000, 90000};
    // Best pressure ratio by size: a small wheel tops out near 3.3, a big one near 4.2 (real compressor maps).
    static constexpr double ratio[] = {3.0, 3.3, 3.6, 3.9, 4.2};
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
    s.max_ratio = ratio[n - kMinSize];
    s.max_speed = speed_for_ratio(s.max_ratio);
    return s;
}
inline Spec spec(int n, bool twin) {
    Spec s = spec(n);
    if (twin) {
        s.twin_scroll = true;
        s.max_ratio = 1.0 + (s.max_ratio - 1.0) * kTwinTopEnd;
        s.max_speed = speed_for_ratio(s.max_ratio);
    }
    return s;
}
// Turbo looks (Nate 2026-11-04 "add all those in as variant options"): each size comes in three looks that work the
// same. Look 0 keeps the old id, so saved turbos load as the G-series look.
constexpr int kTurboLooks = 3;
inline constexpr const char* kTurboLookIds[kTurboLooks] = {"", "_precision", "_efr"};
inline constexpr const char* kTurboLookNames[kTurboLooks] = {"G-Series", "Precision", "EFR"};
// A twin-scroll version of each look adds "_twin" (it spools quicker, see twin_pulse_gain).
constexpr const char* kTwinSuffix = "_twin";
inline std::string definition_id(int n, int look = 0, bool twin = false) {
    return std::string(kIdPrefix) + char('0' + n) + kTurboLookIds[look < 0 || look >= kTurboLooks ? 0 : look] + (twin ? kTwinSuffix : "");
}
inline bool turbo_twin_from_id(const char* id, size_t length) {
    size_t sl = std::char_traits<char>::length(kTwinSuffix);
    return id && length > sl && std::char_traits<char>::compare(id + length - sl, kTwinSuffix, sl) == 0;
}
inline int turbo_look_from_id(const char* id, size_t length) {
    if (turbo_twin_from_id(id, length)) length -= std::char_traits<char>::length(kTwinSuffix);
    for (int look = 1; look < kTurboLooks; ++look) {
        size_t sl = std::char_traits<char>::length(kTurboLookIds[look]);
        if (id && length > sl && std::char_traits<char>::compare(id + length - sl, kTurboLookIds[look], sl) == 0) return look;
    }
    return 0;
}
// Survival loot tiers. World loot drops a part while the world's loot level equals its tech tier; a new world starts
// at loot level 1 and finishing each bunker raises it by one (game code, Anymaker 0.1.25). The starter parts drop
// after the 2nd bunker and the rest after the 3rd (Nate, 2026-10-17).
constexpr int kStarterTier = 3;   // Turbo 3x3 and the small supercharger, wastegate, blow-off valve and air filter
constexpr int kLateTier = 4;      // every other AnyEngineParts part (Rotary and Dyno too)
// spawn_probability is the part's share of a world loot roll next to the other parts and items of its tier, so it sets
// how often the part turns up. AnyEngineParts sits about a quarter below the typical stock part of the same tier (Nate,
// 2026-10-21: engine parts "a bit more rare than other stuff"; Anymaker 0.1.27 loot code and part data).
constexpr const char* kStarterSpawn = "0.15";
constexpr const char* kLateSpawn = "0.075";
inline std::string tech_tier_line(int tier) { return "      \"tech_tier\": " + std::to_string(tier) + ",\n"; }
inline std::string loot_lines(int tier) {
    return tech_tier_line(tier) + "      \"spawn_probability\": " + (tier == kStarterTier ? kStarterSpawn : kLateSpawn) + ",\n";
}
// Returns 3 or 6 for a turbo definition id, else 0.
inline int size_from_id(const char* id, size_t length) {
    size_t p = std::char_traits<char>::length(kIdPrefix);
    if (!id || length < p + 1 || std::char_traits<char>::compare(id, kIdPrefix, p) != 0) return 0;
    int n = id[p] - '0';
    if (!turbo_offered(n)) return 0;
    if (turbo_twin_from_id(id, length)) length -= std::char_traits<char>::length(kTwinSuffix);
    for (int look = 0; look < kTurboLooks; ++look) {
        size_t sl = std::char_traits<char>::length(kTurboLookIds[look]);
        if (length == p + 1 + sl && std::char_traits<char>::compare(id + p + 1, kTurboLookIds[look], sl) == 0) return n;
    }
    return 0;
}

// Gas logic nodes, in definition order. 0 and 1 are the stock impeller's own (inlet, outlet); 2 and 3 are
// the hot side the mod adds at runtime.
enum Node { kColdIn, kColdOut, kHotIn, kHotOut, kNodeCount };

// Data channels.
// New channels go at the end: saved data links refer to them by position.
enum Channel { kIntakePressure, kExhaustPressure, kPowerMultiplier, kSpool, kRpm, kExhaustFlow, kIntakeFlow, kExhaustTemperature, kSurge, kBoost, kBoostPsi, kAntilagFiring, kBoostLimit,
               kOilFlow, kTurboTemperature, kCondition, kAirFiltered, kBoostAirTemperature, kDrivePower, kBypass, kBoostTarget,
               kBoostTargetSet, kChannelCount };
constexpr int kFirstInput = kBoostLimit;
struct ChannelInfo { const char* name; bool input; };
inline constexpr ChannelInfo kChannels[kChannelCount] = {
    {"intake_pressure", false},     // compressor outlet, kPa on the game's scale (98 = full boost)
    {"exhaust_pressure", false},    // turbine inlet, same scale (39 starts to cut engine power)
    {"power_multiplier", false},    // engine power factor the intake pressure gives: 1 none, 2 at 88 kPa, no cap
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
    {"wastegate", false},           // 0-1, how far the turbo's built-in wastegate is open (it holds the boost target)
    {"boost_target_psi", true},     // input: boost (psi over the air outside) the built-in wastegate holds; 0 = its spring (12.8)
    {"boost_target", false},        // the boost target it holds now, psi (spring, this input, or a Boost Controller)
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
        "and boost climbs with the square of the shaft's speed, so the more exhaust, the quicker it spools. A built-in wastegate holds 88 kPa "
        "(12.8 psi, double engine power); a Boost Controller on the boost pipe, or the boost_target_psi input, turns it up to about " +
        fmt(std::round((s.max_ratio - 1.0) * kAmbientKpa * kPsiPerKpa)) + " psi if the exhaust can spin it that fast. Twins share the air, so a big engine holds more boost on more turbos. " + std::string(n >= 6 ? "Suits a 3- or 4-rotor or a long small engine; on a small one it barely spools. "
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

// A plain mounting face (no type, like the stock frame faces and the air filter's): the part bolts to beams, plates
// and other parts there. `pos` is the face's lowest cell, `size` its extent across the face: (z, y) on an x face,
// (x, z) on a y face, (x, y) on a z face.
inline std::string mount_face(int x, int y, int z, int dir, int su = 1, int sw = 1) {
    return ", {\"pos\": [" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "], \"size\": [" + std::to_string(su) + ", " +
           std::to_string(sw) + "], \"dir\": " + std::to_string(dir) + "}";
}
// Every cell of a face of a box (0..cx-1, 0..cy-1, 0..cz-1) as mounting face, except one cell (skip_u, skip_w; -1 none).
inline std::string mount_cells(int cx, int cy, int cz, int dir, int skip_u = -1, int skip_w = -1) {
    std::string out;
    int axis = dir / 2, hi = dir % 2;
    int nu = axis == 0 ? cz : cx, nw = axis == 1 ? cz : cy;
    for (int u = 0; u < nu; ++u)
        for (int w = 0; w < nw; ++w) {
            if (u == skip_u && w == skip_w) continue;
            int x = axis == 0 ? (hi ? cx - 1 : 0) : u, y = axis == 1 ? (hi ? cy - 1 : 0) : w, z = axis == 2 ? (hi ? cz - 1 : 0) : axis == 0 ? u : w;
            out += mount_face(x, y, z, dir);
        }
    return out;
}

// Mounting faces on the given faces of a box (cells 0..cx-1, 0..cy-1, 0..cz-1), one per cell, leaving out every cell
// a pipe or oil node (or the torque spot) sits on, so a mount never covers a port (audited 2026-11-04).
struct FaceCell { int x, y, z, dir, span = 1; };
inline bool face_covers(const FaceCell& f, int x, int y, int z, int dir) {
    if (f.dir != dir) return false;
    auto in = [&](int v, int lo) { return v >= lo && v < lo + f.span; };
    if (dir <= 1) return x == f.x && in(y, f.y) && in(z, f.z);
    if (dir <= 3) return y == f.y && in(x, f.x) && in(z, f.z);
    return z == f.z && in(x, f.x) && in(y, f.y);
}
inline std::string mount_box(int cx, int cy, int cz, std::initializer_list<int> dirs, const std::vector<FaceCell>& skip) {
    std::string out;
    for (int dir : dirs) {
        int axis = dir / 2, hi = dir % 2;
        for (int x = 0; x < cx; ++x)
            for (int y = 0; y < cy; ++y)
                for (int z = 0; z < cz; ++z) {
                    int v = axis == 0 ? x : axis == 1 ? y : z, last = (axis == 0 ? cx : axis == 1 ? cy : cz) - 1;
                    if (v != (hi ? last : 0)) continue;
                    bool blocked = false;
                    for (const FaceCell& f : skip) blocked = blocked || face_covers(f, x, y, z, dir);
                    if (!blocked) out += mount_face(x, y, z, dir);
                }
    }
    return out;
}

// One part definition: a resized stock impeller (class gas_pump_centrifugal) with a hot side.
inline std::string definition_entry(int n, const std::string& mesh_path, int look = 0, bool twin = false) {
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
    // Mounting faces (Nate 2026-11-04): the bottom (but the torque cell), the top, the front and the back.
    {
        int span = s.large_pipe ? 2 : 1;
        std::vector<FaceCell> skip = {{0, 0, n == 2 ? 0 : top, 2}};
        for (Node k : {kColdOut, kHotIn}) { Port p = port(n, k); skip.push_back({p.x, p.y, p.z, p.dir, span}); }
        for (bool in : {true, false}) { Port p = oil_port(n, in); skip.push_back({p.x, p.y, p.z, p.dir}); }
        surfaces += mount_box(2, n, n, {2, 3, 4, 5}, skip);
    }
    std::string descriptors;
    for (int i = 0; i < kChannelCount; ++i)
        descriptors += std::string(i ? ",\n        " : "") + "{\"type\": \"" + (kChannels[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + kChannels[i].name + "\"}";
    std::string box = "[1, " + std::to_string(top) + ", " + std::to_string(top) + "]";
    return std::string("    {\n") +
        "      \"id\": \"" + definition_id(n, look, twin) + "\",\n" +
        "      \"name\": \"Turbo " + std::to_string(n) + "x" + std::to_string(n) + " (" + kTurboLookNames[look] + (twin ? ", Twin Scroll" : "") + ")\",\n" +
        "      \"description\": \"" + description(n) + (twin ? " Twin scroll: a divided turbine housing that keeps the exhaust pulses apart, so it spools quicker and makes boost lower in the rev range, for a little less boost at the very top." : "") + "\",\n" +
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
        loot_lines(n == 3 ? kStarterTier : kLateTier) +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string mesh_path(int n, int look = 0, bool twin = false) { return "meshes/components/turbo_" + definition_id(n, look, twin) + ".mesh"; }
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
    double target_pressure = 1;  // the outlet pressure its wastegate holds (game units)
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
    double controller_psi = 0;    // boost target from a Boost Controller on its boost pipe (0: none), set by the mod
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

// The outlet pressure (game units) a turbo's built-in wastegate holds, breathing from `inlet_pressure`.
inline double boost_target_pressure(const State& st, const Spec& s, double inlet_pressure) {
    double inlet_kpa = boost_kpa(std::max(1.0, finite_or(inlet_pressure, 1.0)));
    auto at_ratio = [&](double ratio) { return 1.0 + (inlet_kpa * ratio - kAmbientKpa) / kKpaPerUnit; };
    double target = at_ratio(kFullBoostRatio);
    double psi = finite_or(st.slots[kBoostTarget], 0.0);
    if (!(psi > 0)) psi = finite_or(st.controller_psi, 0.0);
    if (psi > 0) target = std::min(at_ratio(s.max_ratio), 1.0 + psi / kPsiPerKpa / kKpaPerUnit);
    double limit = finite_or(st.slots[kBoostLimit], 0.0);
    if (limit > 0) target = std::min(target, 1.0 + limit / kKpaPerUnit);
    return std::clamp(target, 1.0, kMaxEnginePressure);
}

inline Step step(State& st, const Spec& s, const Inputs& in) {
    Step out;
    double dt = std::clamp(finite_or(in.dt, 0.0), 0.0, 0.25);
    if (dt <= 0) return out;
    double p[kNodeCount];
    for (int i = 0; i < kNodeCount; ++i) p[i] = std::max(0.0, finite_or(in.pressure[i], 1.0));
    bool dead = st.blown || !(st.health > 0);     // the shaft no longer turns
    st.speed = std::clamp(finite_or(st.speed, 0.0), 0.0, s.max_speed);
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
    // compressor needs is the target plus its droop at this flow), and never past its speed limit. The target is its
    // spring's (a ratio of 1.87 over its inlet), or the boost target (psi over the outside air at its outlet, from the
    // `boost_target_psi` input or a Boost Controller), which can ask for up to the compressor's best ratio; the boost
    // limit input caps it.
    out.target_pressure = boost_target_pressure(st, s, p[kColdIn]);
    double target_out = out.target_pressure;
    double per_speed2 = boost_kpa(p[kColdIn]) / kKpaPerUnit * (kFullBoostRatio - 1.0) * std::max(0.05, std::clamp(finite_or(st.health, 1.0), 0.0, 1.0));
    double need = (std::max(0.0, target_out - p[kColdIn]) + std::max(0.0, flow) / (s.compressor_coeff * density)) / per_speed2;
    double cap = s.shaft_full * std::min(s.max_speed, std::sqrt(std::max(0.0, need)));
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
            turbine = std::max(0.0, kTurbineEfficiency * exhaust_kg * work * 4.0 * r / (runaway * spout) * (1.0 - u / runaway)) * twin_pulse_gain(s, st.exhaust_flow);
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
    st.speed = std::clamp(finite_or(omega / s.shaft_full, 0.0), 0.0, s.max_speed);
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
    st.slots[kBoostTargetSet] = (out.target_pressure - 1.0) * kKpaPerUnit * kPsiPerKpa;
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
    // Mounting faces (Nate 2026-11-04): bottom, top, front, and the back but its torque cell (x 0, y 0).
    surfaces += mount_box(n, n, n, {2, 3, 4, 5}, {{0, 0, top, 5}, large ? FaceCell{1, 1, 0, 4, 2} : FaceCell{0, 0, 0, 4, 2},
                                                  large ? FaceCell{2, 3, 0, 3, 2} : FaceCell{1, 1, 0, 3}});
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
        loot_lines(large ? kLateTier : kStarterTier) +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}

// ------------------------------------------------------------------------------------------- positive displacement
// Twin screw and Roots superchargers: blowers that sit on top of the engine and stretch to fit it (width and length to
// 16 cells, height 2 or 3). Belt driven like the centrifugal ones (a stock Pulley Wheel on the torque spot, belted to an
// Engine Wheel), with a 2.5:1 drive gear inside, so the rotors turn 1.0, 1.55 or 2.67 times engine speed on the small,
// medium or large Engine Wheel: the overdrive a real blower's pulleys set.
//
// A positive displacement blower moves a fixed volume of air per turn, so its boost is the ratio of what it moves to
// what the engine swallows: both go with engine speed, so boost comes in almost at once and stays flat across the rev
// range, the way a real twin screw or Roots does (bigger blower or faster drive: more boost). The game's engines draw
// air by their size, speed and throttle, not by the pressure, so the blower measures what the engine draws each tick
// and works out the pressure ratio a real one would make on that engine; it then fills the boost pipe towards it. Air
// leaks back past the rotors more as the boost rises and less as they spin faster (volumetric efficiency). Off
// throttle its bypass valve opens (the engine draws next to nothing), and a relief limit caps it (kPdMaxRatio).
// A Boost Controller (or the boost_target_psi input) sets a lower cap through the bypass.
//   Twin screw: compresses inside the rotors, so the air comes out cooler and takes less power (72 % efficient).
//   Roots: pushes air into the manifold without squeezing it first: hotter air and more belt power at high boost
//   (about 55 % efficient at 10 psi, worse above), leakier at low speed. The classic muscle blower.
enum PdKind { kTwinScrew = 0, kRoots = 1 };
constexpr const char* kTwinScrewId = "supercharger_twin_screw";
constexpr const char* kRootsId = "supercharger_roots";
inline const char* pd_id(PdKind k) { return k == kRoots ? kRootsId : kTwinScrewId; }
inline int pd_from_id(const char* id, size_t length) {   // -1: neither
    auto is = [&](const char* want) { return id && std::char_traits<char>::length(want) == length && std::char_traits<char>::compare(id, want, length) == 0; };
    return is(kTwinScrewId) ? kTwinScrew : is(kRootsId) ? kRoots : -1;
}
constexpr bool kOfferRoots = false;       // the Roots blower is on hold (Nate, 2026-11-04)
constexpr int kPdBase = 2;                   // cells per axis unstretched
constexpr int kPdMaxExtendXZ = 14, kPdMaxExtendY = 1;   // up to 16 x 3 x 16 cells
constexpr double kPdGear = 2.5;              // pulley turns per rotor turn
constexpr double kPdLitresPerCellRev = 0.014;   // air moved per rotor turn per cell of blower (game litres)
constexpr double kPdMaxRatio[2] = {3.0, 2.6};   // relief valve: twin screw ~29 psi, Roots ~23 psi
constexpr double kPdLeak[2] = {0.05, 0.09};     // volumetric efficiency lost per unit of pressure ratio over 1, at speed
constexpr double kPdLeakSpeedRps = 40.0;        // rotor speed (rev/s) where leakage is down to kPdLeak
constexpr double kPdBypassShare = 0.12;         // engine draw under this share of what the blower moves: throttle shut, bypass open
constexpr double kPdFillTau = 0.15;
constexpr double kPdIdleRpm = 1000.0, kPdBoostRpm = 1800.0;   // engine RPM where the bypass starts to close, and is shut             // seconds it takes to fill the boost pipe to its ratio
inline int pd_axis_cells(int32_t extend, int max_extend) {
    int64_t e = extend < 0 ? -int64_t(extend) : int64_t(extend);
    return kPdBase + int(e > max_extend ? max_extend : e);
}
inline int pd_cells(int32_t ex, int32_t ey, int32_t ez) {
    return pd_axis_cells(ex, kPdMaxExtendXZ) * pd_axis_cells(ey, kPdMaxExtendY) * pd_axis_cells(ez, kPdMaxExtendXZ);
}
// Air moved (game litres per second, at the inlet's pressure) at a pulley speed.
inline double pd_displacement_lps(int cells, double pulley_rps) { return kPdLitresPerCellRev * std::max(0, cells) * std::abs(finite_or(pulley_rps, 0.0)) / kPdGear; }
inline double pd_volumetric_efficiency(PdKind k, double ratio, double rotor_rps) {
    double speed = std::max(0.25, std::abs(finite_or(rotor_rps, 0.0)) / kPdLeakSpeedRps);
    return std::clamp(1.0 - kPdLeak[k] * std::max(0.0, ratio - 1.0) / std::sqrt(speed), 0.3, 1.0);
}
// Adiabatic efficiency: a twin screw's is near flat; a Roots' falls as the boost rises (it compresses against the
// manifold, not inside itself).
inline double pd_efficiency(PdKind k, double ratio) {
    if (k == kTwinScrew) return 0.72;
    double r = std::max(1.0001, finite_or(ratio, 1.0));
    double ideal = (std::pow(r, kAirExponent) - 1.0) / (kAirExponent * (r - 1.0));   // isentropic over isochoric work
    return std::clamp(0.70 * ideal, 0.3, 0.7);
}
// The game's engines' air at full throttle (research-engine-air.md): volume x 400 / 60 x clamp(rpm / 8000 + 0.2, 0, 1)
// x the boost factor / 9.6e6 of fuel a tick, each unit of fuel taking 8720 of air, at 60 ticks a second. What a real
// engine of that size swallows at an RPM is the part that goes with its speed.
constexpr double kEngineAirPerVolumeRpm = 400.0 / 60.0 / 9.6e6 * 8720.0 * 60.0 / 8000.0;
inline double engine_full_draw_lps(double volume, double rpm, double intake_pressure) {
    return volume * kEngineAirPerVolumeRpm * 8000.0 * std::clamp(std::abs(rpm) / 8000.0 + 0.2, 0.0, 1.0) * power_multiplier(intake_pressure);
}
inline double engine_swept_lps(double volume, double rpm) { return volume * kEngineAirPerVolumeRpm * std::abs(rpm); }
struct PdInputs {
    double engine_volume = 0;     // the engine it feeds (m_volume), 0: not known yet
    double engine_rpm = 0;
    double draw_lps = 0;          // what the engine took out of the boost pipe over the last tick (smoothed), L/s
    double target_pressure = 0;   // a cap from the Boost Controller / boost_target_psi (game units; 0: none)
    double capacity = 0;          // litres the boost pipe holds
};
// One tick: the cold ports only (kColdIn, kColdOut), as blower_step.
inline BlowerStep pd_step(State& st, PdKind k, int cells, const Inputs& in, const PdInputs& pd, double pulley_rps) {
    BlowerStep out;
    double dt = std::clamp(finite_or(in.dt, 0.0), 0.0, 0.25);
    if (dt <= 0) return out;
    double p_in = std::max(0.1, finite_or(in.pressure[kColdIn], 1.0)), p_out = std::max(0.0, finite_or(in.pressure[kColdOut], 1.0));
    double rotor_rps = std::abs(finite_or(pulley_rps, 0.0)) / kPdGear;
    double density = inlet_density(in.intake_temperature);
    double moved = pd_displacement_lps(cells, pulley_rps) * density * p_in;      // L/s of outside-air mass, at no leak
    double draw = std::max(0.0, finite_or(pd.draw_lps, 0.0));
    // What a real blower would make: what it moves over what the engine swallows at outside pressure (the game's
    // engines draw a little more as boost raises their power; take that back out).
    // With the engine known: its swept air at this RPM (flat boost), and its throttle from what it draws against what
    // it would at full throttle. Without: what it draws.
    bool known = pd.engine_volume > 0 && std::isfinite(pd.engine_volume) && std::isfinite(pd.engine_rpm) && std::abs(pd.engine_rpm) > 200;
    double swallowed = known ? engine_swept_lps(pd.engine_volume, pd.engine_rpm) : draw / power_multiplier(std::max(1.0, p_out));
    double throttle = known ? clamp01(draw / std::max(1e-6, engine_full_draw_lps(pd.engine_volume, pd.engine_rpm, std::max(1.0, p_out)))) : 1.0;
    double open = known ? clamp01((throttle - 0.25) / 0.35) : 1.0;     // the bypass closes as the throttle opens
    open = open * open * (3 - 2 * open);
    // Near idle the bypass stays open too, closing between kPdIdleRpm and kPdBoostRpm, so the belt load comes on as the
    // engine pulls away instead of all at once at idle (Nate 2026-11-07: tipping in from idle stalled the engine).
    if (known) {
        double rise = clamp01((std::abs(pd.engine_rpm) - kPdIdleRpm) / (kPdBoostRpm - kPdIdleRpm));
        open *= rise * rise * (3 - 2 * rise);
    }
    double ratio = 1.0;
    bool bypass = moved <= 1e-9 || (known ? open <= 0 : draw < kPdBypassShare * moved);
    if (!bypass) {
        ratio = moved / std::max(1e-6, swallowed);
        for (int i = 0; i < 3; ++i) ratio = moved * pd_volumetric_efficiency(k, ratio, rotor_rps) / std::max(1e-6, swallowed);   // leakage grows with boost
    }
    double kpa_in = boost_kpa(p_in);
    double relief = 1.0 + (kpa_in * kPdMaxRatio[k] - kAmbientKpa) / kKpaPerUnit;
    double target = bypass ? p_in : 1.0 + (kpa_in * std::max(1.0, ratio) - kAmbientKpa) / kKpaPerUnit;
    target = std::min(target, relief);
    if (!bypass && target > p_in) target = p_in + (target - p_in) * open;
    if (pd.target_pressure > 1.0) target = std::min(target, pd.target_pressure);
    double limit = finite_or(st.slots[kBoostLimit], 0.0);
    if (limit > 0) target = std::min(target, 1.0 + limit / kKpaPerUnit);
    target = std::clamp(target, std::min(p_in, 1.0), kMaxEnginePressure);
    // Fill towards it: replace what the engine drew and close the gap over kPdFillTau, never more than the rotors move.
    double capacity = pd.capacity > 0 && std::isfinite(pd.capacity) ? pd.capacity : (p_out > 1e-6 ? std::max(0.0, in.volume[kColdOut]) / p_out : 1.0);
    double want = (draw + (target - p_out) * capacity / kPdFillTau) * dt;
    // The game's engines take relatively more air at low RPM than their size says (the + 0.2 in their draw), so the
    // blower's delivery is counted on the same scale: it keeps up with the engine at every speed (flat boost).
    double scale = known ? std::clamp(std::abs(pd.engine_rpm) / 8000.0 + 0.2, 0.0, 1.0) / std::max(0.025, std::abs(pd.engine_rpm) / 8000.0) : 1.0;
    double most = scale * moved * pd_volumetric_efficiency(k, std::max(1.0, boost_kpa(p_out) / kpa_in), rotor_rps) * dt;
    if (p_out < p_in) most = std::max(most, (draw + (p_in - p_out) * capacity / kPdFillTau) * dt);   // too small for the engine: it sucks through
    want = std::clamp(want, 0.0, std::max(0.0, most));
    out.compressor_move = capped(want, in.volume[kColdIn], in.volume[kColdOut]);
    st.surging = false;   // a positive displacement blower cannot surge
    double flow = out.compressor_move / dt;
    double paid = working_flow(st, in, flow, dt);
    out.pressure_ratio = std::max(1.0, finite_or(boost_kpa(p_out) / std::max(1.0, kpa_in), 1.0));
    out.efficiency = pd_efficiency(k, out.pressure_ratio);
    out.head = std::max(0.0, target - p_in);
    // The rotors also churn the air the bypass sends round, a little.
    double churn = bypass ? 0.02 * moved * kKgPerLitre * kCpAir * 10.0 : 0.0;
    out.drive_joules = (compressor_watts(paid, p_in, p_out, in.intake_temperature, out.efficiency) + churn) / kBlowerDriveEfficiency * dt;
    double kf = std::min(1.0, dt / 0.15);
    st.intake_flow += (flow - st.intake_flow) * kf;
    st.speed = std::min(1.0, rotor_rps / (kBlowerFullBoostRps / kPdGear));
    st.spool = bypass ? 0.0 : clamp01((p_out - p_in) / std::max(1e-6, relief - p_in));
    st.slots[kIntakePressure] = p_out * kKpaPerUnit;
    st.slots[kPowerMultiplier] = power_multiplier(p_out);
    st.slots[kSpool] = st.spool;
    st.slots[kRpm] = rotor_rps * 60.0;
    st.slots[kIntakeFlow] = st.intake_flow;
    st.slots[kSurge] = 0.0;
    st.slots[kBoost] = (p_out - 1.0) * kKpaPerUnit;
    st.slots[kBoostPsi] = st.slots[kBoost] * kPsiPerKpa;
    st.slots[kCondition] = 1.0;
    st.slots[kDrivePower] = out.drive_joules / dt / 1000.0;
    st.slots[kBypass] = bypass ? 1.0 : clamp01(1.0 - flow / std::max(1e-6, moved));
    st.slots[kBoostTargetSet] = (target - 1.0) * kKpaPerUnit * kPsiPerKpa;
    return out;
}

inline std::string pd_mesh_path(PdKind k) { return std::string("meshes/components/turbo_") + pd_id(k) + ".mesh"; }
// Both: 2 x 2 x 2 cells unstretched, stretching like a tank: width (x) and length (z) to 16 cells, height (y) to 3.
// A large-pipe outlet on the bottom (the near corner, under the rotors' discharge), a large-pipe intake on the back face
// (twin screw: where its throttle body bolts on) or the top (Roots: under its carb hat or scoop), the torque spot on
// the front face, upper row (it rides up with the height), data ports on both sides.
inline std::string pd_entry(PdKind k, const std::string& mesh_path) {
    bool roots = k == kRoots;
    std::string description = std::string(roots ? "Roots blower, the classic muscle car supercharger. " : "Twin screw supercharger. ") +
        "Sits on top of the engine and stretches to fit it: wider and longer to 16 cells, 2 or 3 tall. A bigger blower moves more air for more boost. "
        "Bolt a Pulley Wheel onto its torque spot (the front face, top row) and belt it to an Engine Wheel: the small wheel turns it at engine speed, "
        "the medium 1.55 times, the large 2.67 times (more boost). Boost comes in from idle and stays flat to the redline. "
        "Outlet: the large pipe on the bottom, to the air manifold. Intake: the large pipe on the " + (roots ? "top" : "back face") + ", open to the air (an air filter fits). "
        "Its bypass opens off throttle. " +
        (roots ? "It squeezes the air in the manifold, not inside itself: hotter boost and more belt power than a twin screw, an intercooler helps. Relief at about 23 psi."
               : "It compresses inside the rotors: cooler boost and less belt power than a Roots. Relief at about 29 psi.") +
        " A Boost Controller caps its boost through the bypass.";
    std::string surfaces = "{\"id\": \"torque\", \"pos\": [0, 1, 0], \"dir\": 4, \"type\": \"torque\"},\n"
        "        {\"size\": [2, 2], \"gender\": 2, \"type\": \"port\"},\n"
        "        {\"pos\": [1, 0, 0], \"size\": [2, 2], \"dir\": 1, \"gender\": 2, \"type\": \"port\"}";
    // Mounting faces (Nate 2026-11-04): it bolts down by its bottom (onto the air manifold, over its outlet) and takes
    // parts on its top, back and the front but the torque cell (x 0, y 1).
    // Its whole back face is the intake (twin screw), so that face takes no mount.
    surfaces += mount_face(0, 0, 0, 2, 2, 2) + mount_box(2, 2, 2, {3, 4}, {{0, 1, 0, 4}, {0, 1, 0, 3, roots ? 2 : 0}});
    std::string in = roots ? "{\"pos\": [0, 1, 0], \"direction\": 3, \"type\": \"gas\", \"size\": 1}" : "{\"pos\": [0, 0, 1], \"direction\": 5, \"type\": \"gas\", \"size\": 1}";
    std::string out = "{\"pos\": [0, 0, 0], \"direction\": 2, \"type\": \"gas\", \"size\": 1}";
    std::string descriptors;
    for (int i = 0; i < kChannelCount; ++i)
        descriptors += std::string(i ? ",\n        " : "") + "{\"type\": \"" + (kChannels[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + kChannels[i].name + "\"}";
    return std::string("    {\n") +
        "      \"id\": \"" + pd_id(k) + "\",\n" +
        "      \"name\": \"" + (roots ? "Supercharger (Roots Blower)" : "Supercharger (Twin Screw)") + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [{\"bounds_max\": [1, 1, 1]}],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [\n        " + surfaces + "\n      ],\n" +
        "      \"logic_nodes\": [\n        " + in + ",\n        " + out + "\n      ],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh_path + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"mode_x\": \"stretch\",\n" +
        "      \"mode_y\": \"stretch\",\n" +
        "      \"mode_z\": \"stretch\",\n" +
        "      \"interval\": [1, 1, 1],\n" +
        "      \"ext_max\": [" + std::to_string(kPdMaxExtendXZ) + ", " + std::to_string(kPdMaxExtendY) + ", " + std::to_string(kPdMaxExtendXZ) + "],\n" +
        // the stretch planes (turbo_pd_mesh.h): clear of the intake flange across it, so the port stays round
        "      \"center_stretch\": " + (roots ? "[1.15, 0.5, 1.15]" : "[1.15, 1.15, 0.5]") + ",\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags\": \"torque\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [0.0, 0.0, 0.0], \"p1\": [1.0, 1.0, 1.0]}],\n" +
        "      \"density\": 900.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [\n        " + descriptors + "\n      ],\n" +
        loot_lines(kLateTier) +
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
// Looks v2 (Nate, 2026-11-04 "add all of the air filters and intercoolers in"): three of each, each its own part. The
// first two of each keep the old ids, so saved parts keep loading (they take the new looks).
constexpr int kFilterLooks = 3, kCoolerLooks = 3;
inline constexpr const char* kFilterLookIds[kFilterLooks] = {"", "_sock", "_drum"};
inline constexpr const char* kFilterLookNames[kFilterLooks] = {"Cone", "Mesh Sock", "Round Drum"};
inline constexpr const char* kCoolerIds[kCoolerLooks] = {"turbo_intercooler", "turbo_intercooler_round", "turbo_intercooler_race"};
inline constexpr const char* kCoolerNames[kCoolerLooks] = {"Bar and Plate", "Tube and Fin", "Fabricated Race"};
inline std::string air_filter_id(bool large, int look) { return std::string("turbo_air_filter") + kFilterLookIds[look] + (large ? "_large" : ""); }
// The look a filter or intercooler id has (0 if it is neither, or the first look).
inline int part_look_from_id(const char* id, size_t length) {
    std::string s(id ? id : "", id ? length : 0);
    if (s.rfind("turbo_air_filter", 0) == 0) return s.find("_sock") != std::string::npos ? 1 : s.find("_drum") != std::string::npos ? 2 : 0;
    for (int k = 0; k < kCoolerLooks; ++k) if (s == kCoolerIds[k]) return k;
    return 0;
}
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
inline Valve filter_from_id(const char* id, size_t length) {
    for (int look = 0; look < kFilterLooks; ++look)
        for (int large = 0; large < 2; ++large) {
            std::string want = air_filter_id(large != 0, look);
            if (want.size() == length && want.compare(0, length, id, length) == 0) return large ? kAirFilterLarge : kAirFilter;
        }
    return kNoValve;
}
inline Valve valve_from_id(const char* id, size_t length) {
    auto is = [&](const char* want) { return std::char_traits<char>::length(want) == length && std::char_traits<char>::compare(want, id, length) == 0; };
    return !id ? kNoValve : is(kBlowOffId) ? kBlowOff : is(kBlowOffLargeId) ? kBlowOffLarge : is(kWastegateId) ? kWastegate : is(kWastegateLargeId) ? kWastegateLarge
         : (is(kIntercoolerId) || is(kIntercoolerRoundId) || is(kCoolerIds[2])) ? kIntercooler : filter_from_id(id, length);
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
    {   // Mounting faces (Nate 2026-11-04): top, both ends, and the bottom but its torque cell.
        int n = large ? 2 : 1;
        surfaces += mount_box(n, n, n, {2, 3, 4, 5}, {{0, 0, 0, 2}, {0, 0, 0, 4, n}, {0, 0, n - 1, 5, n}});
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
        loot_lines(large ? kLateTier : kStarterTier) +
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
    {   // Mounting faces (Nate 2026-11-04): bottom, the outlet side, and the top but its torque cell.
        int n = large ? 2 : 1;
        surfaces += mount_face(0, 0, 0, 2, n, n) + mount_face(n - 1, 0, 0, 1, n, n) + mount_cells(n, n, n, 3, 0, 0);
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
        loot_lines(large ? kLateTier : kStarterTier) +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string wastegate_mesh_path(bool large) { return std::string("meshes/components/turbo_") + (large ? kWastegateLargeId : kWastegateId) + ".mesh"; }
constexpr const char* kValveMeshPath = "meshes/components/gas_port_a.mesh";
// Air filter: a cone filter for the compressor intake. It is a gas vent to the game (class gas_vent / gas_vent_b,
// so it draws outside air like one); a turbo whose intake pipe has one on it counts as filtered.
inline std::string air_filter_entry(bool large, const std::string& mesh, int look = 0) {
    // The small filter is 3 x 3 x 3 cells and the large 4 x 4 x 4 (as turbo_upkeep_mesh.h's air_filter_cells), its pipe in
    // the middle of the bottom face: the small pipe under cell 1, the large (2 x 2, anchored at its low corner) under
    // cells 1..2. Every face but the top takes other parts.
    const std::string n = large ? "4" : "3", last = large ? "3" : "2", size = "\"size\": [" + n + ", " + n + "]";
    const int cells = large ? 4 : 3;
    std::string surfaces = mount_box(cells, cells, cells, {0, 1, 2, 4, 5}, {{1, 0, 1, 2, large ? 2 : 1}}).substr(2);   // not under its neck
    (void)size;
    static constexpr const char* kKind[kFilterLooks] = {"Cone air filter", "Mesh sock air filter (a velocity stack in a sock)", "Round drum air filter"};
    std::string description = std::string(kKind[look]) + " for a Turbo's compressor intake, " + (large ? "large pipe (the 6x6 turbo)" : "small pipe (the 3x3 turbo)") +
        ". Pipe its neck to the turbo's intake (front centre). Clean air makes the turbo wear several times slower. It clogs slowly "
        "with use and a clogged filter costs a little boost; place a new one to replace it.";
    return std::string("    {\n") +
        "      \"id\": \"" + air_filter_id(large, look) + "\",\n" +
        "      \"name\": \"Turbo Air Filter, " + kFilterLookNames[look] + (large ? " (Large)" : " (Small)") + "\",\n" +
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
        loot_lines(large ? kLateTier : kStarterTier) +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string air_filter_mesh_path(bool large, int look = 0) { return "meshes/components/turbo_" + air_filter_id(large, look) + ".mesh"; }

// Intercooler: grows like the stock radiator (wider in whole cells along x, its end tanks staying put; taller along y) and, like it, has no
// pipes of its own: gas ports snap onto any face, small or large. It is a stock gas tank underneath (class gas_tank),
// so every port on it shares one volume of air and the boost passes through; the turbo feeding it works out how much
// heat it takes out (turbo_health.h).
// Two of them, picked by Nate from five looks (turbo_upkeep_mesh.h): the black race core (turbo_intercooler) and the
// round-tank front mount (turbo_intercooler_round). Same part otherwise.
inline const char* intercooler_id(int look) { return kCoolerIds[look < 0 || look >= kCoolerLooks ? 0 : look]; }
inline std::string intercooler_mesh_path(int tx = 0, int ty = 0, int look = 0) {
    return std::string("meshes/components/turbo_") + intercooler_id(look) + "_" + std::to_string(tx) + "_" + std::to_string(ty) + "_0.mesh";
}
inline std::string intercooler_entry(bool built, int round = 0) {
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
        "      \"name\": \"Turbo Intercooler (" + kCoolerNames[round < 0 || round >= kCoolerLooks ? 0 : round] + ")\",\n" +
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
        loot_lines(kLateTier) +
        "      \"loot_group\": \"mechanical\",\n" +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]]\n" +
        "    }";
}


// The definitions file: both turbo sizes, the two blow-off valves, the two wastegates, the two air filters and the
// intercooler. built[n - kMinSize] says whether that size's mesh was written (else the stock small impeller's look);
// bov_built[large], wg_built[large] and filter_built[large] the same for the valves and filters, cooler_built for the
// intercoolers' tiles (else the radiator's look).
inline std::string definitions_json(const bool built[kMaxSize - kMinSize + 1], const bool bov_built[2] = nullptr, const bool wg_built[2] = nullptr,
                                    const bool filter_built[2] = nullptr, const bool* cooler_built = nullptr, const bool blower_built[2] = nullptr,
                                    const bool pd_built[2] = nullptr) {
    std::string out = "{\n  \"definitions\": [\n";
    for (int n : kTurboSizes)
        for (int twin = 0; twin < 2; ++twin)
            for (int look = 0; look < kTurboLooks; ++look)
                out += definition_entry(n, built[n - kMinSize] ? mesh_path(n, look, twin) : std::string(kStockMeshPath), look, twin) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += blow_off_entry(large, bov_built && bov_built[large] ? blow_off_mesh_path(large) : std::string(kValveMeshPath)) + ",\n";
    // The wastegate part is gone (Nate, 2026-11-04): every turbo's built-in wastegate holds the boost target, and the
    // ECU has the antilag and launch two-step it used to carry. Saved wastegates drop from vehicles on load.
    (void)wg_built;
    for (int large = 0; large < 2; ++large)
        for (int look = 0; look < kFilterLooks; ++look)
            out += air_filter_entry(large, filter_built && filter_built[large] ? air_filter_mesh_path(large, look) : std::string(large ? "meshes/components/gas_port_b.mesh" : kValveMeshPath), look) + ",\n";
    for (int look = 0; look < kCoolerLooks; ++look) out += intercooler_entry(cooler_built && *cooler_built, look) + ",\n";
    for (int large = 0; large < 2; ++large)
        out += blower_entry(large, blower_built && blower_built[large] ? blower_mesh_path(large) : std::string(kStockMeshPath)) + ",\n";
    // The twin screw (without its mesh: the stock impeller look). The Roots blower is on hold (Nate, 2026-11-04): its
    // model and looks stay in the code, but it is not offered (kOfferRoots).
    out += pd_entry(kTwinScrew, pd_built && pd_built[kTwinScrew] ? pd_mesh_path(kTwinScrew) : std::string(kStockMeshPath));
    if (kOfferRoots) out += ",\n" + pd_entry(kRoots, pd_built && pd_built[kRoots] ? pd_mesh_path(kRoots) : std::string(kStockMeshPath));
    out += "\n";
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
