// ECU tune maths (ecu_logic.h).
#include "../ecu_logic.h"
#include "../gauges_logic.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace ecu;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e; }
int main() {
    Tune stock;
    assert(near(power_scale(stock), 1.0) && near(rated_with(2.0, stock), 2.0) && near(fuel_scale(stock), 1.0));
    // pack/unpack round trip, and the store's "never seen" 1.0 is the default tune
    Tune t; t.rev_limit = 7250; t.launch_rpm = 4500; t.launch = true; t.boost_psi = 22; t.timing = -4; t.fuel_trim = 6; t.pops = 0.75; t.antilag = true;
    Tune u = unpack(pack(t));
    for (int r = 0; r < kRowCount; ++r) assert(near(get(u, r), get(t, r)));
    Tune d = unpack(1.0);
    for (int r = 0; r < kRowCount; ++r) assert(near(get(d, r), get(stock, r)));
    assert(pack(stock) >= 2.0 && pack(t) < 9.0e15 && pack(t) == std::floor(pack(t)));
    // timing: advance makes power and knocks sooner; retard the other way
    Tune adv; adv.timing = 5; Tune ret; ret.timing = -5;
    assert(power_scale(adv) > 1.05 && power_scale(ret) < 0.95 && rated_with(2.0, adv) < 2.0 && rated_with(2.0, ret) > 2.0);
    // fuel: lean to -8 makes power, past it loses; rich is a little down but safer; rich pops more
    Tune lean8; lean8.fuel_trim = -8; Tune lean15; lean15.fuel_trim = -15; Tune rich; rich.fuel_trim = 10;
    assert(power_scale(lean8) > power_scale(stock) && power_scale(lean15) < power_scale(lean8) && power_scale(rich) < 1.0);
    assert(rated_with(2.0, lean8) < 2.0 && rated_with(2.0, rich) > 2.0 && crackle(rich) > crackle(stock));
    // panel rows clamp and step
    Tune n; nudge(n, kBoost, 3); assert(near(n.boost_psi, 3)); nudge(n, kBoost, -10); assert(near(n.boost_psi, 0));
    { Tune r; nudge(r, kRevLimit, 1); assert(near(r.rev_limit, 3000)); nudge(r, kRevLimit, 1); assert(near(r.rev_limit, 3250));
      nudge(r, kRevLimit, -2); assert(near(r.rev_limit, 0)); nudge(r, kLaunchRpm, 1); assert(near(r.launch_rpm, 2000)); }
    nudge(n, kRevLimit, 100); assert(near(n.rev_limit, 12000)); nudge(n, kLaunch, 1); assert(n.launch);
    assert(row_text(stock, kBoost) == "Springs (12.8 psi)" && row_text(adv, kTiming) == "+5 deg" && row_text(t, kAntilag) == "On");
    // wired inputs win when not 0
    double slots[16]{}; slots[kInBoost] = 30; slots[kInTiming] = -3;
    Tune e = effective(t, slots);
    assert(near(e.boost_psi, 30) && near(e.timing, -3) && near(e.rev_limit, 7250));
    // rev limiter: cuts at the limit, fires again 150 under it
    bool cut = false;
    assert(!limiter_cut(cut, 6900, 7000) && limiter_cut(cut, 7000, 7000) && limiter_cut(cut, 6900, 7000) && !limiter_cut(cut, 6840, 7000));
    assert(!limiter_cut(cut, 9000, 0));
    // the ECU part: its channels and definition
    assert(gauges::kind_of_id("engine_ecu", 10) == gauges::kEcu && gauges::channel_count(gauges::kEcu) == kChannelCount);
    assert(gauges::channel_index(gauges::kEcu, "fuel_trim", 9) == kInFuel && gauges::channel_index(gauges::kEcu, "knock_limit", 11) == kOutKnockLimit);
    std::string def = gauges::definition_entry(gauges::kEcu, "x");
    assert(def.find("\"id\": \"engine_ecu\"") != std::string::npos && def.find("\"name\": \"ECU\"") != std::string::npos);
    std::puts("ecu_logic ok");
}
