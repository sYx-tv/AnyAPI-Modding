#include "../turbo_logic.h"
#include "../turbo_mesh.h"
#include "../turbo_upkeep_mesh.h"
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
        in.dt = dt;
        Step o = step(st, s, in);
        pl.hot_in -= o.turbine_move;
        pl.cold_out += o.compressor_move;
    }
    return pl.cold_out / pl.cap;
}

int main() {
    // Ids and sizes.
    for (int n = kMinSize; n <= kMaxSize; ++n) { std::string id = definition_id(n); assert(size_from_id(id.c_str(), id.size()) == n); }
    assert(size_from_id("turbo", 5) == 0 && size_from_id("turbocharger_6", 14) == 0 && size_from_id("turbocharger_1", 14) == 0);
    // Specs grow with size: more flow, more lag.
    for (int n = kMinSize; n < kMaxSize; ++n) {
        Spec a = spec(n), b = spec(n + 1);
        assert(b.rated_flow > a.rated_flow && b.spool_up_s > a.spool_up_s && b.compressor_coeff > a.compressor_coeff && b.max_rpm < a.max_rpm);
    }
    assert(!spec(3).large_pipe && spec(4).large_pipe);
    // Channels.
    for (int i = 0; i < kChannelCount; ++i) assert(channel_index(kChannels[i].name, std::strlen(kChannels[i].name)) == i && kChannels[i].input == (i == kFirstInput));
    assert(channel_index("pressure", 8) < 0);
    // Ports: inside the part, axial ones on the front and back faces.
    for (int n = kMinSize; n <= kMaxSize; ++n)
        for (int k = 0; k < kNodeCount; ++k) { Port p = port(n, Node(k)); assert(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y < n && p.z >= 0 && p.z < n); }
    assert(port(3, kColdIn).dir == 0 && port(3, kHotOut).dir == 1 && port(3, kColdIn).y == 1 && port(4, kColdOut).y == 3 && port(5, kHotIn).z == 0);
    // Definitions file: four entries, four gas nodes each, the stock class, balanced braces.
    bool built[4] = {true, true, false, true}, bov_built[2] = {false, true}, wg_built[2] = {true, false};
    std::string json = definitions_json(built, bov_built, wg_built);
    size_t count = 0;
    for (size_t at = 0; (at = json.find("\"class\": \"gas_pump_centrifugal\"", at)) != std::string::npos; ++at) ++count;
    assert(count == 8);                                    // four turbos, two of each valve
    assert(json.find("\"class\": \"gas_tank\"") != std::string::npos);   // the intercooler: gas ports snap onto it
    assert(json.find("\"class\": \"gas_vent\"") != std::string::npos && json.find("\"class\": \"gas_vent_b\"") != std::string::npos);
    assert(json.find("\"id\": \"oil_in\"") != std::string::npos && json.find("\"type\": \"liquid\"") != std::string::npos);
    assert(json.find("radiator_a_0_0_0.mesh") != std::string::npos && json.find("\"mode_y\": \"tile\"") != std::string::npos);
    {   // with every mesh built the parts use theirs; the intercooler points at its first tile
        bool all[4] = {true, true, true, true}, two[2] = {true, true}, one = true;
        std::string full = definitions_json(all, two, two, two, &one);
        assert(full.find("turbo_turbo_air_filter_large.mesh") != std::string::npos && full.find("turbo_turbo_intercooler_0_0_0.mesh") != std::string::npos && full.find("\"mode_x\": \"tile\"") != std::string::npos &&
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
    assert(json.find("turbo_turbocharger_2.mesh") != std::string::npos && json.find("turbo_turbocharger_4.mesh") == std::string::npos && json.find("turbo_a.mesh") != std::string::npos);
    assert(json.find("\"name\": \"Turbo 5x5\"") != std::string::npos && json.find("\"type_f64_input\", \"name\": \"boost_limit\"") != std::string::npos);
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
           valve_from_id("turbo_wastegate_large", 21) == kWastegateLarge && valve_from_id("turbo_intercooler", 17) == kIntercooler && valve_from_id("turbo_air_filter_large", 22) == kAirFilterLarge);
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
    // Rated exhaust flow spools to full boost and back pressure stays under the 4 units that cut engine power.
    { State st; Plant pl; Spec s = spec(3); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.9, 20);
      assert(st.spool > 0.95 && p > 8.5 && pl.hot_in / pl.cap < 4.0); assert(near(st.slots[kPowerMultiplier], power_multiplier(p), 0.05)); }
    // Lag: after one second a 5x5 has built less spool than a 2x2 at the same relative flow.
    { State a, b; Plant pa, pb; run(a, spec(2), pa, spec(2).rated_flow, 0, 1.0); run(b, spec(5), pb, spec(5).rated_flow, 0, 1.0); assert(a.spool > b.spool + 0.15); }
    // A big engine on a small turbo chokes it: back pressure goes past the engine's 4-unit limit.
    { State st; Plant pl; run(st, spec(2), pl, 40, 30, 10); assert(pl.hot_in / pl.cap > 4.0); }
    // The same flow on a 5x5 is too little for full boost.
    { State st; Plant pl; run(st, spec(5), pl, 25, 20, 20); assert(st.spool < 0.4); }
    // Boost limit (kPa, game scale) caps the intake pressure.
    { State st; st.slots[kBoostLimit] = 49; Plant pl; Spec s = spec(3); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.5, 20); assert(p < 5.05 && p > 3.5); }
    // Boost outputs: over the air outside, in kPa and psi (full boost about 88 kPa, 12.8 psi).
    { State st; Plant pl; Spec s = spec(3); double p = run(st, s, pl, s.rated_flow, s.rated_flow * 0.9, 20);
      assert(near(st.slots[kBoost], (p - 1) * kKpaPerUnit, 2.5) && near(st.slots[kBoostPsi], st.slots[kBoost] * 0.1450377, 1e-9) && st.slots[kBoostPsi] > 10); }
    // Antilag holds the spool off throttle (engine idling) and says so; it does nothing with the engine off.
    { Spec s = spec(3);
      auto idle = [&](bool antilag) { State st; Plant pl; run(st, s, pl, s.rated_flow, 0, 10);
          for (int i = 0; i < 180; ++i) {
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
        MeshLayout m; assert(parse_mesh(mesh, m) && m.vertices == 8 && m.indices == 12);
        std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), mesh.data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
        uint32_t idx[12]; std::memcpy(idx, mesh.data() + m.index_bytes + 4, sizeof idx);
        assert(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[6] == 4 && idx[7] == 6 && idx[8] == 5);
        double c = (n - 1) * kCell / 2, s = n / 3.0;
        assert(near(v[0].pos[0], -0.04, 1e-6) && near(v[0].pos[1], c - 0.12 * s, 1e-5) && near(v[2].pos[2], c + 0.12 * s, 1e-5));
        assert(near(v[4].pos[0], kCell + 0.04, 1e-6) && near(v[4].pos[1], c + 0.12 * s, 1e-5) && near(v[4].normal[0], 1, 1e-6));
        assert(v[0].rgba[0] == 153 && v[0].rgba[1] == 0 && v[4].rgba[0] == 153 && v[4].rgba[1] == 38 && v[7].rgba[0] == 51);   // paint markers 1 and 2
        // Cold outlet stub lands in the top cell on the +z face; hot inlet stub in the bottom cell on -z.
        assert(int(std::lround(v[3].pos[1] / kCell)) == port(n, kColdOut).y && near(v[3].pos[2], (n - 0.5) * kCell, 1e-5));
        assert(int(std::lround(v[7].pos[1] / kCell)) == port(n, kHotIn).y && near(v[7].pos[2], -0.5 * kCell, 1e-5));
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
            for (int k = 0; k < kBlowOffSoundCount; ++k)
                for (int z = kMinSize; z <= kMaxSize; ++z)
                    for (double nudge : {-1.0, 0.0, 1.0}) {          // the game may round the byte either way
                        bool open; double level; int sound = -1, size = -1;
                        decode_blow_off_factor(byte(encode_blow_off_factor(true, spool, k, z)) + nudge / 255.0, open, level, sound, size);
                        assert(open && sound == k && size == z && near(level, spool, 0.07 + 0.12 * std::abs(nudge)));
                        decode_blow_off_factor(byte(encode_blow_off_factor(false, spool, k, z)), open, level, sound, size);
                        assert(!open && level == 0 && sound == k && size == z);   // shut keeps the last sound for the tail
                    }
        }
        auto peak = [](Synth& s, double level, bool flag, int samples) {
            std::vector<int16_t> pcm(static_cast<size_t>(samples));
            render(s, level, flag, pcm.data(), samples);
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
    // Hot boost air: about +120 C at full head, intercoolers take it back out (bigger and slower is better), and the
    // engine's boost factor follows the air's density; naturally aspirated engines are untouched.
    {
        assert(near(compressor_heat(9, 9), 120) && compressor_heat(0, 9) == 0 && compressor_heat(4.5, 9) < 120);
        assert(intercooler_effectiveness(0, 10) == 0 && intercooler_effectiveness(6, 10) > 0.8 && intercooler_effectiveness(12, 10) > intercooler_effectiveness(6, 10) &&
               intercooler_effectiveness(6, 80) < intercooler_effectiveness(6, 10));
        assert(boost_factor_for_air(1.0, 140) == 1.0 && near(boost_factor_for_air(2.0, 30), 2.0) && boost_factor_for_air(2.0, 140) < 1.8 && boost_factor_for_air(2.0, 0) > 2.0);
        assert(knock_damage(80, 2, 1) == 0 && knock_damage(140, 1.1, 1) == 0 && knock_damage(140, 2, 1) > 0 && std::isfinite(boost_factor_for_air(2.0, NAN)));
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
    // Air filter and intercooler meshes: readable, both paint markers, inside their cells.
    for (int large = 0; large < 2; ++large) {
        std::vector<std::vector<uint8_t>> meshes{build_air_filter_mesh(stock, large, "f")};
        for (int tile = 0; tile < 3 && !large; ++tile) meshes.push_back(build_intercooler_tile(stock, tile, "i"));
        for (size_t k = 0; k < meshes.size(); ++k) {
            MeshLayout m; assert(parse_mesh(meshes[k], m) && m.vertices > 50);
            std::vector<MeshVertex> v(m.vertices); std::memcpy(v.data(), meshes[k].data() + m.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
            bool paint0 = false, paint1 = false;
            double lo[3], hi[3];
            if (k == 0) { for (int j = 0; j < 3; ++j) { lo[j] = -kCell / 2; hi[j] = (large ? 1.5 : 0.5) * kCell; } }
            else { lo[0] = lo[1] = lo[2] = -kCell / 2; hi[0] = hi[2] = kCell / 2; hi[1] = 1.5 * kCell; }   // one cell wide, two tall
            for (auto& p : v) {
                for (int j = 0; j < 3; ++j) assert(p.pos[j] >= lo[j] - 0.003 && p.pos[j] <= hi[j] + 0.003);
                paint0 = paint0 || (p.rgba[0] == 153 && p.rgba[1] == 0); paint1 = paint1 || (p.rgba[0] == 153 && p.rgba[1] == 38);
            }
            assert((paint0 && paint1) || k == 2);             // the middle intercooler tile is all core
        }
    }
    return 0;
}
