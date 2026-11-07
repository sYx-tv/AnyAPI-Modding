#pragma once
// Rotary sound choices, in line with EngineSound (Nate 2026-11-05): the engine's replicated sound index means
//   0..2   the three ports (bridge, street, peripheral), as before: a save without the mod still plays one
//   3..7   five sounds made for the rotor count (1, 2, 3 or 4 rotors)
//   8 / any negative value   Custom, its sliders packed like EngineSound's (enginesound_params.h pack())
// EngineSound's panel shows them and its host and save hooks carry them; rotary_mod.cpp plays them. Pure C++.
#include "enginesound_params.h"
#include "rotary_sound.h"
#include <algorithm>
#include <cwchar>

namespace rotary_tune {

using enginesound::Params;
using enginesound::Mode;
using enginesound::Choice;
namespace es = enginesound;

inline constexpr int kPresets = es::synth::kPresetsPerLayout;   // 5, the same slots as EngineSound's
static_assert(kPresets == 5, "rotary presets fill EngineSound's five slots");

inline int clamp_rotors(int r) { return r < 1 ? 1 : r > 4 ? 4 : r; }

inline const char* preset_name(int rotors, int k) {
    static const char* names[4][kPresets] = {
        {"Kart buzz", "Single brap", "Race single", "Quiet single", "Popping single"},
        {"Street 2-rotor", "Bridge 2-rotor brap", "Peripheral 2-rotor", "Turbo 2-rotor", "Drift 2-rotor"},
        {"Street 3-rotor", "Bridge 3-rotor", "Peripheral 3-rotor", "Twin-turbo 3-rotor", "Drift 3-rotor"},
        {"Street 4-rotor", "Bridge 4-rotor", "Endurance 4-rotor", "Turbo 4-rotor", "Drift 4-rotor"},
    };
    return names[clamp_rotors(rotors) - 1][std::clamp(k, 0, kPresets - 1)];
}

// The rotary's sliders, on EngineSound's packing (same bit widths), with their own meaning. Diesel is unused.
inline constexpr int kSliderOrder[] = {es::kFiring, es::kNote, es::kBark, es::kRasp, es::kMuffler, es::kCam,
                                       es::kCrackle, es::kIntake, es::kMech, es::kVolume};
inline constexpr int kSliderCount = int(sizeof(kSliderOrder) / sizeof(kSliderOrder[0]));
inline const char* slider_label(int s) {
    switch (s) {
    case es::kFiring: return "Port";
    case es::kNote: return "Exhaust note";
    case es::kBark: return "Bark";
    case es::kRasp: return "Rasp";
    case es::kMuffler: return "Muffler";
    case es::kCam: return "Idle chop";
    case es::kCrackle: return "Pops and crackle";
    case es::kIntake: return "Idle blat";
    case es::kMech: return "Shaft whirr";
    case es::kVolume: return "Volume";
    }
    return "";
}
inline const char* port_label(int step) {
    static const char* p[4] = {"Street port", "Bridge port", "Peripheral port", "Race peripheral"};
    return p[step & 3];
}

// note, bark, rasp, muffler, chop, pops, blat, whirr, port, (diesel), volume
inline Params make(int note, int bark, int rasp, int muffler, int chop, int pops, int blat, int whirr, int port, int volume) {
    Params p{};
    p.v[es::kNote] = uint8_t(std::clamp(note, 0, es::max_step(es::kNote)));
    p.v[es::kBark] = uint8_t(bark); p.v[es::kRasp] = uint8_t(rasp); p.v[es::kMuffler] = uint8_t(muffler);
    p.v[es::kCam] = uint8_t(chop); p.v[es::kCrackle] = uint8_t(pops); p.v[es::kIntake] = uint8_t(blat);
    p.v[es::kMech] = uint8_t(whirr); p.v[es::kFiring] = uint8_t(port); p.v[es::kDiesel] = 0; p.v[es::kVolume] = uint8_t(volume);
    return p;
}
// A smaller engine sings higher, a bigger one deeper.
inline Params preset(int rotors, int k) {
    const int r = clamp_rotors(rotors), shift = r == 1 ? 2 : r == 2 ? 0 : r == 3 ? -1 : -2;
    switch (std::clamp(k, 0, kPresets - 1)) {
    case 0: return make(8 + shift, 2, 2, 3, 1, 2, 1, 1, 0, 3);    // street: smooth, a little lope
    case 1: return make(4 + shift, 1, 3, 1, 5, 4, 2, 1, 1, 3);    // bridge port: the lumpy brap
    case 2: return make(2 + shift, 1, 6, 0, 7, 4, 3, 2, 2, 4);    // peripheral: open, raw, rough idle
    case 3: return make(5 + shift, 2, 1, 6, 2, 1, 1, 2, 0, 3);    // turbo: muffled and smooth, few pops
    default: return make(5 + shift, 3, 4, 2, 3, 7, 2, 1, 1, 4);   // drift: pops and bangs
    }
}
// The port a stock index means, and the preset closest to it (for "Customize this sound").
inline int preset_of_port(int32_t index) { return index == rotary::kStreetPort ? 0 : index == rotary::kPeripheralPort ? 2 : 1; }

inline Choice decode(int32_t value, int rotors) {
    if (es::is_packed(value)) return {Mode::Custom, -1, es::unpack(value)};
    if (value >= es::kFirstPreset && value < es::kCustomIndex) return {Mode::Preset, int(value - es::kFirstPreset), preset(rotors, int(value - es::kFirstPreset))};
    if (value >= es::kCustomIndex) return {Mode::Custom, -1, preset(rotors, 0)};
    return {Mode::Vanilla, -1, preset(rotors, preset_of_port(value))};
}
inline const char* choice_name(int32_t value, int rotors) {
    Choice c = decode(value, rotors);
    if (c.mode == Mode::Preset) return preset_name(rotors, c.preset);
    if (c.mode == Mode::Custom) return "Custom";
    return value == rotary::kStreetPort ? "Street port" : value == rotary::kPeripheralPort ? "Peripheral port" : "Bridge port";
}

// The sound the sliders make: the port's character with the sliders' pitch, chop, pops and level.
inline rotary::Character character_of(const Params& p) {
    auto f = [&](int s) { return double(p.v[s]) / es::max_step(s); };
    const int port = p.v[es::kFiring] & 3;
    rotary::Character c = rotary::character(port == 0 ? rotary::kStreetPort : port == 1 ? rotary::kBridgePort : rotary::kPeripheralPort);
    if (port == 3) { c.rasp_hz *= 1.25; c.pipe_q *= 1.15; }   // race peripheral: a harder edge
    c.pipe_hz = es::note_hz(p.v[es::kNote]);
    c.body_hz = es::bark_hz(p.v[es::kBark]);
    c.rasp_gain = 0.15 + 1.05 * f(es::kRasp);
    const double chop = f(es::kCam);
    c.lope = 0.08 + 0.8 * chop; c.jitter = 0.05 + 0.4 * chop; c.misfire = 0.13 * chop; c.idle_span = 6 + 26 * chop;
    c.crackle = 2.0 + 30.0 * f(es::kCrackle);
    c.blat = 0.4 + 0.3 * p.v[es::kIntake];
    c.whirr = 0.02 + 0.04 * p.v[es::kMech];
    c.gain *= es::volume_gain(p.v[es::kVolume]);
    return c;
}
inline double muffler_of(const Params& p) { return 0.85 * double(p.v[es::kMuffler]) / es::max_step(es::kMuffler); }
inline bool pops_off(const Params& p) { return p.v[es::kCrackle] == 0; }

inline void value_text(int s, const Params& p, wchar_t* buf, size_t n) {
    const int v = p.v[s];
    static const wchar_t* four[4] = {L"Off", L"Light", L"Medium", L"Heavy"};
    switch (s) {
    case es::kFiring: std::swprintf(buf, n, L"%hs", port_label(v)); break;
    case es::kNote: std::swprintf(buf, n, L"%d Hz %ls", int(es::note_hz(v)), v < 5 ? L"deep" : v < 10 ? L"" : L"high"); break;
    case es::kBark: std::swprintf(buf, n, L"%d Hz", int(es::bark_hz(v))); break;
    case es::kMuffler: { static const wchar_t* m[8] = {L"Straight pipe", L"Open", L"Race", L"Loud", L"Sport", L"Mild", L"Stock", L"Quiet"}; std::swprintf(buf, n, L"%ls", m[v & 7]); break; }
    case es::kIntake: case es::kMech: std::swprintf(buf, n, L"%ls", four[v & 3]); break;
    case es::kVolume: std::swprintf(buf, n, L"%d%%", int(std::lround(es::volume_gain(v) * 100))); break;
    default: std::swprintf(buf, n, v ? L"%d / 7" : L"Off", v); break;
    }
}

}  // namespace rotary_tune
