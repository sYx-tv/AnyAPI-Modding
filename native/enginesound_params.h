#pragma once
// EngineSound: preset table, slider packing and the pitch/volume curve.
// Pure C++ with no game access, so tests run on any platform.
//
// The game stores an engine's sound choice in one replicated s32 (_m_sound_index). Vanilla uses
// kVanillaMin..kVanillaMax. EngineSound adds preset indices after that, one "Custom" index, and
// packed custom values (kPackedFlag | slider bits). Because the host replicates and saves that s32,
// every player with the mod decodes the same value and hears the same engine.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace enginesound {

// Assumed vanilla range. The host logs the real one; if it differs, the mod leaves the row alone.
inline constexpr int32_t kVanillaMin = 0, kVanillaMax = 2;

// e_audio_effect values (reference: Steam build 25755694, static enum values).
enum Effect : int32_t {
    kMotorA = 38, kMotorB = 39, kEngineBaseA = 40, kEngineHighA = 41, kEngineHighB = 47,
    kEngineHighC = 48, kEnginePop = 50, kPropMarineA = 79, kPropAircraftA = 82,
};

enum Slider : int { kBase, kPitch, kRange, kCurve, kVolume, kIdleLayer, kSmoothing, kPops };
inline constexpr int kSliderCount = 8;

struct SliderSpec { const char* label; int bits; int shift; int neutral; };
// 25 bits in total; bit 30 marks a packed value so it can never collide with an index.
inline constexpr SliderSpec kSliders[kSliderCount] = {
    {"Base sound", 3, 0, 0},
    {"Idle pitch", 4, 3, 10},
    {"Rev range", 4, 7, 5},
    {"Rev curve", 3, 11, 2},
    {"Volume", 4, 14, 10},
    {"Idle layer", 3, 18, 5},
    {"Smoothing", 3, 21, 0},
    {"Backfire pops", 1, 24, 1},
};
inline constexpr int32_t kPackedFlag = 0x40000000;
inline constexpr int32_t kPackedMask = (1 << 25) - 1;

inline constexpr int32_t kBaseEffects[8] = {
    kEngineHighA, kEngineHighB, kEngineHighC, kEngineBaseA, kMotorA, kMotorB, kPropMarineA, kPropAircraftA};
inline constexpr const char* kBaseNames[8] = {
    "Stock 1 sample", "Stock 2 sample", "Stock 3 sample", "Idle sample", "Electric A", "Electric B", "Marine", "Aircraft"};

struct Params { uint8_t v[kSliderCount]; };

inline int max_step(int s) { return (1 << kSliders[s].bits) - 1; }

inline Params neutral() {
    Params p{};
    for (int s = 0; s < kSliderCount; ++s) p.v[s] = uint8_t(kSliders[s].neutral);
    return p;
}
inline int32_t pack(const Params& p) {
    int32_t bits = 0;
    for (int s = 0; s < kSliderCount; ++s)
        bits |= int32_t(std::clamp<int>(p.v[s], 0, max_step(s))) << kSliders[s].shift;
    return kPackedFlag | bits;
}
inline bool is_packed(int32_t value) { return (value & ~kPackedMask) == kPackedFlag; }
inline Params unpack(int32_t value) {
    Params p{};
    for (int s = 0; s < kSliderCount; ++s) p.v[s] = uint8_t((value >> kSliders[s].shift) & max_step(s));
    return p;
}

// Starting points to tune by ear. Each is stored as slider steps so it shares one code path with Custom.
struct Preset { const char* name; Params params; };
// Deliberately far from stock so each is easy to tell apart; tune by ear after testing.
// Pitch steps: 0.40 + 0.06*v. Range: 0.5 + 0.1*v. Curve: 0.5 + 0.25*v. Volume: 0.1*v. Idle layer: 0.2*v.
inline constexpr Preset kPresets[] = {
    //                     base pitch range curve vol idle smooth pops
    {"Muscle V8",        {{2,   8,   11,   3,    12,  6,   2,     1}}},  // stock 3 loop, higher idle, wide rev climb
    {"Smooth inline",    {{0,   2,   8,    2,    9,   3,   3,     0}}},  // stock 1 loop pitched far down, softer idle
    {"Tamed sport",      {{1,   3,   3,    1,    6,   4,   2,     0}}},  // stock 2 loop pitched down, narrow range, quieter
    {"Big diesel",       {{2,   1,   4,    4,    13,  7,   5,     1}}},  // stock 3 loop very low and slow to rev
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

// Physical values for each slider step.
inline double idle_pitch(const Params& p) { return 0.40 + 0.06 * p.v[kPitch]; }     // 0.40..1.30, neutral 1.0
inline double rev_range(const Params& p) { return 0.50 + 0.10 * p.v[kRange]; }      // 0.50..2.00, neutral 1.0
inline double rev_curve(const Params& p) { return 0.50 + 0.25 * p.v[kCurve]; }      // 0.50..2.25, neutral 1.0
inline double volume(const Params& p) { return 0.10 * p.v[kVolume]; }               // 0.00..1.50, neutral 1.0
inline double idle_layer(const Params& p) { return 0.20 * p.v[kIdleLayer]; }        // 0.00..1.40, neutral 1.0
inline double smoothing_seconds(const Params& p) { return 0.05 * p.v[kSmoothing]; } // 0.00..0.35
inline bool pops(const Params& p) { return p.v[kPops] != 0; }
inline int32_t base_effect(const Params& p) { return kBaseEffects[p.v[kBase] & 7]; }

// Multiplier applied to the game's own pitch for the main engine voice. rpm is the engine's
// replicated speed factor (0..1). At neutral settings this returns 1, so Custom starts as stock.
inline double pitch_multiplier(const Params& p, double rpm) {
    rpm = std::isfinite(rpm) ? std::clamp(rpm, 0.0, 1.0) : 0.0;
    double shaped = std::pow(rpm, rev_curve(p));
    return idle_pitch(p) * (1.0 + (rev_range(p) - 1.0) * shaped);
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
