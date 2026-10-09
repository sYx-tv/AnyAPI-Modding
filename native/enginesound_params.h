#pragma once
// EngineSound: engine types, presets, slider packing and the sound shaping math.
// Pure C++ with no game access, so tests run on any platform.
//
// The game stores an engine's sound choice in one replicated s32 (_m_sound_index). Vanilla uses
// kVanillaMin..kVanillaMax. EngineSound adds preset indices after that, one "Custom" index, and packed
// Custom tunes: any negative value, carrying 31 bits of slider steps. The host replicates and saves that
// s32, so every player with the mod decodes the same tune and hears the same engine.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace enginesound {

inline constexpr int32_t kVanillaMin = 0, kVanillaMax = 2;   // confirmed in game: 0/1/2 -> effects 41/47/48

// e_audio_effect values (reference: Steam build 25755694, static enum values).
enum Effect : int32_t {
    kFluidGasVent = 20, kMotorA = 38, kMotorB = 39, kEngineBaseA = 40, kEngineHighA = 41, kEngineHighB = 47,
    kEngineHighC = 48, kEnginePop = 50, kEngineKnock = 51, kFluidGasSupercharger = 66,
    kTurbineCompressorLoop = 86, kTurbineCompressorLoopB = 89,
};

// ------------------------------------------------------------------ engine types
// Each type picks the two recorded loops (main and low/body layer) and their pitch ratios, plus how fast
// a cam lope pulses. These are starting points chosen by ear-guess; tune after in-game listening.
struct EngineType { const char* name; int32_t main; double main_ratio; int32_t low; double low_ratio; double lope_hz; };
inline constexpr EngineType kTypes[16] = {
    {"Stock 1",          kEngineHighA, 1.00, kEngineBaseA, 1.00, 2.6},
    {"Stock 2",          kEngineHighB, 1.00, kEngineBaseA, 1.00, 2.6},
    {"Stock 3",          kEngineHighC, 1.00, kEngineBaseA, 1.00, 2.2},
    {"Inline-4",         kEngineHighA, 0.80, kEngineBaseA, 1.10, 3.0},
    {"Inline-6",         kEngineHighA, 0.60, kEngineHighC, 1.20, 2.8},
    {"V6",               kEngineHighB, 0.55, kEngineHighC, 0.95, 2.6},
    {"V8 cross-plane",   kEngineHighC, 1.05, kEngineHighC, 0.60, 2.2},
    {"V8 flat-plane",    kEngineHighB, 0.62, kEngineHighA, 0.70, 2.8},
    {"V10",              kEngineHighB, 0.72, kEngineHighC, 0.90, 2.6},
    {"V12",              kEngineHighB, 0.85, kEngineHighA, 0.85, 3.2},
    {"Boxer-4",          kEngineHighC, 1.25, kEngineHighA, 0.65, 2.0},
    {"Diesel inline-6",  kEngineHighC, 0.80, kEngineBaseA, 0.75, 2.0},
    {"Diesel V8",        kEngineHighC, 0.68, kEngineBaseA, 0.60, 1.8},
    {"Big-rig diesel",   kEngineHighC, 0.55, kEngineBaseA, 0.50, 1.5},
    {"Rotary",           kEngineHighB, 0.95, kEngineHighA, 1.10, 4.0},
    {"Electric",         kMotorA,      1.00, kMotorB,      1.00, 0.0},
};

// ------------------------------------------------------------------ sliders
enum Slider : int { kType, kPitch, kRange, kCurve, kVolume, kLow, kLope, kInduction, kBoost, kCrackle, kLoad, kSmoothing };
inline constexpr int kSliderCount = 12;
struct SliderSpec { const char* label; int bits; int shift; int neutral; };
// 31 bits in total: a packed tune is INT32_MIN | bits, so it is always negative and never an index.
inline constexpr SliderSpec kSliders[kSliderCount] = {
    {"Engine type", 4, 0, 0},
    {"Idle pitch", 4, 4, 10},
    {"Rev range", 3, 8, 2},
    {"Rev curve", 2, 11, 1},
    {"Volume", 3, 13, 4},
    {"Body layer", 3, 16, 5},
    {"Cam lope", 3, 19, 0},
    {"Induction", 2, 22, 0},
    {"Boost noise", 2, 24, 1},
    {"Lift-off crackle", 2, 26, 0},
    {"Throttle response", 2, 28, 0},
    {"Rev smoothing", 1, 30, 0},
};
inline constexpr int32_t kPackedPayloadBits = 31;
inline constexpr const char* kInductionNames[4] = {"None", "Turbo", "Supercharger", "Twin turbo"};
inline constexpr const char* kLevelNames[4] = {"Off", "Light", "Medium", "Heavy"};

struct Params { uint8_t v[kSliderCount]; };

inline int max_step(int s) { return (1 << kSliders[s].bits) - 1; }

inline Params neutral() {
    Params p{};
    for (int s = 0; s < kSliderCount; ++s) p.v[s] = uint8_t(kSliders[s].neutral);
    return p;
}
inline int32_t pack(const Params& p) {
    uint32_t bits = 0;
    for (int s = 0; s < kSliderCount; ++s)
        bits |= uint32_t(std::clamp<int>(p.v[s], 0, max_step(s))) << kSliders[s].shift;
    return int32_t(0x80000000u | bits);
}
inline bool is_packed(int32_t value) { return value < 0; }
inline Params unpack(int32_t value) {
    Params p{};
    uint32_t bits = uint32_t(value) & 0x7fffffffu;
    for (int s = 0; s < kSliderCount; ++s) p.v[s] = uint8_t((bits >> kSliders[s].shift) & uint32_t(max_step(s)));
    return p;
}

// ------------------------------------------------------------------ presets
// Starting points; the Engine sound panel's "Customize" button copies one into Custom for tweaking.
struct Preset { const char* name; Params params; };
inline constexpr Preset kPresets[] = {
    //                     type pitch range curve vol low lope ind boost crackle load smooth
    {"Muscle V8",        {{6,   9,    4,    1,    5,  6,  2,   0,  0,    1,      2,   1}}},
    {"Smooth inline",    {{4,   10,   3,    1,    4,  4,  0,   0,  0,    0,      1,   1}}},
    {"Tamed sport",      {{7,   10,   2,    1,    3,  4,  0,   0,  0,    0,      1,   1}}},
    {"Big diesel",       {{13,  10,   1,    2,    5,  6,  1,   1,  1,    0,      2,   1}}},
    {"Built 2JZ",        {{4,   11,   6,    1,    5,  3,  1,   1,  3,    2,      3,   0}}},
    {"Powerstroke",      {{12,  9,    2,    2,    5,  6,  2,   1,  3,    0,      3,   1}}},
    {"Hemi",             {{6,   8,    4,    1,    6,  6,  5,   2,  2,    1,      2,   0}}},
};
inline constexpr int32_t kPresetCount = int32_t(sizeof(kPresets) / sizeof(kPresets[0]));
inline constexpr int32_t kFirstPreset = kVanillaMax + 1;
inline constexpr int32_t kCustomIndex = kFirstPreset + kPresetCount;

enum class Mode { Vanilla, Preset, Custom };
struct Choice { Mode mode; Params params; int preset; };

// What a replicated sound index means to EngineSound. Unknown values fall back to vanilla.
inline Choice decode(int32_t value) {
    if (is_packed(value)) return {Mode::Custom, unpack(value), -1};
    if (value >= kFirstPreset && value < kCustomIndex) return {Mode::Preset, kPresets[value - kFirstPreset].params, int(value - kFirstPreset)};
    if (value == kCustomIndex) return {Mode::Custom, neutral(), -1};
    return {Mode::Vanilla, neutral(), -1};
}

// ------------------------------------------------------------------ physical values per slider step
inline const EngineType& engine_type(const Params& p) { return kTypes[p.v[kType] & 15]; }
inline double idle_pitch(const Params& p) { return 0.40 + 0.06 * p.v[kPitch]; }           // 0.40..1.30, neutral 1.0
inline double rev_range(const Params& p) { return 0.60 + 0.20 * p.v[kRange]; }            // 0.6..2.0, neutral 1.0
inline double rev_curve(const Params& p) { constexpr double c[4] = {0.6, 1.0, 1.5, 2.2}; return c[p.v[kCurve] & 3]; }
inline double volume(const Params& p) { return 0.25 * p.v[kVolume]; }                     // 0..1.75, neutral 1.0
inline double body_layer(const Params& p) { return 0.20 * p.v[kLow]; }                     // 0..1.4, neutral 1.0
inline double lope_depth(const Params& p) { return p.v[kLope] / 7.0; }                     // 0..1
inline int induction(const Params& p) { return p.v[kInduction] & 3; }
inline double boost_volume(const Params& p) { constexpr double b[4] = {0.25, 0.5, 0.8, 1.1}; return b[p.v[kBoost] & 3]; }
inline double crackle_rate(const Params& p) { constexpr double r[4] = {0, 3, 7, 14}; return r[p.v[kCrackle] & 3]; }  // pops/s at full revs
inline double load_response(const Params& p) { constexpr double l[4] = {0, 0.3, 0.6, 1.0}; return l[p.v[kLoad] & 3]; }
inline double smoothing_seconds(const Params& p) { return p.v[kSmoothing] ? 0.2 : 0.0; }
inline int32_t main_effect(const Params& p) { return engine_type(p).main; }
inline int32_t low_effect(const Params& p) { return engine_type(p).low; }

inline double clamp01(double x) { return std::isfinite(x) ? std::clamp(x, 0.0, 1.0) : 0.0; }

// Multiplier applied to the game's own pitch for the main voice. rpm and load are the engine's replicated
// speed and power factors (0..1). At neutral settings this returns 1, so a neutral Custom sounds stock.
inline double pitch_multiplier(const Params& p, double rpm, double load = 0) {
    rpm = clamp01(rpm); load = clamp01(load);
    double shaped = std::pow(rpm, rev_curve(p));
    return engine_type(p).main_ratio * idle_pitch(p) * (1.0 + (rev_range(p) - 1.0) * shaped) * (1.0 + 0.04 * load_response(p) * load);
}
// Body layer follows the same rev shape at half strength so it stays underneath the main voice.
inline double body_pitch_multiplier(const Params& p, double rpm) {
    double shaped = std::pow(clamp01(rpm), rev_curve(p));
    return engine_type(p).low_ratio * idle_pitch(p) * (1.0 + 0.5 * (rev_range(p) - 1.0) * shaped);
}
// Throttle response: quieter off throttle, louder under load. Neutral (0) returns 1.
inline double load_gain(const Params& p, double load) {
    double r = load_response(p);
    return 1.0 - 0.5 * r + r * clamp01(load);
}

// Cam lope: an irregular volume and pitch pulse at idle that fades out as revs rise.
struct Lope { double volume; double pitch; };
inline Lope lope(const Params& p, double rpm, double t) {
    double depth = lope_depth(p) * std::max(0.0, 1.0 - clamp01(rpm) * 2.5);
    double hz = engine_type(p).lope_hz;
    if (depth <= 0 || hz <= 0 || !std::isfinite(t)) return {1, 1};
    constexpr double tau = 6.283185307179586;
    double a = std::sin(tau * hz * t), b = std::sin(tau * hz * 1.61 * t + 1.3);
    double pulse = 0.5 + 0.5 * a * b;                       // 0..1, uneven beat
    return {1.0 - 0.6 * depth * pulse, 1.0 + 0.05 * depth * a};
}

// Turbo spool target from revs and load; the caller smooths it (lag) per engine.
inline double spool_target(double rpm, double load) { return clamp01(rpm) * (0.25 + 0.75 * clamp01(load)); }
inline double spool_seconds(int induction_kind) { return induction_kind == 3 ? 0.45 : 0.9; }

// Induction layer (played on the engine's spare knock voice). Returns false for "None".
struct Layer { int32_t effect; double volume; double speed; };
inline bool induction_layer(const Params& p, double rpm, double load, double spool, Layer& out) {
    rpm = clamp01(rpm); load = clamp01(load); spool = clamp01(spool);
    switch (induction(p)) {
    case 1: out = {kTurbineCompressorLoop, boost_volume(p) * std::pow(spool, 1.5), 0.6 + 1.6 * spool}; return true;
    case 2: out = {kFluidGasSupercharger, boost_volume(p) * rpm * (0.3 + 0.7 * load), 0.5 + 1.5 * rpm}; return true;
    case 3: out = {kTurbineCompressorLoopB, boost_volume(p) * std::pow(spool, 1.3), 0.75 + 1.9 * spool}; return true;
    default: return false;
    }
}

// One-pole smoothing toward target over tau seconds.
inline double smooth(double current, double target, double tau, double dt) {
    if (!std::isfinite(current) || tau <= 0 || !std::isfinite(dt) || dt <= 0) return target;
    double a = 1.0 - std::exp(-std::min(dt, 0.25) / tau);
    return current + (target - current) * a;
}

// Game audio speed is clamped to a sane band so a bad value can't produce silence or a squeal.
inline double clamp_speed(double speed) { return std::isfinite(speed) ? std::clamp(speed, 0.05, 4.0) : 1.0; }
inline double clamp_volume(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 2.0) : 0.0; }

} // namespace enginesound
