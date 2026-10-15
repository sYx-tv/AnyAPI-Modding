#pragma once
// Dyno: records power and torque curves from a vehicle's engines (pure logic, no game calls; tested by
// tests/dyno_logic_test.cpp).
//
// What the game gives (Anymaker 0.1.25, research-engine-air.md): each engine tick writes m_display_torque, the energy
// it turned into motion that tick (power = energy * 60 W at the game's 60 ticks per second), and m_rps. Power follows
// the game's own formula: it climbs with RPM up to about 6,400 RPM, stays flat to the 8,000 RPM fuel cut and is
// multiplied by boost. So a pull on the road reads the same as a pull on rollers would.
//
// A pull: the throttle held at 90 % or more. Samples go into 100 RPM bins (averaged), and when the throttle comes off
// the pull is kept if it covered at least 1,500 RPM over at least a second. The last three pulls are kept, oldest
// first, so builds can be compared.
//
// A pull also ends when it reaches the fuel cut, like a real dyno run stops at the limiter. Bouncing on it, the engine
// makes power only on the ticks that start under 8,000 RPM, and a turbo still spooling there kept raising those last
// bins (Nate's first rotary pulls, 2026-10-15). The next pull starts once the throttle has come off. The mod gives each
// sample the RPM the engine had when it made that power (read before the game's engine tick): read after, the
// bounce showed up as power at 8,600 to 8,900 RPM.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace dyno {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWattsPerHp = 745.69987;
constexpr double kTicksPerSecond = 60.0;     // m_display_torque is energy per tick
constexpr int kBinRpm = 100, kMaxRpm = 9000, kBins = kMaxRpm / kBinRpm + 1;
constexpr int kRunsKept = 3;
constexpr double kFullThrottle = 0.9, kMinSpanRpm = 1500, kMinSeconds = 1.0, kMaxSeconds = 90.0;
constexpr double kFuelCutRpm = 8000;          // the game's engines make no power above it
constexpr double kAtCutRpm = kFuelCutRpm - 60;   // a sample this close counts as reaching the cut

inline double torque_nm(double watts, double rps) { return rps > 0.5 ? watts / (2 * kPi * rps) : 0.0; }

// One tick of a vehicle's engines, summed (power, knock) or the highest (the rest). knock is the knock damage Turbo's
// hot boost air did this tick (0 without it); the game's other engine damage (low oil, overheating) is not knock.
struct EngineSample {
    double rps = 0, watts = 0, throttle = 0, boost = 1, temperature_c = 0, knock = 0;
    int engines = 0;
};

struct Run {
    double sum[kBins]{};
    int count[kBins]{};
    int lo = kBins, hi = -1;                 // first and last bin with samples
    double seconds = 0, boost_peak = 1, temperature_peak = 0, knock_peak = 0;
    uint32_t number = 0;

    bool has(int bin) const { return bin >= 0 && bin < kBins && count[bin] > 0; }
    double watts(int bin) const { return has(bin) ? sum[bin] / count[bin] : 0.0; }
    static double bin_rpm(int bin) { return double(bin) * kBinRpm; }
    double span_rpm() const { return hi >= lo ? double(hi - lo) * kBinRpm : 0.0; }
    bool valid() const { return span_rpm() >= kMinSpanRpm && seconds >= kMinSeconds; }
    bool knocked() const { return knock_peak > 0; }
    void peak_power(double& watts_out, double& rpm_out) const {
        watts_out = 0; rpm_out = 0;
        for (int b = lo; b <= hi; ++b) if (has(b) && watts(b) > watts_out) { watts_out = watts(b); rpm_out = bin_rpm(b); }
    }
    void peak_torque(double& nm_out, double& rpm_out) const {
        nm_out = 0; rpm_out = 0;
        for (int b = lo; b <= hi; ++b) {
            if (!has(b) || b == 0) continue;
            double nm = torque_nm(watts(b), bin_rpm(b) / 60.0);
            if (nm > nm_out) { nm_out = nm; rpm_out = bin_rpm(b); }
        }
    }
};

struct Recorder {
    Run runs[kRunsKept];                     // oldest first; runs[count - 1] is the latest
    int count = 0;
    Run current;
    bool active = false;
    bool wait_lift = false;                  // the last pull ended at the fuel cut: wait for the throttle to come off
    uint32_t next_number = 1;
    EngineSample live;                       // the latest tick, for the readout

    // Forgets the kept pulls; a pull being recorded carries on and becomes pull 1.
    void clear() {
        for (Run& run : runs) run = Run{};
        count = 0;
        next_number = 1;
    }

    // Returns true when this tick finished a kept pull.
    bool feed(const EngineSample& s, double dt) {
        live = s;
        if (!(dt > 0) || dt > 0.25) dt = 1.0 / kTicksPerSecond;
        double rpm = s.rps * 60.0;
        bool full = s.engines > 0 && s.throttle >= kFullThrottle && std::isfinite(rpm) && rpm > 0 && std::isfinite(s.watts);
        if (wait_lift) {
            if (!full) wait_lift = false;
            return false;
        }
        if (!active) {
            if (!full) return false;
            current = Run{};
            active = true;
        }
        if (!full || current.seconds > kMaxSeconds) return finish();
        int bin = int(std::lround(std::min(rpm, double(kMaxRpm)) / kBinRpm));
        if (s.watts > 0 && s.rps > 2) {      // the game only reports power above 2 rev/s
            current.sum[bin] += s.watts;
            ++current.count[bin];
            current.lo = std::min(current.lo, bin);
            current.hi = std::max(current.hi, bin);
        }
        current.seconds += dt;
        current.boost_peak = std::max(current.boost_peak, s.boost);
        current.temperature_peak = std::max(current.temperature_peak, s.temperature_c);
        if (std::isfinite(s.knock)) current.knock_peak = std::max(current.knock_peak, s.knock);
        if (rpm >= kAtCutRpm && s.watts > 0) { wait_lift = true; return finish(); }
        return false;
    }

    bool finish() {
        active = false;
        if (!current.valid()) return false;
        current.number = next_number++;
        if (count == kRunsKept) { for (int i = 1; i < kRunsKept; ++i) runs[i - 1] = runs[i]; --count; }
        runs[count++] = current;
        return true;
    }
    const Run* latest() const { return count ? &runs[count - 1] : nullptr; }
};

// Axis top for a value: the next of 1, 2, 2.5 or 5 x 10^n at or above it with a little headroom, and its step (four
// or five grid lines).
inline void nice_axis(double value, double& top, double& step) {
    value = std::max(value * 1.08, 1.0);
    double mag = std::pow(10.0, std::floor(std::log10(value)));
    const double steps[] = {0.2, 0.25, 0.5, 1.0, 2.0, 2.5, 5.0};
    for (double k : steps) {
        double st = k * mag;
        double t = std::ceil(value / st) * st;
        if (t / st <= 6) { top = t; step = st; return; }
    }
    top = std::ceil(value / mag) * mag; step = mag;
}

inline std::string fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
inline std::string thousands(double v) {      // 6400 -> "6,400"
    long long n = std::llround(v);
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return n < 0 ? "-" + s : s;
}

// Data port channels. Outputs read the latest values; clear_runs (input) above 0.5 clears the kept pulls.
enum Channel { kRpm, kPowerHp, kPowerKw, kTorqueNm, kBoost, kEngineTemperature, kRecording, kPeakHp, kPeakRpm, kPeakTorqueNm, kRunCount, kClearRuns, kChannelCount };
struct ChannelInfo { const char* name; bool input; const wchar_t* help; };
inline constexpr ChannelInfo kChannels[kChannelCount] = {
    {"rpm", false, L"Engine speed (highest engine)"},
    {"power_hp", false, L"Engine power now, all engines, hp"},
    {"power_kw", false, L"Engine power now, kW"},
    {"torque_nm", false, L"Engine torque now, N·m"},
    {"boost", false, L"Intake pressure factor (1 = none, 2 = one turbo's full boost, up to 3 with compound turbos)"},
    {"engine_temperature", false, L"Hottest engine, °C"},
    {"recording", false, L"1 while a pull is being recorded"},
    {"peak_hp", false, L"Latest pull's peak power, hp"},
    {"peak_rpm", false, L"RPM of that peak"},
    {"peak_torque_nm", false, L"Latest pull's peak torque, N·m"},
    {"run_count", false, L"Pulls kept (0 to 3)"},
    {"clear_runs", true, L"Above 0.5: forget the kept pulls"},
};
inline int channel_index(const char* name, size_t length) {
    for (int i = 0; i < kChannelCount; ++i)
        if (std::char_traits<char>::length(kChannels[i].name) == length && std::char_traits<char>::compare(kChannels[i].name, name, length) == 0) return i;
    return -1;
}
inline void fill_outputs(const Recorder& r, double slots[kChannelCount]) {
    const EngineSample& s = r.live;
    slots[kRpm] = s.rps * 60.0;
    slots[kPowerHp] = s.watts / kWattsPerHp;
    slots[kPowerKw] = s.watts / 1000.0;
    slots[kTorqueNm] = torque_nm(s.watts, s.rps);
    slots[kBoost] = s.boost;
    slots[kEngineTemperature] = s.temperature_c;
    slots[kRecording] = r.active ? 1.0 : 0.0;
    double w = 0, rpm = 0, nm = 0, nm_rpm = 0;
    if (const Run* run = r.latest()) { run->peak_power(w, rpm); run->peak_torque(nm, nm_rpm); }
    slots[kPeakHp] = w / kWattsPerHp;
    slots[kPeakRpm] = rpm;
    slots[kPeakTorqueNm] = nm;
    slots[kRunCount] = r.count;
}

constexpr const char* kDefinitionId = "dyno_sensor";
constexpr const char* kStockMeshPath = "meshes/components/compass_sensor_a.mesh";
constexpr const char* kDynoMeshName = "anyengineparts_dyno_sensor_a";
constexpr const char* kDynoMeshPath = "meshes/components/anyengineparts_dyno_sensor_a.mesh";

// The part: a 1 x 2 x 1 sensor of the game's altitude sensor class (nothing else hooks that class), shaped like the
// compass sensor, with the dyno's data channels. tech_tier 3 and the "sensor" category, like the turbos' tier.
inline std::string definition_json(const std::string& mesh_path = kStockMeshPath) {
    std::string descriptors;
    for (const auto& c : kChannels) {
        if (!descriptors.empty()) descriptors += ",\n        ";
        descriptors += std::string("{\"type\": \"") + (c.input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + c.name + "\"}";
    }
    return std::string(R"({
  "definitions": [
    {
      "id": "dyno_sensor",
      "name": "Dyno",
      "description": "Records engine power and torque. Floor the throttle for a pull and the curves show on screen while you are near the vehicle. Keeps the last three pulls. Data port outputs rpm, power, torque and the latest peak.",
      "class": "altitude_sensor",
      "zones": [{"bounds_max": [0, 1, 0]}],
      "interaction_zones": [],
      "surfaces": [
        {"dir": 2, "gender": 2, "type": "port"},
        {"gender": 2, "type": "port"},
        {"dir": 1, "gender": 2, "type": "port"},
        {"dir": 4, "gender": 2, "type": "port"},
        {"dir": 5, "gender": 2, "type": "port"}
      ],
      "logic_nodes": [],
      "mesh_static": {"mesh_path": ")" + mesh_path + R"("},
      "meshes_dynamic": [],
      "category": "sensor",
      "tags_generated": "port,",
      "inspection_geometry": [],
      "mateable_port_types": 2,
      "data_descriptors": [
        )") + descriptors + R"(
      ],
      "tech_tier": 3,
      "spawn_probability": 0.2,
      "loot_group": "mechanical",
      "constraint_orientations": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],
      "force_emitter_element_rotor": {"blades": []},
      "liquid_pump_wattage": 1000.0,
      "liquid_pump_max_output_rps": 50.0,
      "gas_pump_wattage": 1000.0,
      "gas_pump_max_output_rps": 50.0
    }
  ]
}
)";
}

}  // namespace dyno
