#pragma once
// Turbo upkeep: oil feed, heat, air filter, wear, hot boost air and the intercooler. Pure logic, no game calls,
// so tests/turbo_logic_test.cpp can check it. turbo_mod.cpp feeds it from the game and applies the results.
//
// Game facts this is tuned to (game.gcl 0.1.25):
// - Engines put their exhaust out at a fixed 300 C, and overheat (damage) above 160 C.
// - An engine's boost factor is remap(clamp(intake pressure, 1, 10), 1, 10, 1, 2); intake temperature is never read.
//   AnyEngineParts scales the boost part of that factor by the intake air's density (cold air, more boost).
// - Engine oil is a mix of oil (liquid type 2) and pollutant (type 3). Engines turn oil into pollutant as they run;
//   the stock oil filter removes it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace turbo {

// ------------------------------------------------------------------------------------------- heat and wear
constexpr double kExhaustC = 300.0;           // what the game's engines exhaust at
constexpr double kTurboHotC = 250.0;          // above this the turbo wears faster, more the hotter it gets
constexpr double kTurboMaxC = 600.0;
constexpr double kOilFullFlow = 0.5;          // L/s of oil through the turbo that cools it fully
constexpr double kHalfLifeHours = 20.0;       // hours of hard use (full boost) to lose half: oil fed, filtered air
constexpr double kNoOilWear = 3.0;            // no oil flow (on top of the heat it brings)
constexpr double kNoFilterWear = 4.0;         // unfiltered air
constexpr double kNeitherWear = 1.5;          // both missing: extra on top of the two
constexpr double kPollutedWear = 4.0;         // fully polluted oil, extra wear (scales with the pollutant share)

struct UpkeepInputs {
    double spool = 0;            // 0-1
    double exhaust_fraction = 0; // exhaust flow over the size's rated flow, 0-1+
    double exhaust_c = kExhaustC;
    double oil_lps = 0;          // oil flowing through the turbo
    double oil_c = 20;           // the oil's temperature where it comes in
    double oil_pollution = 0;    // pollutant share of the oil, 0-1
    bool filter = false;         // an air filter on the compressor intake
    double filter_condition = 1; // that filter's condition, 0-1 (dirty filters restrict)
    double ambient_c = 20;
    double dt = 1.0 / 60;
};
struct UpkeepState {
    double temperature = 20;     // turbo body, C
    double condition = 1;        // 1 new, 0 blown
};
struct UpkeepStep {
    double oil_out_c = 20;       // temperature of the oil leaving the turbo
    double wear_rate = 0;        // condition lost per second this tick (for the outputs)
    double filter_wear = 0;      // filter condition lost this tick
};

// How well the oil flow cools: 0 dry, 1 at kOilFullFlow or more.
inline double oil_cooling(double oil_lps) { return std::clamp(std::isfinite(oil_lps) ? oil_lps / kOilFullFlow : 0.0, 0.0, 1.0); }

// Wear multiplier for the setup (1 = oil fed, filtered, clean oil).
inline double wear_multiplier(bool oiled, bool filtered, double pollution) {
    double m = (oiled ? 1.0 : kNoOilWear) * (filtered ? 1.0 : kNoFilterWear) * (!oiled && !filtered ? kNeitherWear : 1.0);
    if (oiled) m *= 1.0 + kPollutedWear * std::clamp(std::isfinite(pollution) ? pollution : 0.0, 0.0, 1.0);
    return m;
}
inline double heat_wear(double temperature_c) { return 1.0 + std::max(0.0, (temperature_c - kTurboHotC) / 50.0); }

// The filter clogs with the air it cleans: about 10 hours of full-boost running to go fully dirty (a supercharger's
// filter by its share of full boost the same way).
inline double filter_clog(double spool, double dt) { return spool * dt / (10.0 * 3600.0); }

inline UpkeepStep upkeep(UpkeepState& st, const UpkeepInputs& in) {
    UpkeepStep out;
    double dt = std::isfinite(in.dt) && in.dt > 0 ? std::min(in.dt, 0.5) : 0.0;
    double spool = std::clamp(std::isfinite(in.spool) ? in.spool : 0.0, 0.0, 1.0);
    double exh = std::clamp(std::isfinite(in.exhaust_fraction) ? in.exhaust_fraction : 0.0, 0.0, 1.5);
    double cool = oil_cooling(in.oil_lps);
    double oil_c = std::isfinite(in.oil_c) ? in.oil_c : in.ambient_c;
    if (!std::isfinite(st.temperature)) st.temperature = in.ambient_c;
    // Heat: the exhaust soaks the hot side, the bearings heat with shaft speed (four times as much dry). Oil
    // carries the heat away; the housing loses a little to the air.
    double heat = 0.5 * exh * (in.exhaust_c - st.temperature) + 15.0 * spool * spool * (cool > 0.05 ? 1.0 : 4.0);
    double loss = 2.0 * cool * (st.temperature - oil_c) + 0.02 * (st.temperature - in.ambient_c);
    st.temperature = std::clamp(st.temperature + (heat - loss) * dt, -60.0, kTurboMaxC);
    // The oil leaving it carries that heat: it comes out half way to the turbo's temperature.
    out.oil_out_c = oil_c + 0.5 * (st.temperature - oil_c);
    // Wear: hard use wears it, the setup and the heat multiply that.
    double load = 0.1 + 0.9 * spool * spool;
    double base = -std::log(0.5) / (kHalfLifeHours * 3600.0);   // per second at full load
    double rate = base * load * wear_multiplier(cool > 0.05, in.filter, in.oil_pollution) * heat_wear(st.temperature);
    if (spool < 0.02 && exh < 0.02) rate = 0;   // not running: no wear (heat soak still cools down)
    st.condition = std::clamp(st.condition * std::exp(-rate * dt), 0.0, 1.0);
    if (st.condition < 0.001) st.condition = 0;
    out.wear_rate = rate;
    if (in.filter) out.filter_wear = filter_clog(spool, dt);
    return out;
}

// What the turbo can still do. Above half condition it is as good as new; below, its boost falls to 60% at the
// end; at 0 it has blown (no boost, the shaft does not turn, and the intake chokes: kBlownFlowShare in turbo_logic.h).
inline double condition_head_factor(double condition) {
    double c = std::clamp(std::isfinite(condition) ? condition : 1.0, 0.0, 1.0);
    if (c <= 0) return 0;
    return c >= 0.5 ? 1.0 : 0.6 + 0.8 * c;
}
inline bool bearing_whine(double condition) { return std::isfinite(condition) && condition < 0.2 && condition > 0; }
// Worn right out (upkeep snaps the last 0.1% to 0). A turbo that reaches it during a tick blows up; one loaded at 0
// is already blown.
inline bool turbo_blown(double condition) { return std::isfinite(condition) && condition <= 0; }
// A dirty filter restricts the intake: full boost down to 85% when fully clogged.
inline double filter_head_factor(bool filter, double condition) {
    return filter ? 0.85 + 0.15 * std::clamp(std::isfinite(condition) ? condition : 1.0, 0.0, 1.0) : 1.0;
}

// ------------------------------------------------------------------------------------------- hot boost air
// Compressing air heats it, as a real compressor does: the outlet is the inlet times the pressure ratio's
// isentropic rise, over the compressor's efficiency where it runs. At full boost (a ratio of 1.87) at its best
// efficiency that is about +80 C on 20 C air; a compressor run off its best (near surge or choke) heats it more.
inline double compressor_heat(double inlet_c, double pressure_ratio, double efficiency) {
    double in = std::isfinite(inlet_c) ? std::clamp(inlet_c, -50.0, 600.0) : 20.0;
    double ratio = std::isfinite(pressure_ratio) ? std::max(1.0, pressure_ratio) : 1.0;
    double eta = std::isfinite(efficiency) ? std::clamp(efficiency, 0.2, 1.0) : 0.7;
    return (in + 273.15) * (std::pow(ratio, 0.2857) - 1.0) / eta;
}
// The air a compressor sends on: its inlet air (outside air, or a first stage's boost) heated by compressing it, then
// cooled towards the outside air by the intercoolers' effectiveness (intercooler_effectiveness below).
inline double boost_air_c(double inlet_c, double pressure_ratio, double efficiency, double cooling, double ambient_c) {
    double in = std::isfinite(inlet_c) ? std::clamp(inlet_c, -50.0, 600.0) : ambient_c;
    double hot = in + compressor_heat(in, pressure_ratio, efficiency);
    return hot - (hot - ambient_c) * std::clamp(std::isfinite(cooling) ? cooling : 0.0, 0.0, 1.0);
}
// Intercoolers on the boost pipe: how much of that heat they take back out (0-1). `cells` is their total frontal
// area in cells, `flow` the air through them in L/s: bigger cores and slower air cool better.
inline double intercooler_effectiveness(double cells, double flow_lps) {
    if (!(cells > 0)) return 0;
    double f = std::max(0.0, std::isfinite(flow_lps) ? flow_lps : 0.0);
    return 1.0 - std::exp(-cells * 0.6 / (1.0 + f / 20.0));
}
// The engine's boost factor for its intake air temperature: the boost part of the factor scales with the air's
// density against 30 C air, so hot boost costs power and cold air makes more.
constexpr double kReferenceC = 30.0;
inline double intake_density_ratio(double intake_c) {
    double t = std::isfinite(intake_c) ? intake_c : kReferenceC;
    return std::clamp((kReferenceC + 273.15) / (std::max(t, -100.0) + 273.15), 0.5, 1.2);
}
inline double boost_factor_for_air(double factor, double intake_c) {
    if (!std::isfinite(factor) || factor <= 1.0) return factor;
    return 1.0 + (factor - 1.0) * intake_density_ratio(intake_c);
}
// Knock: hot boosted air hurts the engine. Added to the engine's own damage counter per tick (the game takes a
// damage step at 1): full boost from 20 C air with no intercooler (about 98 C) costs a step about every 8 to 9
// minutes. Real engines start to knock on boost well under 100 C of intake air.
constexpr double kKnockC = 60.0;
inline double knock_damage(double intake_c, double factor, double dt) {
    if (!std::isfinite(intake_c) || !std::isfinite(factor) || !(dt > 0)) return 0;
    double heat = std::max(0.0, intake_c - kKnockC) / 50.0, boost = std::max(0.0, factor - 1.15);
    return 0.003 * heat * boost * dt;
}


// ------------------------------------------------------------------------------------------- engine ratings
// No boost cap (Nate, 2026-11-04): instead every engine has a rating, the boost factor (power multiplier) it takes with
// 40 C charge air before it knocks. Past it the knock control pulls timing (most of the extra boost turns into heat, not
// power) and the engine takes damage, faster the further past it runs. Hot charge air lowers the rating, so an
// intercooler lets the same engine take more boost.
// The game's engines (rom/data/vehicle_component_definitions.json): inline engine / engine_block_b / engine_block_c,
// V v_engine / _b / _c, boxer opposed_engine / _b / _c, each small / medium / large and stretched along z (a cylinder
// per 2 / 3 / 5 cells on an inline, a pair on a V or boxer); AnyVehicleOverhaul's rotary_engine.
//   - Size: bigger blocks have thicker walls and bigger head bolts: small 2.0, medium 2.5, large 3.0.
//   - Layout: a closed-deck inline is the stoutest (+0.15), a boxer's long unsupported crank the weakest (-0.1).
//   - Length: every cylinder past 6 flexes the crank a little more (-0.02 each, down to -0.2).
//   - Rotary: 2.3 (no valves or pistons to hurt, but apex seals).
// A built engine or the ECU (later phases) raises these.
struct EngineRating { double factor = 2.0; int cylinders = 0; bool known = false; };
inline EngineRating engine_rating(const char* id, size_t n, int32_t extend_z) {
    auto is = [&](const char* want) { size_t w = std::char_traits<char>::length(want); return id && n == w && std::char_traits<char>::compare(id, want, n) == 0; };
    struct Row { const char* id; int layout; int size; };   // layout 0 inline, 1 V, 2 boxer; size 0-2
    static const Row rows[] = {{"engine", 0, 0}, {"engine_block_b", 0, 1}, {"engine_block_c", 0, 2},
                               {"v_engine", 1, 0}, {"v_engine_b", 1, 1}, {"v_engine_c", 1, 2},
                               {"opposed_engine", 2, 0}, {"opposed_engine_b", 2, 1}, {"opposed_engine_c", 2, 2}};
    EngineRating r;
    if (is("rotary_engine")) { r.factor = 2.3; r.known = true; return r; }
    for (const Row& row : rows) {
        if (!is(row.id)) continue;
        int64_t cells = extend_z < 0 ? -int64_t(extend_z) : int64_t(extend_z);
        if (cells > 4096) cells = 0;
        int steps = int(cells / (row.size == 0 ? 2 : row.size == 1 ? 3 : 5));
        r.cylinders = std::clamp(row.layout == 0 ? 1 + steps : 2 + 2 * steps, 1, 16);
        static constexpr double base[] = {2.0, 2.5, 3.0}, layout[] = {0.15, 0.0, -0.1};
        r.factor = base[row.size] + layout[row.layout] - std::min(0.2, 0.02 * std::max(0, r.cylinders - 6));
        r.known = true;
        return r;
    }
    return r;
}
// The rating with this charge air: hot air knocks sooner (from 40 C), cold air a little later.
inline double rated_factor_for_air(double rated, double intake_c) {
    double t = std::isfinite(intake_c) ? intake_c : kReferenceC;
    double share = std::clamp(1.0 - (t - 40.0) / 150.0, 0.3, 1.1);
    return 1.0 + (std::max(1.0, rated) - 1.0) * share;
}
struct Knock {
    double factor = 1;    // the boost factor the engine makes after knock control
    double damage = 0;    // added to the engine's damage counter this tick (the game takes a damage step at 1)
    double over = 0;      // how far past its rating, 0.1 = 10 %
};
// `factor` is the boost factor its intake pressure gives (after the air's density), `rated` the engine's rating.
// Past the rating, 30 % of the extra boost still makes power; damage grows with the square of how far past it is:
// 10 % over, a damage step about every 9 minutes; 30 % over, about every 100 s; 60 % over, about every 30 s.
inline Knock knock_step(double factor, double rated, double intake_c, double dt) {
    Knock k;
    k.factor = std::isfinite(factor) ? factor : 1.0;
    if (!(k.factor > 1.0) || !(dt > 0)) return k;
    double limit = rated_factor_for_air(rated, intake_c);
    k.over = std::max(0.0, k.factor / limit - 1.0);
    if (k.over <= 0) return k;
    k.factor = limit + (k.factor - limit) * 0.3;
    k.damage = (0.01 * k.over + 0.08 * k.over * k.over) * dt;
    return k;
}

}  // namespace turbo
