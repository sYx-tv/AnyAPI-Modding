#pragma once
// AnyVehicleOverhaul ECU: one small part that tunes every engine on its vehicle (Nate, 2026-11-04: "all tuning lives
// on the ECU, and everything must matter"). Pure logic: no game access, tested in tests/ecu_logic_test.cpp.
//
// Two ways to tune it, and both work at once:
//   - the ECU panel (its key, F9 by default, near an ECU): Up/Down picks a row, Left/Right changes it. Saved with
//     the world, per ECU.
//   - its data inputs, from a microcontroller, a dial or a switch: a wired input that is not 0 wins over the panel.
//
// What each setting does (all on the game's own numbers, see turbo_mod.cpp hk_engine_tick / hk_remap):
//   rev_limit     fuel cut at this RPM (0: the engine's own limit). Bounces on the limiter like a real cut.
//   launch_rpm    two-step: while launch is on, the engine holds at this RPM (antilag on makes boost on the line).
//   launch        above 0.5: the two-step is armed (wire it to a clutch or brake switch, or gear 0).
//   boost_psi     boost target for every turbo and supercharger on the vehicle (0: their springs, 12.8 psi).
//   timing        ignition advance, -10..+10 degrees from stock. Each degree: +1.2% power and the engine knocks
//                 2% sooner (its knock limit drops). Retard is the safe tune for big boost.
//   fuel_trim     -15..+15 % fuel. Lean (minus) up to -8 %: +0.5 % power a step but knocks sooner and runs hotter;
//                 past -8 % it loses power. Rich (plus): a little less power, knocks later, burns more fuel, pops more.
//   pops          0..1: burble tune, pops and bangs on the overrun (lift off at speed).
//   antilag       above 0.5: every wastegate on the vehicle runs antilag (the turbos stay spooled off throttle).
// Outputs: rpm, boost_psi, knock (0 none, 1 = 100 % past the limit), power_scale and knock_limit (boost factor).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ecu {

constexpr const char* kDefinitionId = "engine_ecu";
constexpr int kPanelKeyDefault = 0x78;              // VK_F9

struct Tune {
    double rev_limit = 0;     // RPM, 0 off
    double launch_rpm = 0;    // RPM, 0 off
    bool launch = false;
    double boost_psi = 0;     // 0: springs
    double timing = 0;        // degrees
    double fuel_trim = 0;     // percent
    double pops = 0;          // 0-1
    bool antilag = false;
};

// ------------------------------------------------------------------------------------- the panel's rows
struct Row { const char* label; double min, max, step; const char* unit; };
enum RowId { kRevLimit, kLaunchRpm, kLaunch, kBoost, kTiming, kFuel, kPops, kAntilag, kRowCount };
inline constexpr Row kRows[kRowCount] = {
    {"Rev limit", 0, 12000, 250, "rpm"},
    {"Launch RPM", 0, 9000, 250, "rpm"},
    {"Launch (two-step)", 0, 1, 1, ""},
    {"Boost target", 0, 60, 1, "psi"},
    {"Ignition timing", -10, 10, 1, "deg"},
    {"Fuel trim", -15, 15, 1, "%"},
    {"Pops and bangs", 0, 1, 0.25, ""},
    {"Antilag", 0, 1, 1, ""},
};
inline double get(const Tune& t, int row) {
    switch (row) {
        case kRevLimit: return t.rev_limit; case kLaunchRpm: return t.launch_rpm; case kLaunch: return t.launch ? 1 : 0;
        case kBoost: return t.boost_psi; case kTiming: return t.timing; case kFuel: return t.fuel_trim;
        case kPops: return t.pops; case kAntilag: return t.antilag ? 1 : 0;
    }
    return 0;
}
inline void set(Tune& t, int row, double v) {
    if (row < 0 || row >= kRowCount || !std::isfinite(v)) return;
    v = std::clamp(v, kRows[row].min, kRows[row].max);
    switch (row) {
        case kRevLimit: t.rev_limit = v; break; case kLaunchRpm: t.launch_rpm = v; break; case kLaunch: t.launch = v > 0.5; break;
        case kBoost: t.boost_psi = v; break; case kTiming: t.timing = v; break; case kFuel: t.fuel_trim = v; break;
        case kPops: t.pops = v; break; case kAntilag: t.antilag = v > 0.5; break;
    }
}
inline void nudge(Tune& t, int row, int steps) {
    if (row < 0 || row >= kRowCount) return;
    set(t, row, get(t, row) + steps * kRows[row].step);
}
inline std::string row_text(const Tune& t, int row) {
    char b[48];
    double v = get(t, row);
    if (row == kLaunch || row == kAntilag) return v > 0.5 ? "On" : "Off";
    if ((row == kRevLimit || row == kLaunchRpm || row == kBoost) && v <= 0) return row == kBoost ? "Springs (12.8 psi)" : "Off";
    if (row == kPops) { std::snprintf(b, sizeof b, "%.0f %%", v * 100); return b; }
    std::snprintf(b, sizeof b, row == kTiming || row == kFuel ? "%+.0f %s" : "%.0f %s", v, kRows[row].unit);
    return b;
}

// Saved as one number per ECU (the world's wear file: whole numbers are exact in a double). Steps from each row's
// minimum, 6 bits a row; +2 so that the store's "never seen" 1.0 reads as the default tune.
inline double pack(const Tune& t) {
    uint64_t bits = 0;
    for (int r = 0; r < kRowCount; ++r) {
        uint64_t s = uint64_t(std::lround((get(t, r) - kRows[r].min) / kRows[r].step)) & 63u;
        bits |= s << (6 * r);
    }
    return double(bits) + 2.0;
}
inline Tune unpack(double v) {
    Tune t;
    if (!std::isfinite(v) || v < 2.0 || v > 9.0e15) return t;
    uint64_t bits = uint64_t(v - 2.0);
    for (int r = 0; r < kRowCount; ++r) set(t, r, kRows[r].min + double((bits >> (6 * r)) & 63u) * kRows[r].step);
    return t;
}
// 6 bits hold 0-63 steps: the RPM rows need 48 and 36, so they fit.
static_assert(12000 / 250 < 64 && 9000 / 250 < 64, "rpm rows fit in 6 bits");

// ------------------------------------------------------------------------------------- data channels
// Names and directions in gauges_logic.h (kEcuChannels), in this order.
enum Channel { kInRevLimit, kInLaunchRpm, kInLaunch, kInBoost, kInTiming, kInFuel, kInPops, kInAntilag,
               kOutRpm, kOutBoostPsi, kOutKnock, kOutPowerScale, kOutKnockLimit, kChannelCount };
// The tune the engines get: the panel's, with every wired input that is not 0 in its place.
inline Tune effective(const Tune& panel, const double* slots) {
    Tune t = panel;
    if (!slots) return t;
    for (int r = 0; r < kRowCount; ++r) {
        double v = slots[r];
        if (std::isfinite(v) && v != 0.0) set(t, r, v);
    }
    return t;
}

// ------------------------------------------------------------------------------------- what the tune does
// Power over the stock tune (the engine's boost factor is multiplied by it).
inline double power_scale(const Tune& t) {
    double timing = 1.0 + 0.012 * std::clamp(t.timing, -10.0, 10.0);
    double f = std::clamp(t.fuel_trim, -15.0, 15.0), fuel = 1.0;
    if (f < 0) fuel = f >= -8.0 ? 1.0 + 0.005 * -f : 1.04 - 0.012 * (-f - 8.0);   // lean: best at -8, then it falls off
    else fuel = 1.0 - 0.002 * f;                                                     // rich: a little less
    return timing * fuel;
}
// The engine's knock limit (boost factor) with this tune: advance and lean knock sooner, retard and rich later.
inline double rated_with(double rated, const Tune& t) {
    double extra = std::max(0.0, rated - 1.0);
    double scale = 1.0 - 0.02 * std::clamp(t.timing, -10.0, 10.0);
    double f = std::clamp(t.fuel_trim, -15.0, 15.0);
    scale *= f < 0 ? 1.0 - 0.015 * -f : 1.0 + 0.008 * f;
    return 1.0 + extra * std::clamp(scale, 0.4, 1.4);
}
// Fuel burned over the stock tune (the game burns fuel in proportion to the factor; trim adds or takes some).
inline double fuel_scale(const Tune& t) { return 1.0 + std::clamp(t.fuel_trim, -15.0, 15.0) / 100.0; }
// How much the overrun pops: the tune's pops, more when rich.
inline double crackle(const Tune& t) { return std::clamp(t.pops + std::max(0.0, t.fuel_trim) * 0.02, 0.0, 1.0); }

// The rev limiter: cuts at the limit, fires again 150 RPM under it (the latch is the caller's).
inline bool limiter_cut(bool& cutting, double rpm, double limit) {
    if (!(limit > 0) || !std::isfinite(rpm)) { cutting = false; return false; }
    if (cutting) cutting = rpm > limit - 150.0;
    else cutting = rpm >= limit;
    return cutting;
}

}  // namespace ecu
