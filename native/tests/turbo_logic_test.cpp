#include "../turbo_logic.h"
#include "../turbo_mesh.h"
#include "../turbo_upkeep_mesh.h"
#include "../turbo_supercharger_mesh.h"
#include "../turbo_health.h"
#include "../turbo_wear_store.h"
#include "../turbo_sound.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
using namespace turbo;
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e; }
static bool st_ok(double v) { return std::isfinite(v) && v >= 0 && v <= 1; }

// A fake 1 x 3 x 3 impeller mesh in the stock layout: one quad on the intake face, one on the outlet stub.
static std::vector<uint8_t> fake_impeller() {
    std::vector<MeshVertex> v;
    auto add = [&](float x, float y, float z, float nx, float ny, float nz, uint8_t r) {
        MeshVertex m{}; m.pos[0] = x; m.pos[1] = y; m.pos[2] = z; m.rgba[0] = r; m.rgba[3] = 255; m.normal[0] = nx; m.normal[1] = ny; m.normal[2] = nz; v.push_back(m);
    };
    add(-0.04f, -0.12f, -0.12f, -1, 0, 0, 153); add(-0.04f, 0.12f, -0.12f, -1, 0, 0, 153); add(-0.04f, 0.12f, 0.12f, -1, 0, 0, 153); add(0.05f, 0.08f, 0.12f, 0, 0, 1, 51);
    std::vector<uint32_t> idx = {0, 1, 2, 1, 2, 3};
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); b.insert(b.end(), c, c + n); };
    uint32_t u = 5, one = 1, name = 4;
    put("mesh", 4); put(&u, 4); put(&one, 4); put(&name, 4); put("test", 4);
    std::vector<uint8_t> zeros(88, 0); put(zeros.data(), zeros.size());
    double bounds[6] = {-0.04, -0.12, -0.12, 0.05, 0.12, 0.12}; put(bounds, sizeof bounds);
    u = uint32_t(v.size() * sizeof(MeshVertex)); put(&u, 4); put(v.data(), v.size() * sizeof(MeshVertex));
    u = uint32_t(idx.size() * 4); put(&u, 4); put(idx.data(), idx.size() * 4);
    uint8_t trailer[8] = {}; put(trailer, 8);
    return b;
}

// Runs the model against a simple plant: an engine pushing `exhaust_lps` into the hot inlet, a vent on the hot
// outlet and the compressor intake, and an air manifold drawing `air_lps` from the compressor outlet.
struct Plant { double cap = 2.0, hot_in = 2.0, cold_out = 2.0; };
static double run(State& st, const Spec& s, Plant& pl, double exhaust_lps, double air_lps, double seconds, double dt = 1.0 / 60) {
    for (double t = 0; t < seconds; t += dt) {
        pl.hot_in += exhaust_lps * dt;
        pl.cold_out = std::max(0.0, pl.cold_out - air_lps * dt);
        Inputs in;
        in.pressure[kHotIn] = pl.hot_in / pl.cap; in.volume[kHotIn] = pl.hot_in;
        in.pressure[kHotOut] = 1; in.volume[kHotOut] = pl.cap;
        in.pressure[kColdIn] = 1; in.volume[kColdIn] = pl.cap;
        in.pressure[kColdOut] = pl.cold_out / pl.cap; in.volume[kColdOut] = pl.cold_out;
        in.dt = dt; in.exhaust_temperature = 300;          // the game's exhaust
        Step o = step(st, s, in);
        pl.hot_in -= o.turbine_move;
        pl.cold_out += o.compressor_move;
    }
    return pl.cold_out / pl.cap;
}

// Two turbos in series (a compound): the first stage `a` breathes outside air into a pipe the second stage `b`
// breathes from (intercooled between them). Their turbines are in series too, as a real compound is plumbed: the
// exhaust drives b's turbine first, then a's, then out. Returns the second stage's outlet pressure.
struct CompoundPlant { double cap = 2.0, hot_in = 2.0, hot_mid = 2.0, mid = 2.0, out = 2.0; };
static double run_compound(State& a, State& b, const Spec& sa, const Spec& sb, CompoundPlant& pl, double exhaust_lps, double air_lps, double seconds,
                           double dt = 1.0 / 60) {
    for (double t = 0; t < seconds; t += dt) {
        pl.hot_in += exhaust_lps * dt;
        pl.out = std::max(0.0, pl.out - air_lps * dt);
        Inputs ia, ib;
        ib.pressure[kHotIn] = pl.hot_in / pl.cap; ib.volume[kHotIn] = pl.hot_in;
        ib.pressure[kHotOut] = ia.pressure[kHotIn] = pl.hot_mid / pl.cap; ib.volume[kHotOut] = ia.volume[kHotIn] = pl.hot_mid;
        ia.pressure[kHotOut] = 1; ia.volume[kHotOut] = pl.cap;
        for (Inputs* in : {&ia, &ib}) { in->dt = dt; in->exhaust_temperature = 300; }
        ia.pressure[kColdIn] = 1; ia.volume[kColdIn] = pl.cap;
        ia.pressure[kColdOut] = pl.mid / pl.cap; ia.volume[kColdOut] = pl.mid;
        ib.pressure[kColdIn] = pl.mid / pl.cap; ib.volume[kColdIn] = pl.mid;
        ib.pressure[kColdOut] = pl.out / pl.cap; ib.volume[kColdOut] = pl.out;
        Step oa = step(a, sa, ia), ob = step(b, sb, ib);
        pl.hot_in -= ob.turbine_move;
        pl.hot_mid += ob.turbine_move - oa.turbine_move;
        pl.mid += oa.compressor_move - ob.compressor_move;
        pl.out += ob.compressor_move;
    }
    return pl.out / pl.cap;
}

// A supercharger on its belt feeding an engine's manifold that draws `air_lps`; the intake is open to the air.
// Returns the outlet pressure; `joules` adds up what the belt gave.
static double run_blower(State& st, const Spec& s, Plant& pl, double pulley_rps, double air_lps, double seconds, double* joules = nullptr,
                         bool bov = false, double dt = 1.0 / 60) {
    for (double t = 0; t < seconds; t += dt) {
        pl.cold_out = std::max(0.0, pl.cold_out - air_lps * dt);
        Inputs in;
        in.pressure[kColdIn] = 1; in.volume[kColdIn] = pl.cap;
        in.pressure[kColdOut] = pl.cold_out / pl.cap; in.volume[kColdOut] = pl.cold_out;
        in.dt = dt; in.bov_venting = bov;
        BlowerStep o = blower_step(st, s, in, pulley_rps);
        pl.cold_out += o.compressor_move;
        if (joules) *joules += o.drive_joules;
    }
    return pl.cold_out / pl.cap;
}

// A turbo on one of the game's engines (its tick from research-engine-air.md: fuel from volume, speed, boost and
// back pressure; air in, air plus burnt fuel out as exhaust), running at a fixed speed, an open tailpipe after the
// turbine. `volume` is the engine's (45 a rotor). Returns the seconds to 90 % of full boost at full
// throttle from a light-throttle cruise (-1: never in 6 s); `kpa` gets the boost it settles at.
static double tip_in(int n, double volume, double rpm, double* kpa = nullptr) {
    Spec s = spec(n); State st;
    double own = n * n * 0.25, hot_cap = own + 4, out_cap = own + 2, air_cap = own + 4;
    double hot = hot_cap, out = out_cap, air = air_cap, seconds = -1, dt = 1.0 / 60;
    for (int i = 0; i < 540; ++i) {
        double throttle = i < 180 ? 0.1 : 1.0;
        double boost = std::clamp(air / air_cap, 1.0, 19.0), back = hot / hot_cap;
        double fuel = volume * 400 / 60 * std::clamp(rpm / 8000 + 0.2, 0.0, 1.0) * (1 + (boost - 1) / 9) / 9.6e6 * throttle * (1 - std::clamp((back - 4) / 16, 0.0, 1.0));
        double used = std::min(air, fuel * 8720);
        air -= used; hot += used + fuel * 800;
        Inputs in; in.dt = dt; in.exhaust_temperature = 300; in.boost_capacity = air_cap;
        in.pressure[kHotIn] = hot / hot_cap; in.volume[kHotIn] = own * in.pressure[kHotIn];
        in.pressure[kHotOut] = out / out_cap; in.volume[kHotOut] = own * in.pressure[kHotOut];
        in.pressure[kColdIn] = 1; in.volume[kColdIn] = own;
        in.pressure[kColdOut] = air / air_cap; in.volume[kColdOut] = own * in.pressure[kColdOut];
        Step o = step(st, s, in);
        hot -= o.turbine_move; out += o.turbine_move; air += o.compressor_move;
        out -= std::min(out, 6.0 * (out / out_cap - 1));                       // a free-flowing tailpipe
        if (i >= 180 && seconds < 0 && (air / air_cap - 1) > 0.9 * kMaxHead) seconds = (i - 179) * dt;
    }
    if (kpa) *kpa = (air / air_cap - 1) * kKpaPerUnit;
    return seconds;
}

int main() {
    // Ids and sizes.
    for (int n : kTurboSizes) { std::string id = definition_id(n); assert(size_from_id(id.c_str(), id.size()) == n); }
    assert(size_from_id("turbo", 5) == 0 && size_from_id("turbocharger_7", 14) == 0 && size_from_id("turbocharger_1", 14) == 0);
    assert(size_from_id("turbocharger_2", 14) == 0 && size_from_id("turbocharger_4", 14) == 0 && size_from_id("turbocharger_5", 14) == 0);   // removed (Nate, 2026-10-15)
    // Specs grow with size: more flow, a heavier, slower shaft (more energy to spin up). The 6x6 keeps the 5x5's
    // numbers; it grew for its pipes.
    for (int n = kMinSize; n < 5; ++n) {
        Spec a = spec(n), b = spec(n + 1);
        assert(b.rated_flow > a.rated_flow && b.spool_kj > a.spool_kj && b.inertia > a.inertia && b.compressor_coeff > a.compressor_coeff && b.max_rpm < a.max_rpm);
    }
    assert(spec(6).rated_flow == spec(5).rated_flow && spec(6).spool_kj == spec(5).spool_kj && spec(6).max_rpm == spec(5).max_rpm);
    for (int n : kTurboSizes) {
        Spec s = spec(n);
        assert(near(0.5 * s.inertia * s.shaft_full * s.shaft_full, s.spool_kj * 1000, 1e-6 * s.spool_kj * 1000));   // its energy at full boost
        // The turbine passes its rated flow at a 2-unit drop; more drop pushes more through, but with the square root
        // of the pressure (it chokes), and nothing flows the wrong way into it.
        assert(near(turbine_flow(s, 3, 1), s.rated_flow, 1e-9 * s.rated_flow) && turbine_flow(s, 5, 1) < 1.7 * s.rated_flow && turbine_flow(s, 5, 1) > 1.4 * s.rated_flow);
        assert(turbine_flow(s, 1, 1) == 0 && turbine_flow(s, 1, 3) < 0);
    }
    assert(!spec(3).large_pipe && spec(4).large_pipe && spec(6).large_pipe);
    // Channels.
    for (int i = 0; i < kChannelCount; ++i) assert(channel_index(kChannels[i].name, std::strlen(kChannels[i].name)) == i && kChannels[i].input == (i == kFirstInput));
    assert(channel_index("pressure", 8) < 0);
    // Ports: inside the part, axial ones on the front and back faces. A large pipe end (2 x 2 cells) lies wholly on the
    // part's face too: it covers exactly 4 of the part's cells, all on the face it points out of.
    for (int n = kMinSize; n <= kMaxSize; ++n)
        for (int k = 0; k < kNodeCount; ++k) {
            Port p = port(n, Node(k)); assert(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y < n && p.z >= 0 && p.z < n);
            int covered = 0;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < n; ++y)
                    for (int z = 0; z < n; ++z)
                        if (port_covers(n, Node(k), x, y, z)) {
                            ++covered;
                            int face[6] = {x, 1 - x, y, n - 1 - y, z, n - 1 - z};   // distance to the face each direction points out of
                            assert(face[p.dir] == 0);
                        }
            assert(covered == (spec(n).large_pipe ? 4 : 1));
        }
    assert(port(3, kColdIn).dir == 0 && port(3, kHotOut).dir == 1 && port(3, kColdIn).y == 1 && port(3, kColdOut).y == 2 && port(4, kColdOut).y == 2 &&
           port(5, kColdOut).y == 3 && port(3, kHotIn).x == 1 && port(4, kHotIn).x == 0 && port(5, kHotIn).z == 0);
    // The stock large impeller (turbo_d, 6x6, 2 cells thick) anchors its large +z outlet the same way: [0, top - 1, top].
    assert(port(5, kColdOut).x == 0 && port(5, kColdOut).z == 4);
    // The 6x6: the large intake and exhaust out sit dead centre (cells 2-3 of 0-5), and the boost outlet and turbine
    // inlet span both of its 2 cells of depth, so every pipe is centred (Nate tried 3 deep, which left those two half
    // a cell off, and picked 2 deep like the stock Large Impeller).
    assert(port(6, kColdIn).y == 2 && port(6, kColdIn).z == 2 && port(6, kHotOut).x == 1 && port(6, kHotOut).y == 2 && port(6, kHotOut).z == 2 &&
           port(6, kHotIn).x == 0 && port(6, kHotIn).z == 0 && port(6, kColdOut).x == 0 && port(6, kColdOut).y == 4 && port(6, kColdOut).z == 5);
    // Oil ports: under the part (small liquid nodes), the feed under the cold side and the drain under the hot side,
    // each in a cell no gas node touches (no stock part mixes gas and liquid nodes in one cell).
    for (int n = kMinSize; n <= kMaxSize; ++n)
        for (bool in : {true, false}) {
            Port o = oil_port(n, in);
            assert(o.y == 0 && o.dir == 2 && o.x == (in ? 0 : 1) && o.z >= 0 && o.z < n);
            for (int k = 0; k < kNodeCount; ++k) assert(!port_covers(n, Node(k), o.x, o.y, o.z));
        }
    // Definitions file: the 3x3 and 6x6 turbos (the 2x2, 4x4 and 5x5 are gone), four gas nodes each, the stock class,
    // balanced braces.
    bool built[5] = {true, false, true, true, true}, bov_built[2] = {false, true}, wg_built[2] = {true, false};
    std::string json = definitions_json(built, bov_built, wg_built);
    size_t count = 0;
    for (size_t at = 0; (at = json.find("\"class\": \"gas_pump_centrifugal\"", at)) != std::string::npos; ++at) ++count;
    assert(count == 8);                                    // two turbos, two of each valve, two superchargers
    assert(json.find("turbocharger_2") == std::string::npos && json.find("turbocharger_4") == std::string::npos && json.find("turbocharger_5") == std::string::npos &&
           json.find("Turbo 2x2") == std::string::npos && json.find("Turbo 4x4") == std::string::npos && json.find("5x5") == std::string::npos && json.find("2x2 and") == std::string::npos);
    assert(json.find("\"bounds_max\": [1, 5, 5]") != std::string::npos && json.find("6x6x2") != std::string::npos);   // the 6x6 is 2 deep
    assert(json.find("\"class\": \"gas_tank\"") != std::string::npos);   // the intercooler: gas ports snap onto it
    assert(json.find("\"class\": \"gas_vent\"") != std::string::npos && json.find("\"class\": \"gas_vent_b\"") != std::string::npos);
    assert(json.find("\"id\": \"oil_in\"") != std::string::npos && json.find("\"type\": \"liquid\"") != std::string::npos);
    assert(json.find("radiator_a_0_0_0.mesh") != std::string::npos && json.find("\"mode_y\": \"tile\"") != std::string::npos);
    {   // with every mesh built the parts use theirs; the intercooler points at its first tile
        bool all[5] = {true, true, true, true, true}, two[2] = {true, true}, one = true;
        std::string full = definitions_json(all, two, two, two, &one);
        assert(full.find("turbo_turbo_air_filter_large.mesh") != std::string::npos && full.find("turbo_turbo_intercooler_0_0_0.mesh") != std::string::npos && full.find("turbo_turbo_intercooler_round_0_0_0.mesh") != std::string::npos && full.find("Turbo Intercooler (Round Tank)") != std::string::npos && full.find("\"mode_x\": \"tile\"") != std::string::npos &&
               full.find("radiator_a_0_0_0.mesh") == std::string::npos);
        // the oil ports come after the four gas nodes, so saved gas links keep their meaning
        size_t entry = full.find("\"name\": \"Turbo 3x3\"");
        size_t oil = full.find("oil_in", entry), gas = full.find("\"type\": \"gas\"", entry);
        size_t last_gas = gas;
        for (int k = 0; k < 3; ++k) last_gas = full.find("\"type\": \"gas\"", last_gas + 1);
        assert(oil != std::string::npos && last_gas < oil);
    }
    assert(json.find("turbo_turbo_wastegate.mesh") != std::string::npos && json.find("turbo_turbo_wastegate_large.mesh") == std::string::npos &&
           json.find("\"id\": \"turbo_wastegate_large\"") != std::string::npos);
    assert(json.find("\"id\": \"turbo_blow_off_valve_large\"") != std::string::npos && json.find("turbo_turbo_blow_off_valve_large.mesh") != std::string::npos &&
           json.find("turbo_turbo_blow_off_valve.mesh") == std::string::npos);
    assert(json.find("\"direction\": 5, \"type\": \"gas\", \"size\": 1}") != std::string::npos);   // the large valve's Air port is a large pipe
    assert(json.find("turbo_turbocharger_6.mesh") != std::string::npos && json.find("turbo_turbocharger_3.mesh") == std::string::npos && json.find("turbo_a.mesh") != std::string::npos);
    assert(json.find("\"name\": \"Turbo 6x6\"") != std::string::npos && json.find("\"type_f64_input\", \"name\": \"boost_limit\"") != std::string::npos);
    int depth = 0, square = 0; bool in_string = false;
    for (char c : json) {
        if (c == '"') in_string = !in_string;
        if (in_string) continue;
        depth += c == '{' ? 1 : c == '}' ? -1 : 0; square += c == '[' ? 1 : c == ']' ? -1 : 0;
        assert(depth >= 0 && square >= 0);
    }
    assert(depth == 0 && square == 0 && !in_string);
    assert(json.find("\"id\": \"turbo_blow_off_valve\"") != std::string::npos && json.find("\"type_f64_input\", \"name\": \"open\"") != std::string::npos);
    assert(valve_from_id("turbo_wastegate", 15) == kWastegate && valve_from_id("turbo_blow_off_valve", 20) == kBlowOff && valve_from_id("turbo_blow_off_valve_large", 26) == kBlowOffLarge && valve_from_id("turbo", 5) == kNoValve &&
           valve_from_id("turbo_wastegate_large", 21) == kWastegateLarge && valve_from_id("turbo_intercooler", 17) == kIntercooler && valve_from_id("turbo_intercooler_round", 23) == kIntercooler && valve_from_id("turbo_air_filter_large", 22) == kAirFilterLarge);
    assert(wastegate_move(1, 3, 1, 50, 50, 1.0 / 60, true) > wastegate_move(1, 3, 1, 50, 50, 1.0 / 60) * 2);   // the large wastegate flows more
    assert(valve_channel_index("open", 4) == kValveOpen && valve_channel_index("pressure", 8) == kValvePressure && valve_channel_index("x", 1) < 0);
    // Valves.
    assert(blow_off_vent(0.3, 5, 10, 1.0 / 60) == 0);                 // 2.9 kPa excess, under a 5 kPa spring
    assert(blow_off_vent(1.0, 5, 10, 1.0 / 60) > 0.9);                // 9.8 kPa excess vents
    assert(blow_off_vent(1.0, 5, 0.2, 1.0 / 60) <= 0.2);              // never more than the inlet holds
    // Off throttle: a puff every few ticks as the spool eases down never shows the valve open; a real dump does.
    {
        double flow = 0; bool shown = false;
        for (int i = 0; i < 600; ++i) {
            double litres = i % 6 == 0 ? blow_off_vent(0.53, 5, 10, 1.0 / 60) : 0;
            flow += (litres * 60 - flow) * (1.0 / 60 / 0.15);
            shown = shown || blow_off_shows_open(true, flow);
        }
        assert(!shown);
        flow = 0;
        for (int i = 0; i < 10; ++i) flow += (blow_off_vent(2.0, 5, 10, 1.0 / 60) * 60 - flow) * (1.0 / 60 / 0.15);
        assert(blow_off_shows_open(true, flow) && !blow_off_shows_open(false, flow));
    }
    assert(blow_off_vent(1.0, 5, 50, 1.0 / 60, true) > blow_off_vent(1.0, 5, 50, 1.0 / 60) * 2);   // the large valve vents faster
    assert(wastegate_move(0, 3, 1, 5, 5, 1.0 / 60) == 0 && wastegate_move(1, 3, 1, 5, 5, 1.0 / 60) > 1.0 && wastegate_move(0.5, 3, 1, 5, 5, 1.0 / 60) < wastegate_move(1, 3, 1, 5, 5, 1.0 / 60));
    assert(wastegate_move(1, 1, 3, 5, 5, 1.0 / 60) < 0 && std::isfinite(wastegate_move(NAN, NAN, 1, 5, 5, 1.0 / 60)));
    // Model: no exhaust, no spool; the compressor still lets a naturally aspirated engine breathe.
    { State st; Plant pl; Spec s = spec(3); double p = run(st, s, pl, 0, 8, 10); assert(st.spool < 1e-9 && p > 0.6 && p < 1.0); }
    // Rated exhaust flow spools to full boost and back pressure stays under the 4 units that cut engine power; the
    // built-in wastegate holds it there, bypassing what the shaft does not need.
    for (int n : kTurboSizes) {
        State st; Plant pl; Spec s = spec(n); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.9, 20);
        assert(st.spool > 0.95 && p > 8.5 && p < 10.5 && pl.hot_in / pl.cap < 4.0 && st.bypass > 0.2 && st.slots[kBypass] == st.bypass);
        assert(near(st.slots[kPowerMultiplier], power_multiplier(p), 0.05) && st.speed <= kOverspeed && near(st.slots[kRpm], st.speed * s.max_rpm));
    }
    // Lag comes from the shaft, not a timer: on the same exhaust a 6x6's heavier shaft spools slower than a 3x3's,
    // and more exhaust spools the same turbo faster.
    { State a, b; Plant pa, pb; run(a, spec(3), pa, 22, 18, 0.5); run(b, spec(6), pb, 22, 18, 0.5); assert(a.spool > b.spool + 0.15); }
    { State a, b; Plant pa, pb; run(a, spec(6), pa, 40, 0, 0.25); run(b, spec(6), pb, 65, 0, 0.25); assert(b.spool > a.spool + 0.1); }
    // On a real engine (the game's engine tick): matched turbos reach 90 % of full boost in well under two seconds
    // from a cruise at 3,500 RPM and hold it, so full boost no longer waits for the redline; a 6x6 on a 2-rotor at
    // low revs never gets there (too little exhaust), and every match makes some boost from 2,500 RPM.
    { double kpa = 0;
      double t = tip_in(6, 180, 3500, &kpa); assert(t > 0.5 && t < 2.0 && kpa > 80 && kpa < 95);    // 4-rotor
      t = tip_in(3, 90, 3500, &kpa); assert(t > 0.5 && t < 2.0 && kpa > 80 && kpa < 95);           // 2-rotor
      assert(tip_in(6, 180, 2500) > tip_in(6, 180, 5000));                                           // more revs, more exhaust, less lag
      t = tip_in(6, 90, 2500, &kpa); assert(t < 0 && kpa > 15 && kpa < 60); }
    // The compressor's efficiency: best at 3/4 of its design flow for its speed, worse towards surge and choke.
    { Spec s = spec(3); double best = compressor_efficiency(s, 0.75 * s.design_flow, 1);
      assert(near(best, kCompressorEfficiency) && compressor_efficiency(s, 0.1 * s.design_flow, 1) < best - 0.1 && compressor_efficiency(s, 2 * s.design_flow, 1) < best - 0.1 &&
             compressor_efficiency(s, 0, 1) >= 0.4 && near(compressor_efficiency(s, 0.375 * s.design_flow, 0.5), best)); }
    // Filling the boost pipe costs a tenth of its work (the game needs nine pipe-fulls of air for full boost; a real
    // pipe takes under one), shared between the turbos feeding it; steady or falling boost pays for all of it.
    { State st; Inputs in; in.boost_capacity = 10; in.pressure[kColdOut] = 2; in.volume[kColdOut] = 2;
      assert(working_flow(st, in, 400, 1.0 / 60) == 400);                       // no previous pressure yet
      in.pressure[kColdOut] = 2.5; assert(near(working_flow(st, in, 400, 1.0 / 60), 400 - 0.9 * 300));
      in.pressure[kColdOut] = 3; in.feeders = 2; assert(near(working_flow(st, in, 400, 1.0 / 60), 400 - 0.9 * 150));
      in.pressure[kColdOut] = 2; assert(working_flow(st, in, 400, 1.0 / 60) == 400 && working_flow(st, in, -5, 1.0 / 60) == 0); }
    // A big engine on a small turbo chokes it: back pressure goes past the engine's 4-unit limit.
    { State st; Plant pl; run(st, spec(3), pl, 70, 60, 10); assert(pl.hot_in / pl.cap > 4.0); }
    // A small engine's exhaust on a 6x6 is too little for full boost.
    { State st; Plant pl; double p = run(st, spec(6), pl, 22, 18, 20); assert(st.spool < 0.6 && p < 7.0); }
    // Boost limit (kPa, game scale) caps the boost the `boost` output reads (the docs: "caps boost at this pressure").
    { State st; st.slots[kBoostLimit] = 49; Plant pl; Spec s = spec(3); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.5, 20);
      assert(p < 6.05 && p > 4.5 && st.slots[kBoost] < 49.5 && st.slots[kBoost] > 35); }
    // Compounds: the engine's boost factor carries on past the game's 10 (2x) up to 19 (3x), and no further.
    assert(near(power_multiplier(10), 2.0) && near(power_multiplier(19), 3.0) && near(power_multiplier(40), 3.0) && near(power_multiplier(5.5), 1.5));
    // The boost limit is over the air outside, so a second stage whose inlet is already boosted makes only the rest.
    { State st; st.speed = 1; st.slots[kBoostLimit] = 49; assert(near(head_for(st, 1.0), 5.0, 0.01) && near(head_for(st, 4.0), 2.0, 0.01) && head_for(st, 9.0) == 0); }
    // The head is a pressure ratio that goes with the square of shaft speed: 9 units from outside air at full speed,
    // a quarter of that at half speed, more units from a boosted inlet, never past the engine's 19.
    { State st; st.speed = 1; assert(near(head_for(st, 1.0), kMaxHead, 0.01) && head_for(st, 5.0) > 12 && near(head_for(st, 10.0), 9.0, 1e-9));
      st.speed = 0.5; assert(near(head_for(st, 1.0), kMaxHead / 4, 0.01)); st.speed = NAN; assert(head_for(st, 1.0) == 0); }
    // A second stage heats its inlet air further; intercoolers cool towards the outside air.
    { double one = boost_air_c(20, kFullBoostRatio, kCompressorEfficiency, 0, 20);
      assert(near(one, 97.6, 0.5) && boost_air_c(one, 1.5, kCompressorEfficiency, 0, 20) > one + 40 && near(boost_air_c(one, 1.5, kCompressorEfficiency, 1, 20), 20));
      assert(near(boost_air_c(20, kFullBoostRatio, kCompressorEfficiency, 0.5, 20), (one + 20) / 2) && near(boost_air_c(NAN, 1, kCompressorEfficiency, 0, 20), 20)); }
    // Two 3x3s compounded on a big engine reach well past one turbo's 10 (three times the power needs 19); one 3x3 on
    // its own under the same draw stays at or under 10.
    { State a, b; CompoundPlant pl; double p = run_compound(a, b, spec(3), spec(3), pl, 50, 40, 20);
      assert(a.spool > 0.95 && b.spool > 0.5 && p > 13 && p < 19.5 && b.slots[kPowerMultiplier] > 2.3);
      assert(b.spool < a.spool);                        // breathing boosted air, the second stage needs less shaft speed
      assert(b.slots[kBoost] > 100);                    // the second stage's `boost` reads the total over the air outside
      State one; Plant single; double q = run(one, spec(3), single, 25, 40, 20); assert(q <= 10.0 && one.slots[kPowerMultiplier] <= 2.0); }
    // Boost outputs: over the air outside, in kPa and psi (full boost about 88 kPa, 12.8 psi).
    { State st; Plant pl; Spec s = spec(3); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.9, 20);
      assert(near(st.slots[kBoost], (p - 1) * kKpaPerUnit, 2.5) && near(st.slots[kBoostPsi], st.slots[kBoost] * 0.1450377, 1e-9) && st.slots[kBoostPsi] > 10); }
    // Antilag holds the spool off throttle (engine idling) and says so; it does nothing with the engine off.
    { Spec s = spec(3);
      auto idle = [&](bool antilag) { State st; Plant pl; run(st, s, pl, s.rated_flow, 0, 10);
          for (int i = 0; i < 300; ++i) {                  // 5 s: unloaded, the shaft only coasts down on its bearings
              Inputs in; in.pressure[kHotIn] = 1.1; in.volume[kHotIn] = 2.2; in.pressure[kHotOut] = 1; in.volume[kHotOut] = 2;
              in.pressure[kColdIn] = 1; in.volume[kColdIn] = 2; in.pressure[kColdOut] = 1; in.volume[kColdOut] = 2; in.dt = 1.0 / 60; in.antilag = antilag;
              step(st, s, in);
          }
          return st; };
      State off = idle(false), on = idle(true);
      assert(on.spool > 0.7 && off.spool < on.spool - 0.3 && on.slots[kAntilagFiring] == 1 && off.slots[kAntilagFiring] == 0);
      State dead; Plant pl; Inputs in; in.dt = 1.0 / 60; in.antilag = true; for (int i = 0; i < 4; ++i) in.pressure[i] = 1; step(dead, s, in);
      assert(!dead.antilag_firing && dead.spool == 0); }
    assert(valve_channel_index("antilag", 7) == kValveAntilag && channel_index("boost_psi", 9) == kBoostPsi);
    // Two-step: the wastegate takes a launch RPM; the limiter cuts at it and gives the fuel back 150 RPM below, so the
    // engine bounces just under it; 0 is off, and it never asks for more than the game's own fuel cut.
    assert(valve_channel_index("two_step", 8) == kValveTwoStep && json.find("\"type_f64_input\", \"name\": \"two_step\"") != std::string::npos);
    { bool cut = false;
      assert(!two_step_cut(cut, 3000, 4000) && two_step_cut(cut, 4000, 4000) && two_step_cut(cut, 3900, 4000) && !two_step_cut(cut, 3840, 4000));
      assert(!two_step_cut(cut, 3990, 4000) && two_step_cut(cut, -4100, 4000));             // either way round
      assert(!two_step_cut(cut, 9000, 0) && !cut && !two_step_cut(cut, NAN, 4000) && !two_step_cut(cut, 4000, NAN));
      assert(!two_step_cut(cut, 7900, 20000) && two_step_cut(cut, 8000, 20000)); }
    // On the limiter, antilag fires however little exhaust the engine makes (its fuel goes into the exhaust); without
    // antilag the two-step alone does not.
    { Spec s = spec(6);
      for (bool antilag : {false, true}) {
          State st; Inputs in; in.dt = 1.0 / 60; in.antilag = antilag; in.two_step = true;
          for (int i = 0; i < 4; ++i) { in.pressure[i] = 1; in.volume[i] = 2; }
          for (int i = 0; i < 180; ++i) step(st, s, in);
          assert(st.antilag_firing == antilag && (antilag ? st.spool > 0.75 : st.spool == 0));
      } }
    // Spool falls when the exhaust stops.
    { State st; Plant pl; Spec s = spec(3); run(st, s, pl, s.rated_flow, 0, 10); double was = st.spool; run(st, s, pl, 0, 0, 3); assert(st.spool < was * 0.2); }
    // Lift-off with boost trapped in the pipe: without a blow-off valve the compressor surges and the wheel slows
    // hard; with one venting, the boost does not push back and the spool only coasts down.
    {
        auto lift = [](bool bov, double spring = kBlowOffDefaultKpa) {
            State st; Plant pl; Spec s = spec(3);
            run(st, s, pl, s.rated_flow, s.rated_flow * 0.9, 20);
            double trapped = st.spool;
            Inputs in; in.dt = 1.0 / 60; in.bov_venting = bov; in.bov_spring_kpa = spring;
            in.pressure[kHotIn] = 1; in.volume[kHotIn] = 2; in.pressure[kHotOut] = 1; in.volume[kHotOut] = 2;
            in.pressure[kColdIn] = 1; in.volume[kColdIn] = 2; in.pressure[kColdOut] = 10; in.volume[kColdOut] = 20;
            bool surged = false;
            for (int i = 0; i < 30; ++i) { Step o = step(st, s, in); surged = surged || st.surging; if (bov && spring <= kBlowOffSoftSpring) assert(o.compressor_move >= 0); }
            return std::make_pair(surged, st.spool / trapped);
        };
        auto without = lift(false), with = lift(true);
        assert(without.first && !with.first && without.second < with.second - 0.1 && st_ok(with.second));
        // Stiffness: a stiff spring lets some surge through while it vents; the stiffest about as much as none.
        auto stiff = lift(true, 35), stiffest = lift(true, 50);
        assert(stiff.first && stiff.second < with.second && stiff.second > without.second && stiffest.second <= stiff.second);
        assert(blow_off_surge_through(kBlowOffDefaultKpa) == 0 && blow_off_surge_through(15) == 0 && blow_off_surge_through(50) == 1);
    }
    // Exhaust flowing back out of the hot side (engine just stopped) does not spool the turbo.
    { State st; Inputs in; in.dt = 1.0 / 60;
      in.pressure[kHotIn] = 1; in.volume[kHotIn] = 2; in.pressure[kHotOut] = 4; in.volume[kHotOut] = 8;
      in.pressure[kColdIn] = 1; in.volume[kColdIn] = 2; in.pressure[kColdOut] = 1; in.volume[kColdOut] = 2;
      for (int i = 0; i < 120; ++i) { Step o = step(st, spec(3), in); assert(o.turbine_move <= 0); }
      assert(st.spool == 0 && st.exhaust_flow == 0); }
    // Garbage input never produces NaN.
    { State st; Inputs in; for (auto& p : in.pressure) p = NAN; in.dt = NAN; Step o = step(st, spec(4), in); assert(o.turbine_move == 0 && o.compressor_move == 0);
      in.dt = 1.0 / 60; o = step(st, spec(4), in); assert(std::isfinite(o.turbine_move) && std::isfinite(o.compressor_move) && std::isfinite(st.spool)); }
    // Mesh: two housings, mirrored winding on the hot side, scaled to the N x N face, rust paint only on the hot side.
    std::vector<uint8_t> stock = fake_impeller();
    assert(build_turbo_mesh(stock, 1, "x").empty() && build_turbo_mesh(std::vector<uint8_t>(10), 3, "x").empty());
    for (int n = kMinSize; n <= kMaxSize; ++n) {
        std::vector<uint8_t> mesh = build_turbo_mesh(stock, n, "turbocharger");
        MeshLayout m; assert(parse_mesh(mesh, m) && m.vertices > 8 && m.indices > 12);   // the impeller's 2 x 4, then the mod's own pieces
        std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), mesh.data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
        uint32_t idx[12]; std::memcpy(idx, mesh.data() + m.index_bytes + 4, sizeof idx);
        assert(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[6] == 4 && idx[7] == 6 && idx[8] == 5);
        double c = (n - 1) * kCell / 2, s = n / 3.0;
        assert(near(v[0].pos[0], -0.04, 1e-6) && near(v[0].pos[1], c - 0.12 * s, 1e-5) && near(v[2].pos[2], c + 0.12 * s, 1e-5));
        assert(near(v[4].pos[0], kCell + 0.04, 1e-6) && near(v[4].pos[1], c + 0.12 * s, 1e-5) && near(v[4].normal[0], 1, 1e-6));
        assert(v[0].rgba[0] == 153 && v[0].rgba[1] == 0 && v[4].rgba[0] == 153 && v[4].rgba[1] == 38 &&
               v[7].rgba[0] == (spec(n).large_pipe ? 153 : 51));   // paint markers 1 and 2; a large turbo paints its stub ends over (flange blocks)
        // Cold outlet stub lands under the outlet's pipe end on the +z face (top cell); hot inlet stub under the inlet's on -z.
        assert(port_covers(n, kColdOut, 0, int(std::lround(v[3].pos[1] / kCell)), n - 1) && near(v[3].pos[2], (n - 0.5) * kCell, 1e-5));
        assert(port_covers(n, kHotIn, 1, int(std::lround(v[7].pos[1] / kCell)), 0) && near(v[7].pos[2], -0.5 * kCell, 1e-5));
        // Everything stays in the part's cells (openings and fittings at most a couple of millimetres proud), each oil
        // port has its brass ring under it, and every gas port that the stock opening misses gets a hole of its own,
        // centred on the pipe end.
        std::vector<MeshVertex> all(m.vertices); std::memcpy(all.data(), mesh.data() + m.vertex_bytes + 4, all.size() * sizeof(MeshVertex));
        const double edge = 0.5 * kCell + 0.0025;
        for (auto& x : all) assert(x.pos[0] >= -edge && x.pos[0] <= kCell + edge && x.pos[1] >= -edge && x.pos[1] <= (n - 1) * kCell + edge && x.pos[2] >= -edge && x.pos[2] <= (n - 1) * kCell + edge);
        auto has = [&](const uint8_t col[3], const double at[3], double r) {
            for (auto& x : all)
                if (x.rgba[0] == col[0] && x.rgba[1] == col[1] && x.rgba[2] == col[2] && std::hypot(x.pos[0] - at[0], x.pos[1] - at[1], x.pos[2] - at[2]) < r) return true;
            return false;
        };
        for (bool in : {true, false}) { Port o = oil_port(n, in); const double at[3] = {o.x * kCell, -0.5 * kCell, o.z * kCell}; assert(has(kOilBrass, at, 0.02)); }
        double at[3];
        for (Node k : {kColdIn, kColdOut, kHotIn, kHotOut}) {
            port_face_centre(n, k, at);
            bool own = n == 2 ? (k == kColdIn || k == kHotOut) : spec(n).large_pipe && (k == kColdOut || k == kHotIn);
            if (own) assert(has(kValveHole, at, 0.003));   // the hole's centre vertex
        }
    }
    // The real impeller (when the game files are here): the 5x5's eye moves onto its off-centre pipe; the 3x3's stays.
    {
        std::FILE* f = std::fopen("/mnt/user-data/uploads/Anymaker/rom/meshes/components/turbo_a.mesh", "rb");
        if (f) {
            std::vector<uint8_t> real;
            for (int ch; (ch = std::fgetc(f)) != EOF;) real.push_back(uint8_t(ch));
            std::fclose(f);
            for (int n = kMinSize; n <= kMaxSize; ++n) {
                std::vector<uint8_t> mesh = build_turbo_mesh(real, n, "t");
                MeshLayout m; assert(parse_mesh(mesh, m));
                std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), mesh.data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
                double eye[3]; port_face_centre(n, kColdIn, eye);
                double sum[2] = {0, 0}; int count = 0;   // the eye's black hole, on the cold side
                for (size_t i = 0; i < v.size() / 2 && i < 584; ++i)
                    if (v[i].rgba[0] == 0 && v[i].rgba[1] == 0 && v[i].rgba[2] == 0 && v[i].pos[0] < -0.02f && v[i].pos[2] < (n - 1) * kCell - 0.01) { sum[0] += v[i].pos[1]; sum[1] += v[i].pos[2]; ++count; }
                if (n == 2) assert(count == 0);       // filled in: the mod's own opening is at the pipe
                else assert(count > 0 && near(sum[0] / count, eye[1], 0.002) && near(sum[1] / count, eye[2], 0.002));
            }
        }
    }
    // Valve meshes, every style: readable, inside their cells (the cap may rise a little), with both paint markers,
    // and every face wound clockwise seen from outside like the stock meshes (the game culls the other side).
    for (int style = 0; style < kValveStyleCount; ++style)
    for (int large = 0; large < 2; ++large)
        for (int wg = 0; wg < 2; ++wg) {
            std::vector<uint8_t> mesh = wg ? build_wastegate_mesh(stock, large, "v", style) : build_blow_off_mesh(stock, large, "v", style);
            MeshLayout m; assert(parse_mesh(mesh, m) && m.vertices > 100);
            std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), mesh.data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
            double lo = -kCell / 2 - 0.003, hi = (large ? 1.5 : 0.5) * kCell + 0.003;
            bool paint0 = false, paint1 = false;
            for (auto& p : v) {
                for (int j = 0; j < 3; ++j) assert(p.pos[j] >= lo && p.pos[j] <= hi + (j == 1 ? 0.02 : 0.0));
                paint0 = paint0 || (p.rgba[0] == 153 && p.rgba[1] == 0); paint1 = paint1 || (p.rgba[0] == 153 && p.rgba[1] == 38);
            }
            assert(paint0 && paint1);
            std::vector<uint32_t> idx(m.indices); std::memcpy(idx.data(), mesh.data() + m.index_bytes + 4, idx.size() * 4);
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                const MeshVertex &a = v[idx[t]], &b = v[idx[t + 1]], &c = v[idx[t + 2]];
                double e1[3], e2[3], dot = 0;
                for (int j = 0; j < 3; ++j) { e1[j] = b.pos[j] - a.pos[j]; e2[j] = c.pos[j] - a.pos[j]; }
                double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (int j = 0; j < 3; ++j) dot += n[j] * (a.normal[j] + b.normal[j] + c.normal[j]);
                assert(dot <= 1e-12);
            }
        }
    // Sounds: the replicated factor survives the one-byte trip, and every voice renders finite, audible audio.
    {
        auto byte = [](double f) { return std::round(f * 255.0) / 255.0; };
        for (int i = 0; i <= 20; ++i) {
            double spool = i / 20.0, got; bool surging;
            decode_turbo_factor(byte(encode_turbo_factor(spool, false)), got, surging);
            assert(!surging && near(got, spool, 0.01));
            decode_turbo_factor(byte(encode_turbo_factor(spool, true)), got, surging);
            assert(surging && near(got, spool, 0.01));
            for (double nudge : {-1.0, 0.0, 1.0}) {               // blown reads as blown whichever way the byte rounds;
                bool blown;                                     // full spool and the weakest surge never do
                decode_turbo_factor(byte(encode_turbo_factor(spool, i % 2 == 0, true)) + nudge / 255.0, got, surging, blown);
                assert(blown && !surging && got == 0);
                decode_turbo_factor(byte(encode_turbo_factor(1.0, false)) + nudge / 255.0, got, surging, blown);
                assert(!blown && !surging && got > 0.98);
                decode_turbo_factor(byte(encode_turbo_factor(0.0, true)) + nudge / 255.0, got, surging, blown);
                assert(!blown && surging && got < 0.01);
            }
            for (int k = 0; k < kBlowOffSoundCount; ++k)
                for (int z = kMinSize; z <= kMaxSize; ++z)
                    for (double nudge : {-1.0, 0.0, 1.0}) {          // the game may round the byte either way
                        bool open; double level; int sound = -1, size = -1;
                        decode_blow_off_factor(byte(encode_blow_off_factor(true, spool, k, z)) + nudge / 255.0, open, level, sound, size);
                        const int voiced = std::min(z, 5);                // the byte carries sizes 2-5; the 6x6 voices as the 5x5
                        assert(open && sound == k && size == voiced && near(level, spool, 0.07 + 0.12 * std::abs(nudge)));
                        decode_blow_off_factor(byte(encode_blow_off_factor(false, spool, k, z)), open, level, sound, size);
                        assert(!open && level == 0 && sound == k && size == voiced);   // shut keeps the last sound for the tail
                    }
        }
        auto peak = [](Synth& s, double level, bool flag, int samples, bool blown = false) {
            std::vector<int16_t> pcm(static_cast<size_t>(samples));
            render(s, level, flag, pcm.data(), samples, blown);
            int p = 0; for (int16_t x : pcm) p = std::max(p, std::abs(int(x)));
            return p;
        };
        Synth idle; assert(peak(idle, 0, false, 4800) == 0);
        Synth whistle; whistle.size = 3; peak(whistle, 1, false, 24000); int w = peak(whistle, 1, false, 4800);
        assert(w > 3000 && w <= 32767);
        Synth surge; surge.size = 3; peak(surge, 0.6, false, 24000); int calm = peak(surge, 0.6, false, 4800), stu = peak(surge, 0.6, true, 9600);
        assert(stu > calm);
        for (int size = kMinSize; size <= kMaxSize; ++size) {
            Synth t; t.size = size; peak(t, 1, false, 24000); assert(peak(t, 1, false, 4800) > 3000);
            assert(peak(t, 0.7, true, 9600) > 3000);
        }
        for (int k = 0; k < kBlowOffSoundCount; ++k) {
          for (int z = kMinSize; z <= kMaxSize; ++z) {
            Synth bov; bov.voice = Voice::kBlowOff; bov.sound = k; bov.size = z; assert(peak(bov, 0, false, 4800) == 0);
            int whoosh = peak(bov, 1, true, 4800); assert(whoosh > 3000);
            peak(bov, 0, false, 5 * 48000); assert(peak(bov, 0, false, 4800) < 50);   // the tail dies away
            // Held open with next to no air going out (off throttle, the boost all but gone): the burst, then quiet.
            Synth held; held.voice = Voice::kBlowOff; held.sound = k; held.size = z;
            assert(peak(held, 0, true, 4800) > 3000);
            peak(held, 0, true, 5 * 48000); assert(peak(held, 0, true, 4800) < 50);
          }
        }
        for (int z = kMinSize; z <= kMaxSize; ++z) {         // antilag bangs: silent until it fires, loud bangs, dies away
            Synth al; al.voice = Voice::kAntilag; al.size = z; assert(peak(al, 0, false, 4800) == 0);
            assert(peak(al, 0.5, true, 48000) > 8000);
            peak(al, 0, false, 3 * 48000); assert(peak(al, 0, false, 4800) < 50);
        }
        for (int z = kMinSize; z <= kMaxSize; ++z) {         // blowing up: one loud bang as it goes, then it dies away for good
            Synth t; t.size = z;
            for (int k = 0; k < 15; ++k) peak(t, 1, false, 4800);   // heard spooled up for 1.5 s, in game-sized chunks
            assert(peak(t, 0, false, 9600, true) > 20000);
            for (int k = 0; k < 40; ++k) peak(t, 0, false, 4800, true);
            assert(peak(t, 0, false, 4800, true) < 50);
            Synth loaded; loaded.size = z;                      // already blown when first heard: silent
            for (int k = 0; k < 30; ++k) assert(peak(loaded, 0, false, 4800, true) == 0);
            Synth early; early.size = z;                        // blowing in the first second it is heard: no bang (it may be the
            peak(early, 0, false, 4800); assert(peak(early, 0, false, 9600, true) < 50);   // replicated value arriving late)
        }
        Synth wild; peak(wild, std::nan(""), true, 4800); assert(peak(wild, 0.5, false, 4800) <= 32767);
    }
    // Upkeep: the four setups wear in the order (and roughly the times) promised; hours to half condition at full boost.
    {
        auto hours_to_half = [](bool oil, bool filter) {
            UpkeepState st; UpkeepInputs in; in.spool = 1; in.exhaust_fraction = 1; in.oil_lps = oil ? 0.6 : 0; in.oil_c = 90; in.filter = filter; in.dt = 0.5;
            for (int s = 0; s < 100 * 7200; ++s) { upkeep(st, in); if (st.condition <= 0.5) return s / 7200.0; }
            return 1e9;
        };
        double both = hours_to_half(true, true), filter_only = hours_to_half(false, true), oil_only = hours_to_half(true, false), neither = hours_to_half(false, false);
        assert(both > 12 && both < 30);
        assert(oil_only < both / 2.5 && filter_only < oil_only && neither < filter_only / 3 && neither > 0.1);
        // Temperatures: oil keeps it under the wear threshold, dry it runs hotter than the exhaust heats it.
        UpkeepState a, b; UpkeepInputs in; in.spool = 1; in.exhaust_fraction = 1; in.oil_c = 90; in.dt = 0.5;
        in.oil_lps = 0.6; for (int s = 0; s < 3600; ++s) upkeep(a, in);
        in.oil_lps = 0; for (int s = 0; s < 3600; ++s) upkeep(b, in);
        assert(a.temperature < kTurboHotC && b.temperature > a.temperature + 50 && b.temperature <= kTurboMaxC);
        // Idle and off: no wear; it cools down.
        UpkeepState c; c.temperature = 300; UpkeepInputs off; off.dt = 1;
        for (int s = 0; s < 3600; ++s) upkeep(c, off);
        assert(c.condition == 1 && c.temperature < 40);
        // Polluted oil wears more; NaNs do no harm.
        assert(wear_multiplier(true, true, 1) > wear_multiplier(true, true, 0) * 4);
        UpkeepState n; UpkeepInputs bad; bad.spool = NAN; bad.oil_lps = NAN; bad.oil_c = NAN; bad.dt = NAN; upkeep(n, bad);
        assert(std::isfinite(n.temperature) && n.condition == 1);
        assert(condition_head_factor(1) == 1 && condition_head_factor(0.5) == 1 && near(condition_head_factor(0.25), 0.8) && condition_head_factor(0) == 0);
        assert(bearing_whine(0.1) && !bearing_whine(0.5) && !bearing_whine(0));
        assert(filter_head_factor(false, 0) == 1 && near(filter_head_factor(true, 0), 0.85) && filter_head_factor(true, 1) == 1);
    }
    // Hot boost air: about +78 C at full boost from outside air (a pressure ratio of 1.87 at 74 % efficiency), hotter
    // where the compressor runs off its best flow; intercoolers take it back out (bigger and slower is better), and
    // the engine's boost factor follows the air's density; naturally aspirated engines are untouched.
    {
        assert(near(compressor_heat(20, kFullBoostRatio, kCompressorEfficiency), 77.6, 0.5) && compressor_heat(20, 1, kCompressorEfficiency) == 0 &&
               compressor_heat(20, 1.4, kCompressorEfficiency) < 70 && compressor_heat(20, kFullBoostRatio, 0.5) > 100 && std::isfinite(compressor_heat(NAN, NAN, NAN)));
        assert(intercooler_effectiveness(0, 10) == 0 && intercooler_effectiveness(6, 10) > 0.8 && intercooler_effectiveness(12, 10) > intercooler_effectiveness(6, 10) &&
               intercooler_effectiveness(6, 80) < intercooler_effectiveness(6, 10));
        assert(boost_factor_for_air(1.0, 140) == 1.0 && near(boost_factor_for_air(2.0, 30), 2.0) && boost_factor_for_air(2.0, 140) < 1.8 && boost_factor_for_air(2.0, 0) > 2.0);
        assert(knock_damage(55, 2, 1) == 0 && knock_damage(140, 1.1, 1) == 0 && knock_damage(140, 2, 1) > 0 && 1 / knock_damage(98, 2, 1) > 480 && 1 / knock_damage(98, 2, 1) < 560 && std::isfinite(boost_factor_for_air(2.0, NAN)));
    }
    // Wear file: round trip, junk ignored, values clamped.
    {
        WearMap w{{WearKey{'t', 12, 34}, 0.5}, {WearKey{'f', 12, 40}, 0.25}, {WearKey{'t', -1, 7}, 1.0}};
        WearMap r = decode_wear(encode_wear(w));
        assert(r.size() == 3 && near(r[(WearKey{'t', 12, 34})], 0.5) && near(r[(WearKey{'f', 12, 40})], 0.25) && near(r[(WearKey{'t', -1, 7})], 1.0));
        WearMap j = decode_wear("{\"version\": 1, \"wear\": {\"t1:2\": 7, \"x3:4\": 0.5, \"t5\": 0.5, \"f6:7\": -1, \"t8:9\": nope}}");
        assert(j.size() == 2 && j[(WearKey{'t', 1, 2})] == 1 && j[(WearKey{'f', 6, 7})] == 0);
        assert(decode_wear("").empty() && decode_wear("{\"t1:2\"").empty());
    }
    // Air filter and intercooler meshes: readable, both paint markers, inside their cells. An intercooler tile fills
    // its cells edge to edge along x and y, so laid out the game's way (tile 0 at cell 0, tile 1 a cell on, tile 2 on
    // the last cell) the mesh covers the part from its first cell, -1, to its last.
    for (int large = 0; large < 2; ++large) {
        std::vector<std::vector<uint8_t>> meshes{build_air_filter_mesh(stock, large, "f")};
        for (int style = 0; style < kIntercoolerStyleCount && !large; ++style)   // both looks: tiles 0, 1, 2 along x and y
            for (int t = 0; t < 9; ++t) meshes.push_back(build_intercooler_tile(stock, t % 3, t / 3, "i", style));
        for (size_t k = 0; k < meshes.size(); ++k) {
            MeshLayout m; assert(parse_mesh(meshes[k], m) && m.vertices > 50);
            std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), meshes[k].data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
            bool paint0 = false, paint1 = false;
            double lo[3], hi[3], min_x = 1, max_x = -1, min_y = 1, max_y = -1;
            if (k == 0) { for (int j = 0; j < 3; ++j) { lo[j] = -kCell / 2; hi[j] = (air_filter_cells(large) - 0.5) * kCell; } }
            else {                                           // one cell, and the first x tile also cell -1
                lo[0] = lo[1] = lo[2] = -kCell / 2; hi[0] = hi[1] = hi[2] = kCell / 2;
                if ((k - 1) % 9 % 3 == 0) lo[0] = -1.5 * kCell;
            }
            for (auto& p : v) {
                for (int j = 0; j < 3; ++j) assert(p.pos[j] >= lo[j] - 0.003 && p.pos[j] <= hi[j] + 0.003);
                paint0 = paint0 || (p.rgba[0] == 153 && p.rgba[1] == 0); paint1 = paint1 || (p.rgba[0] == 153 && p.rgba[1] == 38);
                min_x = std::min(min_x, double(p.pos[0])); max_x = std::max(max_x, double(p.pos[0]));
                min_y = std::min(min_y, double(p.pos[1])); max_y = std::max(max_y, double(p.pos[1]));
            }
            int tx = int(k - 1) % 9 % 3, ty = int(k - 1) % 9 / 3;
            assert(k == 0 || (std::abs(min_x - lo[0]) < 0.001 && std::abs(max_x - hi[0]) < 0.001 && std::abs(min_y - lo[1]) < 0.003 && std::abs(max_y - hi[1]) < 0.003));
            assert(k == 0 || paint0 == (tx != 1));               // tanks on the end tiles only, caps on the corners only
            assert(k == 0 ? paint0 && paint1 : paint1 == (tx != 1 && ty != 1));
        }
    }
    // Superchargers: two new parts on the impeller class, in place of the 2x2 and 4x4 turbos (gone entirely, like the
    // 5x5, which grew to the 6x6).
    {
        bool all[5] = {true, true, true, true, true}, two[2] = {true, true}, one = true;
        std::string full = definitions_json(all, two, two, two, &one, two);
        auto entry = [&](const char* id) {
            size_t at = full.find(std::string("\"id\": \"") + id + "\"");
            assert(at != std::string::npos);
            return full.substr(at, full.find("\n    }", at) - at);
        };
        auto count_in = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = 0; (at = text.find(what, at)) != std::string::npos; ++at) ++n;
            return n;
        };
        std::string small = entry("supercharger_small"), large = entry("supercharger_large");
        assert(small.find("\"name\": \"Supercharger (Small)\"") != std::string::npos && large.find("\"name\": \"Supercharger (Large)\"") != std::string::npos);
        assert(small.find("{\"id\": \"torque\", \"pos\": [0, 0, 1], \"dir\": 5, \"type\": \"torque\"}") != std::string::npos);   // back face, bottom corner
        assert(large.find("{\"id\": \"torque\", \"pos\": [0, 0, 3], \"dir\": 5, \"type\": \"torque\"}") != std::string::npos);
        assert(small.find("{\"pos\": [0, 0, 0], \"direction\": 4, \"type\": \"gas\", \"size\": 1}") != std::string::npos);     // large intake, centred
        assert(small.find("{\"pos\": [1, 1, 0], \"direction\": 3, \"type\": \"gas\"}") != std::string::npos);                  // small outlet on top
        assert(large.find("{\"pos\": [1, 1, 0], \"direction\": 4, \"type\": \"gas\", \"size\": 1}") != std::string::npos);
        assert(large.find("{\"pos\": [2, 3, 0], \"direction\": 3, \"type\": \"gas\", \"size\": 1}") != std::string::npos);
        assert(count_in(small, "\"type\": \"gas\"") == 2 && count_in(large, "\"type\": \"gas\"") == 2 && small.find("liquid") == std::string::npos);
        assert(count_in(small, "\"type\": \"port\"") == 2 * 2 * 2 && count_in(large, "\"type\": \"port\"") == 2 * 4 * 4);   // data ports on both sides
        assert(small.find("turbo_supercharger_small.mesh") != std::string::npos && large.find("\"bounds_max\": [3, 3, 3]") != std::string::npos);
        assert(small.find("\"tech_tier\": 3") != std::string::npos && small.find("\"name\": \"drive_power\"") != std::string::npos);
        for (int n : {2, 4, 5}) assert(full.find("\"id\": \"" + definition_id(n) + "\"") == std::string::npos);
        for (int n : kTurboSizes) assert(entry(definition_id(n).c_str()).find("\"tech_tier\": 3") != std::string::npos);
        assert(definitions_json(all, two, two, two, &one).find("\"id\": \"supercharger_small\", ") == std::string::npos);
        assert(definitions_json(all).find("meshes/components/turbo_a.mesh") != std::string::npos);   // no meshes: the stock look
        assert(blower_from_id("supercharger_small", 18) == kBlowerSmall && blower_from_id("supercharger_large", 18) == kBlowerLarge &&
               blower_from_id("supercharger", 12) == kNoBlower && valve_from_id("supercharger_small", 18) == kNoValve && size_from_id("supercharger_small", 18) == 0);
    }
    {   // the model: head with the square of the pulley's speed, no lag, and the belt pays for compressing the air
        assert(blower_spool(0) == 0 && near(blower_spool(kBlowerFullBoostRps), 1) && near(blower_spool(-kBlowerFullBoostRps / 2), 0.25) && blower_spool(1e9) == 1);
        assert(near(blower_spool(3500 / 60.0 * kBeltMedium), 0.25));                 // medium Engine Wheel at 3,500 RPM
        assert(blower_spool(4100 / 60.0 * kBeltLarge) > 0.99);                       // the large wheel overdrives it
        assert(near(blower_spool(8000 / 60.0 * kBeltSmall), 0.5436, 1e-3));         // the small one: about half at the fuel cut
        for (bool big : {false, true}) {
            Spec s = blower_spec(big);
            State st; Plant pl; double joules = 0;
            double p = run_blower(st, s, pl, kBlowerFullBoostRps, s.rated_flow * 0.8, 10, &joules);
            assert(p > 8.0 && p <= 10.0 && st.slots[kPowerMultiplier] > 1.8 && st.slots[kPowerMultiplier] <= 2.0);
            double watts = joules / 10;                                               // the air's compression work, through the gears
            double ideal = compressor_watts(s.rated_flow * 0.8, 1, p, kAmbientC, compressor_efficiency(s, s.rated_flow * 0.8, 1)) / kBlowerDriveEfficiency;
            assert(watts > 0.85 * ideal && watts < 1.15 * ideal && near(st.slots[kDrivePower], ideal / 1000, 0.15 * ideal / 1000));
            assert(st.slots[kDrivePower] > 0 && near(st.slots[kCondition], 1) && st.slots[kRpm] > 0.99 * s.max_rpm && !st.surging);
            State st2; Plant pl2; run_blower(st2, s, pl2, kBlowerFullBoostRps, s.rated_flow * 0.8, 0.2);
            assert(st2.spool > 0.95);                                                 // no lag
            State idle; Plant pl3; double j0 = 0; double q = run_blower(idle, s, pl3, 0, 4, 5, &j0);
            assert(idle.spool == 0 && j0 == 0 && q < 1.0);                           // no belt: no boost, no drag
            // Throttle off at speed: boost builds to the head; then the engine drops and the boost surges back out,
            // unless a blow-off valve takes it.
            State lift; Plant pl4; run_blower(lift, s, pl4, kBlowerFullBoostRps, 0, 5);
            assert(pl4.cold_out / pl4.cap > 9.0);
            Plant pl5 = pl4; State with = lift;
            bool surged = false;
            for (int k = 0; k < 30 && !surged; ++k) { run_blower(lift, s, pl4, kBlowerFullBoostRps * 0.4, 0, 1.0 / 60); surged = lift.surging; }
            assert(surged);
            for (int k = 0; k < 30; ++k) { run_blower(with, s, pl5, kBlowerFullBoostRps * 0.4, 0, 1.0 / 60, nullptr, true); assert(!with.surging); }
            State capped; capped.slots[kBoostLimit] = 49; Plant pl6;
            double c = run_blower(capped, s, pl6, kBlowerFullBoostRps, s.rated_flow * 0.5, 10);
            assert(c > 5.5 && c < 6.3);                                               // boost_limit caps it like a turbo
        }
        assert(blower_spec(true).rated_flow > blower_spec(false).rated_flow && blower_spec(false).large_pipe == false && blower_spec(true).large_pipe);
    }
    {   // the Dyno's names for the boost setup
        auto name = [](int t, int c, bool compound = false) { BoostSetup b; b.turbos = t; b.superchargers = c; b.compound = compound; return boost_setup_name(b); };
        assert(name(0, 0) == "Naturally aspirated" && name(1, 0) == "Single turbo" && name(2, 0) == "Twin turbo" && name(3, 0) == "Triple turbo" &&
               name(4, 0) == "Quad turbo" && name(6, 0) == "6 turbos");
        assert(name(2, 0, true) == "Compound turbo" && name(3, 0, true) == "Compound turbo: 3 turbos" && name(1, 0, true) == "Single turbo");
        assert(name(0, 1) == "Supercharged" && name(0, 2) == "Twin supercharged" && name(0, 5) == "5 superchargers");
        assert(name(1, 1) == "Twincharged" && name(2, 1) == "Twincharged: 2 turbos, 1 supercharger" && name(1, 2, true) == "Twincharged: 1 turbo, 2 superchargers");
    }
    {   // the gear whine: follows the pulley, louder and higher with speed; the large one sings lower
        auto loudness = [](bool big, double rps) {
            Synth sy; sy.voice = Voice::kSupercharger; sy.size = big ? 4 : 2;
            std::vector<int16_t> pcm(kSampleRate / 2);
            render(sy, blower_sound_level(rps), false, pcm.data(), int(pcm.size()));
            render(sy, blower_sound_level(rps), false, pcm.data(), int(pcm.size()));
            double sum = 0;
            for (int16_t v : pcm) sum += double(v) * v;
            return std::make_pair(std::sqrt(sum / pcm.size()), sy.gear_pitch);
        };
        auto idle = loudness(false, 800 / 60.0 * kBeltMedium), high = loudness(false, 7000 / 60.0 * kBeltMedium), big = loudness(true, 7000 / 60.0 * kBeltMedium);
        assert(idle.first > 50 && high.first > 2 * idle.first && high.second > 3 * idle.second && big.second < high.second && big.first > 50);
        // Silent at rest, and silent again moments after the pulley stops (Nate heard a faint whine from parked ones).
        assert(loudness(false, 0).first < 1 && loudness(true, 0).first < 1);
        for (int big2 = 0; big2 < 2; ++big2) {
            Synth sy; sy.voice = Voice::kSupercharger; sy.size = big2 ? 4 : 2;
            std::vector<int16_t> pcm(kSampleRate / 4);
            render(sy, blower_sound_level(5000 / 60.0 * kBeltMedium), false, pcm.data(), int(pcm.size()));
            render(sy, 0, false, pcm.data(), int(pcm.size()));
            render(sy, 0, false, pcm.data(), int(pcm.size()));
            render(sy, 0, false, pcm.data(), int(pcm.size()));
            int peak = 0;
            for (int16_t v : pcm) peak = std::max(peak, std::abs(int(v)));
            assert(peak <= 1);
        }
        // Smooth, not steppy (Nate): the level arrives one byte at a time, a step every 60 ms as the engine revs; the
        // pitch slides between steps instead of jumping, and still keeps up with the engine.
        for (int big2 = 0; big2 < 2; ++big2) {
            Synth sy; sy.voice = Voice::kSupercharger; sy.size = big2 ? 4 : 2;
            std::vector<int16_t> pcm(960);
            double byte = 40, last = 0, most = 0, total = 0;
            for (int f = 0; f < 120; ++f) {
                if (f % 3 == 0) byte += 1;
                double level = 0; bool surging = false, blown = false;
                decode_turbo_factor(byte / 255.0, level, surging, blown);
                render(sy, level, false, pcm.data(), int(pcm.size()));
                if (f >= 30) { double d = sy.gear_pitch - last; most = std::max(most, d); total += d; }
                last = sy.gear_pitch;
            }
            double mean = total / 90;
            assert(mean > 0 && most < 1.6 * mean);
            double level = 0; bool surging = false, blown = false;
            decode_turbo_factor(byte / 255.0, level, surging, blown);
            BlowerVoice bv = blower_voice(big2 != 0);
            assert(std::abs(sy.gear_pitch - (bv.gear_base + bv.gear_span * level)) < 3 * bv.gear_span / 124.0);
        }
        assert(near(blower_sound_level(kBlowerSoundRps * 2), 1) && near(blower_sound_level(-kBlowerSoundRps / 2), 0.5));
        double spool = 0; bool surging = false, blown = true;
        decode_turbo_factor(encode_turbo_factor(blower_sound_level(4000 / 60.0 * kBeltMedium), false), spool, surging, blown);
        assert(near(spool, blower_sound_level(4000 / 60.0 * kBeltMedium), 0.01) && !surging && !blown);
    }
    // Supercharger meshes: readable, both paint markers, inside the part's cells, the pulley boss centred on the torque
    // spot's cell on the back face.
    for (int large = 0; large < 2; ++large) {
        std::vector<uint8_t> mesh = build_supercharger_mesh(stock, large, "s");
        MeshLayout m; assert(parse_mesh(mesh, m) && m.vertices > 200);
        std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), mesh.data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
        double lo = -kCell / 2, hi = (blower_cells(large) - 0.5) * kCell;
        bool paint0 = false, paint1 = false, boss = false;
        for (auto& p : v) {
            for (int j = 0; j < 3; ++j) assert(p.pos[j] >= lo - 0.003 && p.pos[j] <= hi + 0.003);
            paint0 = paint0 || (p.rgba[0] == 153 && p.rgba[1] == 0); paint1 = paint1 || (p.rgba[0] == 153 && p.rgba[1] == 38);
            boss = boss || (std::abs(p.pos[0]) < 0.02 && std::abs(p.pos[1]) < 0.02 && p.pos[2] > hi - 0.002);
        }
        assert(paint0 && paint1 && boss);
    }
    return 0;
}
