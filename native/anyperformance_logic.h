#pragma once
// AnyPerformance: frame and tick statistics, kept free of Windows and game code so they can be tested anywhere.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace anyperformance {

// Named steps timed inside one frame or server tick (bunker loading on the server, pieces of client.tick on the
// main thread), indexed by the mod's step table. Times include the steps nested inside.
constexpr int kMaxSteps = 80;
struct Steps {
    double ms[kMaxSteps] = {};
    int calls[kMaxSteps] = {};
    void add(int i, double t) { if (i >= 0 && i < kMaxSteps) { ms[i] += t; ++calls[i]; } }
    void merge(const Steps& o) { for (int i = 0; i < kMaxSteps; ++i) { ms[i] += o.ms[i]; calls[i] += o.calls[i]; } }
};

// "load_static 2412 ms, read_json_dungeon 1900 ms (2x)": the `top` biggest steps of at least min_ms, biggest first.
// Writes "" when none qualifies. Returns the length written (or that would be written, like snprintf).
inline int format_steps(char* out, size_t cap, const Steps& s, const char* const* names, int n, int top, double min_ms) {
    if (cap) out[0] = 0;
    bool used[kMaxSteps] = {};
    int len = 0;
    n = std::min(n, kMaxSteps);
    for (int k = 0; k < top; ++k) {
        int best = -1;
        for (int i = 0; i < n; ++i)
            if (!used[i] && s.calls[i] && s.ms[i] >= min_ms && (best < 0 || s.ms[i] > s.ms[best])) best = i;
        if (best < 0) break;
        used[best] = true;
        size_t at = std::min(size_t(len), cap ? cap - 1 : 0);
        int w = s.calls[best] > 1
            ? std::snprintf(out + at, cap - at, "%s%s %.0f ms (%dx)", k ? ", " : "", names[best], s.ms[best], s.calls[best])
            : std::snprintf(out + at, cap - at, "%s%s %.0f ms", k ? ", " : "", names[best], s.ms[best]);
        if (w < 0) break;
        len += w;
    }
    return len;
}

// "creatures 3.1 ms, players 0.4 ms": the `top` biggest steps by average per frame or tick over `count` of them, of at
// least min_ms on average, biggest first. Writes "" when none qualifies. Returns the length, like format_steps.
inline int format_step_avg(char* out, size_t cap, const Steps& s, const char* const* names, int n, int top, int count,
                           double min_ms) {
    if (cap) out[0] = 0;
    if (count <= 0) return 0;
    bool used[kMaxSteps] = {};
    int len = 0;
    n = std::min(n, kMaxSteps);
    for (int k = 0; k < top; ++k) {
        int best = -1;
        for (int i = 0; i < n; ++i)
            if (!used[i] && s.calls[i] && s.ms[i] / count >= min_ms && (best < 0 || s.ms[i] > s.ms[best])) best = i;
        if (best < 0) break;
        used[best] = true;
        size_t at = std::min(size_t(len), cap ? cap - 1 : 0);
        int w = std::snprintf(out + at, cap - at, "%s%s %.1f ms", k ? ", " : "", names[best], s.ms[best] / count);
        if (w < 0) break;
        len += w;
    }
    return len;
}

// What one main-thread frame spent its time on, in milliseconds. Filled by the timing hooks.
struct FrameParts {
    double client_tick = 0;   // client.tick: everything the client simulates in a frame
    double scene_tick = 0;    // client_scene.tick: actors, vehicles, props and the client physics step
    double loading = 0;       // client_scene.tick_loading: choosing tiles to load and attaching finished ones
    double tile_attach = 0;   // client_scene.environment_tile_task.integrate: one finished tile into physics and the GPU
    int tile_attaches = 0;
    double vehicle_tasks = 0; // client_scene.vehicle.container._update_tasks: vehicle mesh rebuilds started and finished
                              // (inside tick_loading, like tile_attach)
    Steps steps;              // the timed pieces of client.tick
};

// Frame parts summed over a summary window, for the average breakdown.
struct FrameWindow {
    FrameParts sum;
    double frame_ms = 0;
    int frames = 0;
    void add(const FrameParts& p, double ms) {
        sum.client_tick += p.client_tick; sum.scene_tick += p.scene_tick; sum.loading += p.loading;
        sum.tile_attach += p.tile_attach; sum.tile_attaches += p.tile_attaches; sum.vehicle_tasks += p.vehicle_tasks;
        sum.steps.merge(p.steps); frame_ms += ms; ++frames;
    }
};

// The biggest piece of a slow frame, for the log and the overlay. client.tick holds client_scene.tick (simulation and
// the client physics step) and client_scene.tick_loading (tile streaming, which attaches finished tiles, and vehicle
// mesh rebuilds). Outside client.tick is the renderer, UI, audio and waiting on the GPU.
inline const char* blame(const FrameParts& p, double frame_ms) {
    struct { double ms; const char* name; } parts[] = {
        {p.tile_attach, "terrain tile attach"},
        {p.vehicle_tasks, "vehicle rebuild"},
        {p.loading - p.tile_attach - p.vehicle_tasks, "tile loading"},
        {p.scene_tick, "client simulation and physics"},
        {p.client_tick - p.scene_tick - p.loading, "other client work"},
        {frame_ms - p.client_tick, "rendering and UI"},
    };
    const char* best = "unknown"; double best_ms = 0;
    for (auto& x : parts) if (x.ms > best_ms) { best_ms = x.ms; best = x.name; }
    return best;
}

// What one server tick spent its time on, in milliseconds (server thread; host and solo only).
struct ServerParts {
    double total = 0;         // server.tick
    double loading = 0;       // server_scene.tick_loading: terrain tiles and dungeon layers around every player
    double tiles = 0;         //   of which server_scene.environment_tile_container.tick_loading
    double scene = 0;         // server_scene.tick: everything simulated, including the physics step
    double dungeons = 0;      //   server_scene.dungeon_manager.tick (bunkers, elevators)
    double items = 0;         //   loose items: item_floor_container.tick + post_physics
    double vehicles = 0;      //   server_scene.vehicle.container.tick + post_physics
    double entities = 0;      //   server_scene.entity.container.tick
    double physics = 0;       //   physics.scene.update: the physics engine's step (Jolt, native code in game.exe)
    double network = 0;       // replication.master.replicate_peer: packing the world for each player
    double autosave = 0;      // server.autosave_game
    Steps steps;              // bunker loading, terrain streaming and simulation steps
};

// Server tick parts summed over a summary window, for the average breakdown.
struct ServerWindow {
    ServerParts sum;
    int ticks = 0;
    void add(const ServerParts& p) {
        sum.total += p.total; sum.loading += p.loading; sum.tiles += p.tiles; sum.scene += p.scene;
        sum.dungeons += p.dungeons; sum.items += p.items; sum.vehicles += p.vehicles; sum.entities += p.entities;
        sum.physics += p.physics; sum.network += p.network; sum.autosave += p.autosave;
        sum.steps.merge(p.steps); ++ticks;
    }
};

// The biggest piece of a slow server tick. "Other simulation" is what is left of server_scene.tick after the parts
// measured inside it.
inline const char* blame_server(const ServerParts& p) {
    struct { double ms; const char* name; } parts[] = {
        {p.tiles, "terrain streaming"}, {p.loading - p.tiles, "dungeon/world loading"}, {p.dungeons, "bunkers/elevators"},
        {p.items, "loose items"}, {p.vehicles, "vehicles"}, {p.entities, "entities"},
        {p.physics, "physics step"},
        {p.scene - p.dungeons - p.items - p.vehicles - p.entities - p.physics, "other simulation"},
        {p.network, "network replication"}, {p.autosave, "autosave"},
        {p.total - p.loading - p.scene - p.network - p.autosave, "other server work"},
    };
    const char* best = "unknown"; double best_ms = 0;
    for (auto& x : parts) if (x.ms > best_ms) { best_ms = x.ms; best = x.name; }
    return best;
}

// The client draws the world from server ticks with its own playback clock (client.tick): when ticks stop arriving
// it slows down, and when they arrive in a burst it speeds up (up to 4x) to catch up. That is the "world slows, then
// slams into high gear" effect. SimEpisode turns the per-frame playback rate into one event per episode.
struct SimEpisode {
    static constexpr double kSlow = 0.85, kFast = 1.15, kEndAfter = 0.5;  // rate thresholds; seconds of normal rate to close
    bool active = false;
    double start = 0, last_off = 0, slow_s = 0, fast_s = 0, min_rate = 1, max_rate = 1;
    // Feed one frame; returns true when an episode just ended (read the fields, then call reset()).
    bool add(double now_s, double frame_s, double rate) {
        bool off = rate < kSlow || rate > kFast;
        if (off) {
            if (!active) { *this = SimEpisode{}; active = true; start = now_s; }
            last_off = now_s;
            if (rate < kSlow) slow_s += frame_s; else fast_s += frame_s;
            min_rate = std::min(min_rate, rate); max_rate = std::max(max_rate, rate);
            return false;
        }
        return active && now_s - last_off >= kEndAfter;
    }
    void reset() { active = false; }
};

// A rolling window of samples (milliseconds) with percentiles. Single-threaded.
template <int N> struct Window {
    float v[N]; mutable float scratch[N]; int count = 0, next = 0;
    void add(double ms) { v[next] = float(ms); next = (next + 1) % N; if (count < N) ++count; }
    void clear() { count = next = 0; }
    // p in [0,100]: the sample below which p percent fall. 0 when empty.
    double percentile(double p) const {
        if (!count) return 0;
        float* tmp = scratch;
        std::memcpy(tmp, v, sizeof(float) * count);
        int k = std::clamp(int(p / 100.0 * (count - 1) + 0.5), 0, count - 1);
        std::nth_element(tmp, tmp + k, tmp + count);
        return tmp[k];
    }
    double mean() const { double s = 0; for (int i = 0; i < count; ++i) s += v[i]; return count ? s / count : 0; }
    double max() const { double m = 0; for (int i = 0; i < count; ++i) m = std::max(m, double(v[i])); return m; }
};

// Server tick pacing. The game aims for 60 ticks a second (16.7 ms apart); waits between ticks use Sleep(1), so with
// a coarse Windows timer the ticks bunch up: some arrive late, then the next one comes at once to catch up.
struct TickPacing {
    static constexpr double kTarget = 1000.0 / 60.0;
    int ticks = 0, late = 0, bunched = 0;    // late: > 1.5x the target apart; bunched: < 0.5x (catch-up ticks)
    double sum_ms = 0, max_ms = 0, sum_gap = 0, sum_gap_sq = 0, max_gap = 0;
    void add(double gap_ms, double tick_ms) {
        ++ticks; sum_ms += tick_ms; max_ms = std::max(max_ms, tick_ms);
        if (gap_ms <= 0) return;
        sum_gap += gap_ms; sum_gap_sq += gap_ms * gap_ms; max_gap = std::max(max_gap, gap_ms);
        if (gap_ms > kTarget * 1.5) ++late;
        if (gap_ms < kTarget * 0.5) ++bunched;
    }
    // Standard deviation of the gap between ticks: about 0 for even pacing, 5+ ms when ticks bunch.
    double jitter() const {
        if (ticks < 2) return 0;
        double n = ticks, m = sum_gap / n, var = sum_gap_sq / n - m * m;
        return var > 0 ? std::sqrt(var) : 0;
    }
};

// One summary line, e.g. "30 s: 58.2 fps avg, 1% low 41.0 fps, 3 hitches (worst 142 ms) | server 60.0 ticks/s, 1.9 ms
// avg, 6.1 max, spacing jitter 0.4 ms, 0 late, 0 bunched | tiles 4 attached (max 11.2 ms)".
inline int format_summary(char* out, size_t cap, double seconds, int frames, double avg_frame_ms, double low1_ms,
                          int hitches, double worst_ms, const TickPacing& t, int tiles, double tile_max_ms) {
    double fps = avg_frame_ms > 0 ? 1000.0 / avg_frame_ms : 0, low = low1_ms > 0 ? 1000.0 / low1_ms : 0;
    int n = std::snprintf(out, cap, "%.0f s: %.1f fps avg, 1%% low %.1f fps, %d frames, %d hitches", seconds, fps, low, frames, hitches);
    if (hitches && n > 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - n, " (worst %.0f ms)", worst_ms);
    if (n > 0 && size_t(n) < cap) {
        if (t.ticks) n += std::snprintf(out + n, cap - n, " | server %.1f ticks/s, %.1f ms avg, %.1f max, spacing jitter %.1f ms, %d late, %d bunched",
                                        t.ticks / seconds, t.sum_ms / t.ticks, t.max_ms, t.jitter(), t.late, t.bunched);
        else n += std::snprintf(out + n, cap - n, " | no server here (joined game)");
    }
    if (n > 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - n, " | tiles %d attached (max %.1f ms)", tiles, tile_max_ms);
    return n;
}

}  // namespace anyperformance
