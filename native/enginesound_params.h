#pragma once
// EngineSound: which engine a part is, its cylinder count, the sound choice encoding and the Custom sliders.
// Pure C++ with no game access, so tests run on any platform.
//
// The game stores an engine's sound choice in one replicated s32 (_m_sound_index). Vanilla uses
// kVanillaMin..kVanillaMax (the game's own recordings). EngineSound adds the five generated sounds of the engine's
// layout after that, one "Custom" index, and packed Custom tunes: any negative value, carrying the Custom sliders.
// The host replicates and saves that s32, so every player with the mod decodes the same tune and hears the same
// engine. Which five sounds an index means depends on the engine's layout (inline, V or boxer).
//
// The game's engines (Anymaker 0.1.25, rom/data/vehicle_component_definitions.json), each stretched along z:
//   inline  engine / engine_block_b / engine_block_c                small / medium / large
//   V       v_engine / v_engine_b / v_engine_c
//   boxer   opposed_engine / opposed_engine_b / opposed_engine_c
// Stretch steps are 2, 3 and 5 cells for small, medium and large. Unstretched, an inline is a single, a V a V-twin
// and a boxer a flat-twin; each step adds one cylinder to an inline and a pair to a V or boxer.
// "rotary_engine" (AnyEngineParts) makes its own sound and is left alone.
#include "enginesound_synth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace enginesound {

using synth::Layout;
using synth::Tune;

inline constexpr int32_t kVanillaMin = 0, kVanillaMax = 2;   // confirmed in game: 0/1/2 -> effects 41/47/48
inline constexpr int32_t kFirstPreset = kVanillaMax + 1;
inline constexpr int32_t kCustomIndex = kFirstPreset + synth::kPresetsPerLayout;

// ------------------------------------------------------------------ engines
enum Size : int { kSmall, kMedium, kLarge };
struct EngineKind { Layout layout; Size size; bool rotary; bool known; };

inline bool id_is(const char* id, size_t n, const char* want) {
    size_t w = std::strlen(want);
    return id && n == w && std::memcmp(id, want, n) == 0;
}
inline EngineKind engine_kind(const char* id, size_t n) {
    struct Row { const char* id; Layout layout; Size size; };
    static const Row rows[] = {
        {"engine", synth::kInline, kSmall}, {"engine_block_b", synth::kInline, kMedium}, {"engine_block_c", synth::kInline, kLarge},
        {"v_engine", synth::kVee, kSmall}, {"v_engine_b", synth::kVee, kMedium}, {"v_engine_c", synth::kVee, kLarge},
        {"opposed_engine", synth::kBoxer, kSmall}, {"opposed_engine_b", synth::kBoxer, kMedium}, {"opposed_engine_c", synth::kBoxer, kLarge},
    };
    if (id_is(id, n, "rotary_engine")) return {synth::kInline, kSmall, true, true};
    for (const Row& r : rows) if (id_is(id, n, r.id)) return {r.layout, r.size, false, true};
    return {synth::kInline, kMedium, false, false};
}
inline int stretch_interval(Size s) { return s == kSmall ? 2 : s == kMedium ? 3 : 5; }
inline int cylinders(const EngineKind& k, int32_t extend_z) {
    int32_t cells = extend_z < 0 ? -extend_z : extend_z;
    if (cells > 4096) cells = 0;   // not a real stretch
    int steps = int(cells / stretch_interval(k.size));
    int n = k.layout == synth::kInline ? 1 + steps : 2 + 2 * steps;
    return synth::clamp_cylinders(n);
}
// Bigger blocks breathe through bigger pipes: deeper boom and bark.
inline Tune sized(Tune t, Size s) {
    const double pipe[3] = {1.12, 1.0, 0.86}, body[3] = {1.08, 1.0, 0.9};
    t.pipe_hz *= pipe[s]; t.body_hz *= body[s];
    return t;
}
inline const char* size_name(Size s) { return s == kSmall ? "small" : s == kMedium ? "medium" : "large"; }

// The engine's name as players know it: "Inline-4", "V8", "Flat-6", "V-twin", "Single".
inline void engine_name(Layout l, int cylinders, char* buf, size_t n) {
    if (l == synth::kVee) {
        if (cylinders == 2) std::snprintf(buf, n, "V-twin");
        else std::snprintf(buf, n, "V%d", cylinders);
    } else if (l == synth::kBoxer) {
        if (cylinders == 2) std::snprintf(buf, n, "Flat-twin");
        else std::snprintf(buf, n, "Flat-%d", cylinders);
    } else {
        if (cylinders == 1) std::snprintf(buf, n, "Single");
        else std::snprintf(buf, n, "Inline-%d", cylinders);
    }
}

// ------------------------------------------------------------------ the host's row tag
// The host widens the engine's sound row to INT32_MIN..kServerMax - tag. The tag tells clients the row is modded
// and which engine it belongs to (layout, size, cylinders), so the panel can name that layout's sounds.
inline constexpr int32_t kServerMin = INT32_MIN;
inline constexpr int32_t kServerMax = INT32_MAX;
inline constexpr int32_t kTagSpan = 1024;
inline int32_t row_tag(const EngineKind& k, int cylinders) {
    return 1 + int32_t(k.layout) * 3 + int32_t(k.size) + 16 * synth::clamp_cylinders(cylinders);
}
inline bool is_modded_max(int32_t max) { return max > kServerMax - kTagSpan; }
// Tag 0 (a host without the tag) reads as an unknown inline-4.
inline void decode_tag(int32_t tag, EngineKind& k, int& cylinders) {
    k = {synth::kInline, kMedium, false, false};
    cylinders = 4;
    if (tag <= 0 || tag >= kTagSpan) return;
    int kind = (tag - 1) % 16, cyl = (tag - 1) / 16;
    if (kind >= 9 || cyl < 1) return;
    k = {Layout(kind / 3), Size(kind % 3), false, true};
    cylinders = synth::clamp_cylinders(cyl);
}

// ------------------------------------------------------------------ Custom sliders
enum Slider : int { kNote, kBark, kRasp, kMuffler, kCam, kCrackle, kIntake, kMech, kFiring, kDiesel, kVolume };
inline constexpr int kSliderCount = 11;
struct SliderSpec { const char* label; int bits; int shift; };
// A packed tune is INT32_MIN | bits, so it is always negative and never an index.
inline constexpr SliderSpec kSliders[kSliderCount] = {
    {"Exhaust note", 4, 0},
    {"Bark", 3, 4},
    {"Rasp", 3, 7},
    {"Muffler", 3, 10},
    {"Cam lope", 3, 13},
    {"Pops and crackle", 3, 16},
    {"Intake roar", 2, 19},
    {"Valvetrain", 2, 21},
    {"Firing / headers", 2, 23},
    {"Diesel", 1, 25},
    {"Volume", 3, 26},
};
struct Params { uint8_t v[kSliderCount]; };

inline int max_step(int s) { return (1 << kSliders[s].bits) - 1; }
inline int32_t pack(const Params& p) {
    uint32_t bits = 0;
    for (int s = 0; s < kSliderCount; ++s) bits |= uint32_t(std::clamp<int>(p.v[s], 0, max_step(s))) << kSliders[s].shift;
    return int32_t(0x80000000u | bits);
}
inline bool is_packed(int32_t value) { return value < 0; }
inline Params unpack(int32_t value) {
    Params p{};
    uint32_t bits = uint32_t(value) & 0x7fffffffu;
    for (int s = 0; s < kSliderCount; ++s) p.v[s] = uint8_t((bits >> kSliders[s].shift) & uint32_t(max_step(s)));
    return p;
}

// Slider steps <-> the synth's tune.
inline double note_hz(int step) { return 50.0 + 8.0 * step; }      // 50..170 Hz
inline double bark_hz(int step) { return 260.0 + 60.0 * step; }    // 260..680 Hz
inline double volume_gain(int step) { return 0.25 * (step + 1); }  // 0.25..2.0, 1.0 at step 3
inline Tune to_tune(const Params& p) {
    auto f = [&](int s) { return double(p.v[s]) / max_step(s); };
    return {note_hz(p.v[kNote]), bark_hz(p.v[kBark]), f(kRasp), f(kMuffler), f(kCam), f(kCrackle), f(kIntake), f(kMech),
            double(p.v[kDiesel] & 1), f(kFiring), volume_gain(p.v[kVolume])};
}
inline Params from_tune(const Tune& t) {
    auto step = [](double x, int s) { return uint8_t(std::clamp<long>(std::lround(x), 0, max_step(s))); };
    auto unit = [&](double x, int s) { return step((std::isfinite(x) ? x : 0) * max_step(s), s); };
    Params p{};
    p.v[kNote] = step((t.pipe_hz - 50.0) / 8.0, kNote);
    p.v[kBark] = step((t.body_hz - 260.0) / 60.0, kBark);
    p.v[kRasp] = unit(t.rasp, kRasp); p.v[kMuffler] = unit(t.muffler, kMuffler); p.v[kCam] = unit(t.cam, kCam);
    p.v[kCrackle] = unit(t.crackle, kCrackle); p.v[kIntake] = unit(t.intake, kIntake); p.v[kMech] = unit(t.mech, kMech);
    p.v[kFiring] = unit(t.uneven, kFiring); p.v[kDiesel] = t.diesel > 0.5 ? 1 : 0;
    p.v[kVolume] = step(t.gain / 0.25 - 1.0, kVolume);
    return p;
}

// What the Firing / headers slider means on this engine.
inline const char* firing_name(Layout l, int cylinders, int step) {
    if (l == synth::kVee && cylinders == 8) return step >= 2 ? "Flat-plane" : "Cross-plane";
    if (l == synth::kVee && cylinders == 2) { static const char* v[4] = {"45 degree", "60 degree", "75 degree", "90 degree"}; return v[step & 3]; }
    if (l == synth::kBoxer && cylinders >= 4) { static const char* b[4] = {"Equal headers", "Mild rumble", "Rumble", "Full rumble"}; return b[step & 3]; }
    if (l == synth::kInline && cylinders >= 4) { static const char* i[4] = {"Tuned headers", "Mild", "Uneven", "Raspy merge"}; return i[step & 3]; }
    return "(no effect)";
}

// ------------------------------------------------------------------ the sound choice
enum class Mode { Vanilla, Preset, Custom };
struct Choice { Mode mode; int preset; Params params; };

// What a replicated sound index means to EngineSound on an engine of this layout. Indices past Custom (older
// EngineSound saves) open Custom.
inline Choice decode(int32_t value, Layout l) {
    l = l >= synth::kInline && l < synth::kLayoutCount ? l : synth::kInline;
    if (is_packed(value)) return {Mode::Custom, -1, unpack(value)};
    if (value >= kFirstPreset && value < kCustomIndex)
        return {Mode::Preset, int(value - kFirstPreset), from_tune(synth::kPresets[l][value - kFirstPreset].tune)};
    if (value >= kCustomIndex) return {Mode::Custom, -1, from_tune(synth::kPresets[l][0].tune)};
    return {Mode::Vanilla, -1, from_tune(synth::kPresets[l][0].tune)};
}
// The tune a choice plays (presets exactly, Custom from its sliders), sized to the engine.
inline Tune tune_of(const Choice& c, const EngineKind& k) {
    Layout l = k.layout >= synth::kInline && k.layout < synth::kLayoutCount ? k.layout : synth::kInline;
    Tune t = c.mode == Mode::Preset ? synth::kPresets[l][c.preset].tune : to_tune(c.params);
    return sized(t, k.size);
}
inline const char* choice_name(int32_t value, Layout l) {
    Choice c = decode(value, l);
    if (c.mode == Mode::Preset) return synth::kPresets[l][c.preset].name;
    if (c.mode == Mode::Custom) return "Custom";
    return value == 1 ? "Game sound 2" : value == 2 ? "Game sound 3" : "Game sound 1";
}

}  // namespace enginesound
