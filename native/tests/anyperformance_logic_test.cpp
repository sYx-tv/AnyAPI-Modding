#include "../anyperformance_logic.h"
#include <cassert>
#include <cmath>
#include <cstring>
using namespace anyperformance;
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }
int main() {
    // Percentiles and mean over a rolling window; old samples drop out once it is full.
    static Window<100> w;
    assert(w.percentile(99) == 0 && w.mean() == 0);
    for (int i = 1; i <= 100; ++i) w.add(i);
    assert(near(w.mean(), 50.5) && near(w.percentile(0), 1) && near(w.percentile(100), 100) && near(w.percentile(99), 99));
    assert(near(w.max(), 100));
    for (int i = 0; i < 100; ++i) w.add(10);
    assert(near(w.mean(), 10) && near(w.percentile(99), 10));
    w.clear(); assert(w.count == 0);

    // Blame picks the biggest piece; streaming time excludes the tile attach and vehicle rebuild inside it.
    FrameParts p; p.client_tick = 60; p.scene_tick = 8; p.loading = 50; p.tile_attach = 45; p.tile_attaches = 1;
    assert(std::strcmp(blame(p, 70), "terrain tile attach") == 0);
    p.tile_attach = 2; p.vehicle_tasks = 40;
    assert(std::strcmp(blame(p, 70), "vehicle rebuild") == 0);
    p = FrameParts{}; p.client_tick = 10; p.scene_tick = 8;
    assert(std::strcmp(blame(p, 90), "rendering and UI") == 0);
    p.client_tick = 80; p.scene_tick = 75;
    assert(std::strcmp(blame(p, 90), "client simulation and physics") == 0);
    assert(std::strcmp(blame(FrameParts{}, 0), "unknown") == 0);

    // Even 60 Hz pacing has no jitter; Sleep(1) on a 15.6 ms timer gives late and bunched ticks.
    TickPacing even;
    for (int i = 0; i < 600; ++i) even.add(1000.0 / 60, 2);
    assert(even.jitter() < 1e-3 && even.late == 0 && even.bunched == 0 && near(even.sum_ms / even.ticks, 2));
    TickPacing coarse;
    for (int i = 0; i < 300; ++i) { coarse.add(31.25, 2); coarse.add(0.1, 2); }
    assert(coarse.jitter() > 10 && coarse.late == 300 && coarse.bunched == 300);

    char line[640];
    int n = format_summary(line, sizeof line, 30, 1800, 1000.0 / 60, 25, 2, 140, even, 3, 12.5);
    assert(n > 0 && std::strstr(line, "60.0 fps avg") && std::strstr(line, "1% low 40.0 fps") && std::strstr(line, "2 hitches (worst 140 ms)"));
    assert(std::strstr(line, "server 20.0 ticks/s") && std::strstr(line, "tiles 3 attached (max 12.5 ms)"));
    n = format_summary(line, sizeof line, 30, 10, 20, 30, 0, 0, TickPacing{}, 0, 0);
    assert(n > 0 && std::strstr(line, "no server here") && !std::strstr(line, "worst"));
    // Server blame: the physics step when timed, else what server_scene.tick spent outside the measured parts.
    ServerParts sp; sp.total = 90; sp.scene = 80; sp.items = 5; sp.vehicles = 4; sp.network = 3;
    assert(std::strcmp(blame_server(sp), "other simulation") == 0);
    sp.physics = 60; assert(std::strcmp(blame_server(sp), "physics step") == 0);
    sp.physics = 0;
    sp.dungeons = 70; assert(std::strcmp(blame_server(sp), "bunkers/elevators") == 0);
    sp = ServerParts{}; sp.total = 60; sp.loading = 55; sp.tiles = 50;
    assert(std::strcmp(blame_server(sp), "terrain streaming") == 0);
    sp.tiles = 2; assert(std::strcmp(blame_server(sp), "dungeon/world loading") == 0);
    sp = ServerParts{}; sp.total = 300; sp.autosave = 290; assert(std::strcmp(blame_server(sp), "autosave") == 0);

    // Steps: the biggest first, repeat counts shown, small ones left out.
    Steps st; const char* names[] = {"a", "b", "c", "d"};
    st.add(0, 5); st.add(1, 2400); st.add(2, 300); st.add(2, 200); st.add(3, 0.2);
    n = format_steps(line, sizeof line, st, names, 4, 2, 1.0);
    assert(n > 0 && std::strcmp(line, "b 2400 ms, c 500 ms (2x)") == 0);
    n = format_steps(line, sizeof line, st, names, 4, 9, 1.0);
    assert(std::strcmp(line, "b 2400 ms, c 500 ms (2x), a 5 ms") == 0);
    n = format_steps(line, 8, st, names, 4, 3, 1.0);
    assert(std::strlen(line) == 7 && n > 7);
    assert(format_steps(line, sizeof line, Steps{}, names, 4, 3, 1.0) == 0 && line[0] == 0);

    // Averages over a window: per tick, one decimal, small ones left out.
    ServerWindow sw; ServerParts tick; tick.total = 20; tick.items = 6; tick.steps.add(1, 8); tick.steps.add(0, 0.1);
    sw.add(tick); tick.steps = Steps{}; tick.steps.add(1, 4); sw.add(tick);
    assert(sw.ticks == 2 && sw.sum.total == 40 && sw.sum.items == 12 && sw.sum.steps.calls[1] == 2);
    n = format_step_avg(line, sizeof line, sw.sum.steps, names, 4, 3, sw.ticks, 0.1);
    assert(n > 0 && std::strcmp(line, "b 6.0 ms") == 0);
    assert(format_step_avg(line, sizeof line, sw.sum.steps, names, 4, 3, 0, 0.1) == 0 && line[0] == 0);
    FrameWindow fw; FrameParts fp; fp.client_tick = 9; fp.steps.add(2, 3); fw.add(fp, 20); fw.add(fp, 22);
    assert(fw.frames == 2 && fw.frame_ms == 42 && fw.sum.client_tick == 18 && fw.sum.steps.ms[2] == 6);

    // A slow-down then catch-up becomes one episode, closed after half a second back at normal speed.
    SimEpisode e; double t = 0, f = 1.0 / 60;
    for (int i = 0; i < 60; ++i, t += f) assert(!e.add(t, f, 1.0));
    assert(!e.active);
    for (int i = 0; i < 30; ++i, t += f) assert(!e.add(t, f, 0.5));
    for (int i = 0; i < 20; ++i, t += f) assert(!e.add(t, f, 2.0));
    bool ended = false;
    for (int i = 0; i < 40 && !ended; ++i, t += f) ended = e.add(t, f, 1.0);
    assert(ended && near(e.min_rate, 0.5) && near(e.max_rate, 2.0) && near(e.slow_s, 0.5, 1e-9) && near(e.fast_s, 20 * f, 1e-9));
    e.reset(); assert(!e.active && !e.add(t, f, 1.0));
    return 0;
}
