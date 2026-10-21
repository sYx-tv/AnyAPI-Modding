// Dyno logic and mesh tests: pulls recorded from the game's own engine power formula, kept and dropped pulls, data
// channels, axis steps, the part definition and the generated mesh.
#include "../dyno_logic.h"
#include "../dyno_mesh.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace dyno;

namespace {
bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

// The game's engine (research-engine-air.md): watts = size * 400 * clamp(rpm / 8000 + 0.2) * boost * throttle, with
// the fuel cut at 8,000 RPM.
double game_watts(double rpm, double boost, double throttle, double size = 4) {
    if (rpm >= 8000) return 0;
    return size * 400 * std::min(1.0, rpm / 8000 + 0.2) * boost * throttle;
}

// Drives one pull: throttle on from `from` RPM, rising `rate` RPM per second to `to`, then off.
void pull(Recorder& r, double from, double to, double rate, double boost = 1.0, double knock = 0) {
    double rpm = from;
    const double dt = 1.0 / 60;
    while (rpm < to) {
        EngineSample s; s.engines = 1; s.rps = rpm / 60; s.throttle = 1; s.boost = boost; s.temperature_c = 90 + rpm / 200;
        s.watts = game_watts(rpm, boost, 1); s.knock = knock;
        bool kept = r.feed(s, dt);
        assert(!kept || rpm >= kAtCutRpm);            // a pull is kept when it reaches the fuel cut
        rpm += rate * dt;
    }
    EngineSample off; off.engines = 1; off.rps = rpm / 60; off.throttle = 0;
    r.feed(off, dt);
}
}  // namespace

int main() {
    // A full pull from 2,000 to 8,000 RPM is kept with the formula's numbers: flat power from 6,400 RPM.
    Recorder r;
    pull(r, 2000, 8000, 1500);
    assert(r.count == 1 && !r.active);
    const Run& a = *r.latest();
    assert(a.number == 1 && a.valid());
    assert(a.lo == 20 && a.hi >= 79 && a.hi <= 80);
    double w, rpm;
    a.peak_power(w, rpm);
    assert(near(w, 1600, 1e-6) && rpm >= 6400 && rpm <= 6500);
    assert(near(a.watts(30), game_watts(3000, 1, 1), 15));
    double nm, nm_rpm;
    a.peak_torque(nm, nm_rpm);
    assert(nm_rpm == 2000 || nm_rpm == 2100);           // torque is highest low down: power rises slower than RPM
    assert(near(nm, torque_nm(game_watts(nm_rpm, 1, 1), nm_rpm / 60), 30));
    assert(near(a.seconds, 4.0, 0.1) && a.temperature_peak > 120 && !a.knocked());

    // Boost multiplies the curve; knock damage during the pull is noticed.
    pull(r, 2000, 8000, 1500, 1.6, 1e-4);
    assert(r.count == 2 && r.latest()->number == 2 && r.latest()->knocked() && near(r.latest()->boost_peak, 1.6, 1e-9));
    r.latest()->peak_power(w, rpm);
    assert(near(w, 1600 * 1.6, 1e-6));

    // Bouncing on the fuel cut while a turbo keeps spooling adds nothing: the sweep ended at the cut.
    {
        Recorder lim;
        double rpm = 2000;
        const double dt = 1.0 / 60;
        for (; rpm < 7990; rpm += 1500 * dt) {
            EngineSample s; s.engines = 1; s.rps = rpm / 60; s.throttle = 1; s.watts = game_watts(rpm, 1.0, 1);
            lim.feed(s, dt);
        }
        for (int i = 0; i < 600; ++i) {               // 10 s on the limiter, boost rising to 1.3
            double boost = 1.0 + 0.3 * i / 600.0;
            EngineSample s; s.engines = 1; s.throttle = 1; s.boost = boost;
            bool fuelled = i % 2 == 0;
            s.rps = (fuelled ? 7995.0 : 8040.0) / 60;
            s.watts = fuelled ? 1600 * boost : 0;
            assert(!lim.feed(s, dt) && !lim.active);
        }
        assert(lim.count == 1);                       // kept when it reached the cut
        EngineSample off; off.engines = 1; off.rps = 7000.0 / 60; off.throttle = 0;
        assert(!lim.feed(off, dt));
        const Run& run = *lim.latest();
        double w, at;
        run.peak_power(w, at);
        assert(run.hi <= 80 && near(w, 1600, 1e-6) && near(run.boost_peak, 1.0, 1e-9) && run.seconds < 4.5);
    }

    // Too short a sweep (1,000 RPM) or too short a time is dropped; part throttle never starts a pull.
    pull(r, 3000, 4000, 1500);
    assert(r.count == 2);
    pull(r, 2000, 5000, 6000);                           // 0.5 s
    assert(r.count == 2);
    EngineSample half; half.engines = 1; half.rps = 50; half.throttle = 0.5; half.watts = 900;
    r.feed(half, 1.0 / 60);
    assert(!r.active);
    EngineSample none; none.throttle = 1; none.rps = 50;   // no engine on the vehicle
    r.feed(none, 1.0 / 60);
    assert(!r.active);

    // Only three pulls are kept, oldest dropped first; numbering continues.
    pull(r, 2000, 8000, 1500);
    pull(r, 2000, 8000, 1500);
    assert(r.count == 3 && r.runs[0].number == 2 && r.runs[2].number == 4);
    r.clear();
    assert(r.count == 0 && !r.latest() && r.next_number == 1);

    // Clearing mid-pull keeps the pull being recorded; it becomes pull 1.
    Recorder mid;
    pull(mid, 2000, 8000, 1500);
    for (int i = 0; i < 120; ++i) {
        EngineSample s; s.engines = 1; s.throttle = 1; s.rps = (2000 + i * 25.0) / 60; s.watts = game_watts(s.rps * 60, 1, 1);
        mid.feed(s, 1.0 / 60);
        if (i == 60) mid.clear();
    }
    assert(mid.active && mid.count == 0);
    pull(mid, 5000, 8000, 1500);                         // carries straight on: the throttle never came off
    assert(mid.count == 1 && mid.latest()->number == 1 && mid.latest()->lo == 20);

    // A pull held at the limiter for minutes is kept once, when it reaches the cut.
    Recorder held;
    for (int i = 0; i < 60 * 100; ++i) {
        EngineSample s; s.engines = 1; s.throttle = 1; s.rps = std::min(7990.0, 2000 + i * 25.0) / 60; s.watts = game_watts(s.rps * 60, 1, 1);
        held.feed(s, 1.0 / 60);
    }
    assert(held.count == 1);

    // Data channels.
    Recorder c;
    pull(c, 2000, 8000, 1500);
    EngineSample s; s.engines = 2; s.rps = 100; s.watts = 2000; s.throttle = 0.3; s.boost = 1.4; s.temperature_c = 95;
    c.feed(s, 1.0 / 60);
    double slots[kChannelCount]{};
    fill_outputs(c, slots);
    assert(near(slots[kRpm], 6000, 1e-9) && near(slots[kPowerHp], 2000 / kWattsPerHp, 1e-9) && near(slots[kPowerKw], 2, 1e-9));
    assert(near(slots[kTorqueNm], 2000 / (2 * kPi * 100), 1e-9) && slots[kBoost] == 1.4 && slots[kEngineTemperature] == 95);
    assert(slots[kRecording] == 0 && slots[kRunCount] == 1 && near(slots[kPeakHp], 1600 / kWattsPerHp, 1e-6) && slots[kPeakRpm] >= 6400);
    assert(channel_index("clear_runs", 10) == kClearRuns && kChannels[kClearRuns].input && channel_index("rpmx", 4) < 0 && channel_index("rpm", 3) == kRpm);
    for (int i = 0; i < kChannelCount; ++i) for (int j = i + 1; j < kChannelCount; ++j) assert(std::strcmp(kChannels[i].name, kChannels[j].name) != 0);

    // Axis steps: a round top just above the value, four to six grid lines.
    double top, step;
    const double values[] = {0.3, 2.1, 38.6, 63.7, 99, 150, 1234};
    for (double v : values) {
        nice_axis(v, top, step);
        assert(top >= v && top <= v * 2.6 + 1 && top / step >= 2 - 1e-9 && top / step <= 6 + 1e-9);
    }
    nice_axis(63.7, top, step);
    assert((top == 70 && (step == 10 || step == 20)) || (top == 75 && step == 25) || (top == 80 && step == 20));
    assert(thousands(6400) == "6,400" && thousands(950) == "950" && thousands(12345678) == "12,345,678");

    // Definition: the altitude sensor class, every channel, the mesh it was given.
    std::string json = definition_json(kDynoMeshPath);
    assert(json.find("\"class\": \"altitude_sensor\"") != std::string::npos && json.find("\"id\": \"dyno_sensor\"") != std::string::npos);
    assert(json.find("\"tech_tier\": 4,") != std::string::npos);   // world loot after the 3rd bunker
    assert(json.find(kDynoMeshPath) != std::string::npos && definition_json().find(kStockMeshPath) != std::string::npos);
    for (auto& ch : kChannels) assert(json.find(std::string("\"name\": \"") + ch.name + "\"") != std::string::npos);
    assert(json.find("type_f64_input\", \"name\": \"clear_runs\"") != std::string::npos);

    // Mesh: a synthetic file in the compass layout (as tests/gpspart_logic_test.cpp) gains the bars and the curve.
    std::vector<uint8_t> mf;
    auto put = [&](const void* p, size_t n) { auto* b = static_cast<const uint8_t*>(p); mf.insert(mf.end(), b, b + n); };
    put("mesh", 4); uint32_t u = 5; put(&u, 4); u = 1; put(&u, 4); const char* nm_ = "compass_sensor_a"; u = 16; put(&u, 4); put(nm_, 16);
    uint32_t hdr[8] = {3, 1, 5, 2, 2, 3, 3, 4}; put(hdr, 32); uint8_t zero[56] = {}; put(zero, 56); double bd[6] = {-.04, -.04, -.04, .04, .12, .04}; put(bd, 48);
    gpspart::MeshVertex vs[3] = {}; float P[3][3] = {{.04f, -.04f, .04f}, {.04f, .04f, .04f}, {-.04f, .12f, -.04f}};
    for (int i = 0; i < 3; ++i) { std::memcpy(vs[i].pos, P[i], 12); vs[i].rgba[3] = 255; }
    u = sizeof vs; put(&u, 4); put(vs, sizeof vs); uint32_t I[3] = {0, 1, 2}; u = 12; put(&u, 4); put(I, 12); uint8_t tr[8] = {}; put(tr, 8);
    auto g = build_dyno_mesh(mf, kDynoMeshName);
    gpspart::MeshLayout G;
    const uint32_t quads = 3 * 5 + 8;                    // bars (5 faces each) and the curve's segments, two-sided
    assert(!g.empty() && gpspart::parse_mesh(g, G) && G.vertices == 3 + quads * 8 && G.indices == 3 + quads * 12);
    assert(std::memcmp(g.data() + 16, kDynoMeshName, std::strlen(kDynoMeshName)) == 0);
    double gb[6];
    std::memcpy(gb, g.data() + G.bounds, 48);
    assert(near(gb[4], .14, 1e-6) && gb[5] > .04 && gb[0] == -.04);
    for (uint32_t i = 3; i < G.vertices; ++i) {
        gpspart::MeshVertex v;
        std::memcpy(&v, g.data() + G.vertex_bytes + 4 + size_t(i) * 36, 36);
        assert(v.rgba[0] == 255 && v.rgba[1] == 122 && v.pos[1] <= gb[4] + 1e-6 && v.pos[2] <= gb[5] + 1e-6 && std::abs(v.pos[0]) <= .04f);
    }
    auto bad = mf; bad[0] = 'x';
    assert(build_dyno_mesh(bad, "x").empty());
    std::puts("dyno logic ok");
    return 0;
}
