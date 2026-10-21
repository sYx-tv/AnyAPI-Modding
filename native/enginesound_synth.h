#pragma once
// EngineSound piston engine synth: every cylinder's exhaust pulse, fired in the engine's real firing order, through
// its exhaust's resonances, with intake, valvetrain and (for diesels) combustion knock on top. No game or Windows
// dependencies, so tests/enginesound_synth_test.cpp renders it on any platform (and writes the preview WAVs).
//
// A four-stroke cylinder fires once every two crank turns, so the pulse rate is rps x cylinders / 2. What makes
// one layout sound unlike another is when in the 720 degree cycle each cylinder fires and which exhaust bank its
// pulse leaves through:
//   - inline: one bank, evenly spaced (720 / n degrees); a single is a lone thump, a 5 warbles, a 6 is silk.
//   - V: two banks. A V-twin fires 315/405 (the "potato"), a V4 180-270-180-90, V6/V10/V12 evenly. A cross-plane
//     V8 fires evenly overall but each bank gets its pulses unevenly (L R L L R R R L): that is the burble.
//     Its "Firing" setting turns it into a flat-plane V8 (banks alternate, a shriek).
//   - boxer: two banks, evenly spaced; unequal-length headers delay one bank's pulses into a lumpy rumble.
// No two cylinders burn quite alike either, which puts a faint once-a-cycle roughness under every engine.
//
// The game streams it like the AnyEngineParts sounds: 48 kHz mono 16-bit PCM into a positional voice effect.
#include <cmath>
#include <cstdint>

namespace enginesound {
namespace synth {

constexpr int kSampleRate = 48000;
constexpr double kMaxRps = 8000.0 / 60.0;   // the game's engine speed for factor_speed 1
constexpr double kStartRps = 6.0;           // the game's engine start speed
constexpr double kIdleRps = 12.5;           // what the start speed sounds like: a 750 rpm idle
constexpr int kMaxCylinders = 16;

// The game's engines idle at its start speed (360 rpm), far below a real idle, so the synth hears the game's rev
// range stretched up from a real idle to the same 8000 rpm top.
inline double sound_rps(double game_rps) {
    if (!std::isfinite(game_rps) || game_rps <= 0) return 0;
    if (game_rps < kStartRps) return game_rps * kIdleRps / kStartRps;   // cranking and stalling
    return kIdleRps + (game_rps - kStartRps) * (kMaxRps - kIdleRps) / (kMaxRps - kStartRps);
}

enum Layout : int { kInline, kVee, kBoxer, kLayoutCount };

// The sound of one engine build. Every field is 0-1 except the frequencies and gain.
struct Tune {
    double pipe_hz;    // exhaust pipe boom (the deep note): bigger pipes and engines lower
    double body_hz;    // mid "bark" of each pulse
    double rasp;       // raspy edge of each pulse
    double muffler;    // 0 straight pipe .. 1 quiet stock muffler
    double cam;        // idle lope: lumpy, uneven idle that clears with revs
    double crackle;    // pops and bangs when lifting off at speed
    double intake;     // intake roar under load
    double mech;       // valvetrain tick and gear whine
    double diesel;     // diesel clatter (combustion knock), and no crackle
    double uneven;     // V8: cross-plane (0) .. flat-plane (1); boxer and others: unequal-length headers
    double gain;
};

struct Input {
    double rps = 0;        // crank speed
    double load = 0;       // the engine's power factor, 0-1
    bool running = false;  // combustion on
    Layout layout = kInline;
    int cylinders = 4;
    Tune tune{};
};

inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline int clamp_cylinders(int n) { return n < 1 ? 1 : n > kMaxCylinders ? kMaxCylinders : n; }

// ------------------------------------------------------------------ firing order
struct Firing {
    int count = 0;
    double angle[kMaxCylinders] = {};   // crank degrees in the 720 degree cycle, ascending
    int bank[kMaxCylinders] = {};       // exhaust bank 0 or 1
    double bias[kMaxCylinders] = {};    // this cylinder's strength over the mean
};

inline Firing firing(Layout layout, int cylinders, double uneven) {
    Firing f;
    const int n = clamp_cylinders(cylinders);
    f.count = n;
    const double even = 720.0 / n;
    for (int i = 0; i < n; ++i) { f.angle[i] = i * even; f.bank[i] = 0; }
    if (layout == kVee && n >= 2) {
        for (int i = 0; i < n; ++i) f.bank[i] = i & 1;
        if (n == 2) {
            f.angle[1] = 405.0 + 45.0 * clamp01(uneven);           // 45 degree twin 405/315 .. 90 degree twin 450/270
        } else if (n == 4) {
            const double a[4] = {0, 180, 450, 630};                // 90 degree V4: 180-270-180-90
            for (int i = 0; i < 4; ++i) f.angle[i] = a[i];
        } else if (n == 8 && uneven < 0.5) {
            const int b[8] = {0, 1, 0, 0, 1, 1, 1, 0};             // cross-plane: 1-8-4-3-6-5-7-2
            for (int i = 0; i < 8; ++i) f.bank[i] = b[i];
        }
    } else if (layout == kBoxer && n >= 2) {
        for (int i = 0; i < n; ++i) f.bank[i] = i & 1;
        if (n >= 4) {
            // Unequal-length headers: the long side's pulses arrive late and bunch with the next one.
            const double lag = 0.3 * even * clamp01(uneven);
            for (int i = 0; i < n; ++i) if (f.bank[i] == 1 && (i / 2) % 2 == 0) f.angle[i] += lag;
        }
    } else if (layout == kInline && n >= 4 && uneven > 0) {
        // Inline headers of different lengths: a slight unevenness, a raspier merge.
        for (int i = 0; i < n; i += 2) f.angle[i] += 0.08 * even * clamp01(uneven);
    }
    // Every cylinder burns a little differently (fixed per engine, like a real one).
    static const double kBias[kMaxCylinders] = {0.05, -0.04, 0.02, -0.06, 0.04, -0.01, -0.03, 0.06,
                                                -0.05, 0.03, 0.01, -0.02, 0.05, -0.04, 0.02, -0.03};
    for (int i = 0; i < n; ++i) f.bias[i] = kBias[i];
    return f;
}

// ------------------------------------------------------------------ sound options per layout
// The Properties tool's sound row offers these for the engine's layout (plus the game's own sound and Custom).
struct Preset { const char* name; Tune tune; };
constexpr int kPresetsPerLayout = 5;
//                                      pipe body rasp muffler cam crackle intake mech diesel uneven gain
constexpr Preset kPresets[kLayoutCount][kPresetsPerLayout] = {
    {{"Stock",          {95,  420, 0.25, 0.85, 0.10, 0.00, 0.20, 0.30, 0, 0.0, 1.0}},
     {"Sport",          {105, 480, 0.55, 0.45, 0.20, 0.40, 0.50, 0.35, 0, 0.3, 1.0}},
     {"Street tuner",   {100, 520, 0.40, 0.55, 0.10, 0.25, 0.45, 0.40, 0, 0.0, 1.0}},
     {"Race",           {120, 560, 0.85, 0.10, 0.35, 0.80, 0.80, 0.50, 0, 0.6, 1.0}},
     {"Diesel",         {72,  380, 0.10, 0.80, 0.10, 0.00, 0.45, 0.40, 1, 0.0, 1.0}}},
    {{"Stock",          {80,  360, 0.20, 0.85, 0.10, 0.00, 0.20, 0.25, 0, 0.0, 1.0}},
     {"Muscle",         {58,  300, 0.60, 0.20, 0.85, 0.50, 0.55, 0.30, 0, 0.0, 1.25}},
     {"Exotic",         {130, 600, 0.80, 0.25, 0.10, 0.60, 0.70, 0.60, 0, 1.0, 1.0}},
     {"Race",           {110, 520, 0.85, 0.05, 0.40, 0.90, 0.80, 0.50, 0, 0.0, 1.0}},
     {"Truck diesel",   {52,  270, 0.25, 0.40, 0.15, 0.00, 0.85, 0.40, 1, 0.0, 1.0}}},
    {{"Stock",          {90,  400, 0.25, 0.85, 0.10, 0.00, 0.20, 0.30, 0, 0.0, 1.0}},
     {"Rally rumble",   {85,  420, 0.50, 0.40, 0.25, 0.60, 0.50, 0.35, 0, 1.0, 1.0}},
     {"Air-cooled",     {110, 520, 0.60, 0.55, 0.15, 0.10, 0.30, 0.70, 0, 0.3, 1.0}},
     {"Race",           {120, 560, 0.85, 0.10, 0.30, 0.80, 0.80, 0.50, 0, 0.5, 1.0}},
     {"Diesel",         {70,  360, 0.10, 0.75, 0.15, 0.00, 0.45, 0.40, 1, 0.0, 1.0}}},
};
inline const char* layout_name(Layout l) { return l == kVee ? "V" : l == kBoxer ? "Boxer" : "Inline"; }

// ------------------------------------------------------------------ synth
// Band-pass biquad (RBJ, constant skirt gain: peak gain = Q).
struct Resonator {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(double hz, double q) {
        if (hz > kSampleRate * 0.45) hz = kSampleRate * 0.45;
        double w = 6.283185307179586 * hz / kSampleRate, alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = q * alpha / a0; b2 = -q * alpha / a0; a1 = -2.0 * std::cos(w) / a0; a2 = (1.0 - alpha) / a0;
    }
    double run(double x) {
        double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        if (!std::isfinite(y)) { x1 = x2 = y1 = y2 = 0; y = 0; }
        return y;
    }
};

struct Bank {
    double age = 1, amp = 0, tau = 0.004, noise = 1;
    double thump = 0;
    Resonator pipe, pipe2, body;
};

struct Synth {
    double rps = 0, load = 0, run_env = 0, time = 0;
    double to_next = 0;               // crank degrees to the next cylinder firing
    int next = 0;                     // next cylinder in the firing table
    Firing table;
    Layout layout = kLayoutCount;
    int cylinders = 0;
    double uneven = -1;
    Bank bank[2];
    double lope_phase = 0, lope_rate = 2.0;
    // overrun and limiter
    double load_peak = 0, crackle_until = -1, next_pop = 0, pop_age = 1, pop_amp = 0;
    double limit_phase = 0, catch_boost = 0;
    // intake, valvetrain, knock
    double intake_env = 0, tick_phase = 0, tick_age = 1, knock_age = 1, knock_amp = 0, whine_phase = 0;
    // The starter: heard only while it turns a stopped engine over, never while the engine spins down after
    // combustion stops (key off), which used to sweep its whine down the whole rev range.
    double starter_phase = 0, starter_env = 0;
    bool coasting = false;            // combustion stopped with the crank still turning
    Resonator rasp, rasp_hi, intake, tick, knock, knock_hi, pop_body;
    double tuned_pipe = -1, tuned_body = -1;
    Layout tuned_layout = kLayoutCount;
    // burble: the short tone each pulse rings with at a V's idle and overrun
    double blat_phase = 0, blat_hz = 100, blat_amp = 0, blat_age = 1, blat_tau = 0.03;
    Resonator knock_mid;
    // diesel: how hard it is pulling (builds with revs and load) and its exhaust roar
    double spool = 0, roar_lp = 0;
    Resonator roar;
    double soft = 0, soft2 = 0, dc_x = 0, dc_y = 0;
    uint32_t rng = 0x9e3779b9u;
    double noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return double(rng) / 2147483648.0 - 1.0; }
    double random01() { return 0.5 + 0.5 * noise(); }
};

// Bigger engines run bigger pipes: the boom drops a little as cylinders are added.
inline double pipe_hz_for(const Tune& t, int cylinders) { return t.pipe_hz * std::pow(4.0 / clamp_cylinders(cylinders), 0.12); }

inline void render(Synth& s, const Input& in, int16_t* out, int count) {
    const Tune& t = in.tune;
    const int n = clamp_cylinders(in.cylinders);
    const Layout layout = in.layout >= kInline && in.layout < kLayoutCount ? in.layout : kInline;
    if (s.layout != layout || s.cylinders != n || s.uneven != t.uneven) {
        s.table = firing(layout, n, t.uneven);
        s.layout = layout; s.cylinders = n; s.uneven = t.uneven;
        s.next = 0;
        s.to_next = 0;
    }
    const double pipe_hz = pipe_hz_for(t, n);
    const bool vee = layout == kVee;
    if (s.tuned_pipe != pipe_hz || s.tuned_body != t.body_hz || s.tuned_layout != layout) {
        // Open pipes ring longer; an inline's single pipe is kept tighter so it doesn't flap.
        const double q = (2.6 + 2.4 * (1.0 - t.muffler)) * (layout == kInline ? 0.75 : 1.0);
        for (int b = 0; b < 2; ++b) {
            // A V's two banks run their own pipes, tuned well apart: with the cross-plane's uneven bank order that
            // is the burble. Other layouts' second bank is only a little longer.
            const double d = b ? (vee ? 0.78 : 0.91) : 1.0;
            s.bank[b].pipe.set(pipe_hz * d, q);
            s.bank[b].pipe2.set(pipe_hz * d * 2.02, q * 0.8);
            s.bank[b].body.set(t.body_hz * (b ? (vee ? 0.84 : 0.94) : 1.0), 2.2);
        }
        s.rasp.set(1700 + 900 * (1.0 - t.muffler), 1.0);
        s.rasp_hi.set(3600, 1.3);
        s.intake.set(520, 1.4);
        s.tick.set(5200, 4.0);
        // Diesel clatter rings the block, pitched off the bark: a truck's is a heavy, hollow knock, a car's a
        // lighter tick.
        // A V (truck) diesel's knock is a deep, dull thud from a big block, not a tick.
        const double kb = t.body_hz * (vee ? 1.8 : 2.6);
        s.knock.set(kb, vee ? 2.2 : 3.5);
        s.knock_mid.set(kb * 1.6, vee ? 2.0 : 3.0);
        s.knock_hi.set(kb * 2.5, 2.5);
        s.roar.set(pipe_hz * (vee ? 1.6 : 3.2), 0.7);
        s.pop_body.set(pipe_hz * 1.7, 2.0);
        s.tuned_pipe = pipe_hz; s.tuned_body = t.body_hz; s.tuned_layout = layout;
    }
    const double dt = 1.0 / kSampleRate, two_pi = 6.283185307179586;
    const bool diesel = t.diesel > 0.5;
    double target_rps = std::isfinite(in.rps) && in.rps > 0 ? (in.rps > kMaxRps * 1.05 ? kMaxRps * 1.05 : in.rps) : 0;
    // A diesel idles low and runs out of breath early: a truck (V) is heard from 650 to 3,300 rpm, a car (inline,
    // boxer) to 4,500.
    const double diesel_top = (vee ? 3300.0 : 4500.0) / 60.0;
    if (diesel && target_rps > kIdleRps) target_rps = kIdleRps * 0.87 + (target_rps - kIdleRps) * (diesel_top - kIdleRps * 0.87) / (kMaxRps - kIdleRps);
    else if (diesel) target_rps *= 0.87;
    const double target_load = std::isfinite(in.load) ? clamp01(in.load) : 0;
    const double k_rps = 1.0 - std::exp(-dt / 0.05), k_load = 1.0 - std::exp(-dt / 0.035), k_run = 1.0 - std::exp(-dt / 0.03);
    const double k_starter = 1.0 - std::exp(-dt / 0.04);
    // Loudness per pulse: more cylinders fire more often, so each is a little quieter.
    const double per_pulse = std::pow(4.0 / n, 0.25);
    const double muffle = 0.35 + 0.65 * (1.0 - t.muffler);
    const double top = diesel ? diesel_top : kMaxRps;   // the engine's own redline
    for (int i = 0; i < count; ++i) {
        s.time += dt;
        s.rps += (target_rps - s.rps) * k_rps;
        s.load += (target_load - s.load) * k_load;
        const bool was_running = s.run_env > 0.5;
        s.run_env += ((in.running ? 1.0 : 0.0) - s.run_env) * k_run;
        const bool running = s.run_env > 0.5;
        if (running && !was_running) s.catch_boost = 1.0;
        if (was_running && !running) s.coasting = true;
        if (running || s.rps < 0.5 || target_rps > s.rps + 0.25) s.coasting = false;   // stopped, or a starter turning it
        // A starter turns the crank at cranking speed, never faster.
        const bool cranking = !running && !s.coasting && s.rps > 0.5 && s.rps < kIdleRps * 1.2;
        s.starter_env += ((cranking ? 1.0 : 0.0) - s.starter_env) * k_starter;
        s.catch_boost *= std::exp(-dt / 0.4);
        const double rpm01 = clamp01(s.rps / top);
        // How much idle character is left: all of it near the start speed, gone by ~2,000 rpm or under load.
        const double idle = std::pow(clamp01(1.0 - (s.rps - kIdleRps) / 22.0), 1.4) * (1.0 - 0.7 * s.load);
        s.lope_phase += s.lope_rate * dt;
        if (s.lope_phase >= 1.0) { s.lope_phase -= 1.0; s.lope_rate = (1.4 + 1.6 * (1.0 - t.cam)) * (0.8 + 0.4 * s.random01()); }
        const double swell = 0.5 - 0.5 * std::cos(two_pi * s.lope_phase);

        // Overrun: lifting off at speed starts a burst of pops (petrol only).
        s.load_peak = std::fmax(s.load, s.load_peak - 1.2 * dt);
        if (running && !diesel && t.crackle > 0.01 && s.load < 0.15 && s.load_peak > 0.5 && rpm01 > 0.3) {
            s.crackle_until = s.time + (0.5 + 1.2 * t.crackle) * (0.6 + 0.6 * rpm01);
            s.next_pop = s.time + 0.04;
            s.load_peak = 0;
        }
        const bool overrun = s.time < s.crackle_until;
        // Rev limiter: at the top under load the fuel is cut in bursts ("brrap-brrap").
        const bool at_limit = running && s.rps > top * 0.975 && s.load > 0.5;
        s.limit_phase += (at_limit ? 13.0 - 5.0 * t.diesel : 0.0) * dt;
        if (s.limit_phase >= 1.0) s.limit_phase -= 1.0;
        const bool cut = at_limit && s.limit_phase < 0.45;
        if (running && !diesel && ((overrun && s.time >= s.next_pop) || (cut && s.limit_phase < 0.01 && s.random01() < 0.5 * t.crackle + 0.2))) {
            s.pop_age = 0;
            s.pop_amp = (0.5 + 0.7 * s.random01()) * (0.5 + 0.5 * rpm01) * (0.5 + 0.8 * t.crackle) * muffle;
            if (overrun) s.next_pop = s.time + (0.02 + 0.22 * s.random01()) / (0.4 + t.crackle);
        }

        // Crank: fire each cylinder as the crank reaches it.
        // A big cam can't hold a steady idle: the revs hunt up and down with the lope.
        s.to_next -= s.rps * (1.0 + 0.14 * t.cam * idle * (swell - 0.5)) * 360.0 * dt;
        for (int guard = 0; guard < kMaxCylinders && s.to_next <= 0 && s.rps > 0.2; ++guard) {
            const int cyl = s.next;
            Bank& b = s.bank[s.table.bank[cyl]];
            b.age = 0;
            if (running && !cut) {
                double strength = 1.0 + s.table.bias[cyl] * (vee ? 2.2 : 1.0) * (1.0 + 2.0 * idle) + 0.35 * t.cam * idle * s.noise();
                const double lope = 1.0 - 0.55 * t.cam * idle * (1.0 - swell);
                const bool miss = s.random01() < 0.12 * t.cam * idle * (1.3 - swell);
                double drive = (0.45 + 0.55 * s.load) * (0.6 + 0.9 * rpm01) * (1.0 + 0.3 * idle);
                if (overrun) drive *= 0.5 + 0.5 * s.random01();
                b.amp = miss ? 0.06 : strength * lope * drive * (1.0 + 0.7 * s.catch_boost) * per_pulse;
                b.noise = miss ? 0.2 : 1.0;
                if (diesel && !miss) {
                    // The clatter: hardest at idle and when lugging under load, softer as it revs.
                    s.knock_age = 0;
                    s.knock_amp = (0.8 + 0.4 * s.random01()) * (0.7 + 0.5 * s.load) * (1.0 - 0.45 * rpm01) * per_pulse;
                }
                // Burble: on a V at idle and on the overrun each pulse rings out as a short, round tone, pitched by
                // its bank's pipe. The cross-plane's L R L L R R R L bank order gives it the V8 rhythm.
                const double burble = (vee ? 0.75 : layout == kBoxer ? 0.18 : 0.05) * (0.45 + t.cam) * (1.0 - t.diesel) *
                                      std::fmax(idle, std::fmax(overrun ? 0.8 : 0.0, 0.5 * (1.0 - s.load) * (1.0 - rpm01)));
                if (burble > 0.01) {
                    s.blat_age = 0;
                    s.blat_amp = b.amp * burble * (0.85 + 0.3 * s.random01());
                    s.blat_hz = pipe_hz * (s.table.bank[cyl] ? 1.25 : 1.6) * (1.0 + 0.03 * s.noise()) * (1.0 + 0.6 * rpm01);
                    s.blat_tau = std::fmin(0.035, 0.7 / std::fmax(s.rps * n / 2.0, 1.0));
                }
            } else if (running) {
                b.amp = 0.12 * per_pulse; b.noise = 0.3;     // fuel cut: just air
            } else if (cranking) {
                b.amp = 0.28 * clamp01(s.rps / kIdleRps) * per_pulse; b.noise = 0.35;   // cranking: compression puffs
            } else {
                b.amp = 0.12 * clamp01(s.rps / kIdleRps) * per_pulse; b.noise = 0.3;   // spinning down: just air, as on a fuel cut
            }
            // Each pulse lasts a fraction of the gap to the next; a muffler stretches it out.
            const double rate = s.rps * n / 2.0;
            b.tau = std::fmin(0.012, std::fmax(0.0010, (0.28 + 0.25 * t.muffler) / std::fmax(rate, 1.0))) * (1.0 - 0.25 * s.load);
            if (diesel) b.tau = std::fmin(0.02, b.tau * 1.8);   // slow-burning pulses: a softer, rounder chug
            // The gap to the next cylinder, with a little wander (a lot at a lumpy idle).
            s.next = (cyl + 1) % s.table.count;
            double gap = s.table.angle[s.next] - s.table.angle[cyl];
            if (gap <= 0) gap += 720.0;
            s.to_next += std::fmax(5.0, gap + 9.0 * t.cam * idle * s.noise() + 1.2 * s.noise());
        }
        if (s.to_next < -720.0) s.to_next = 0;   // stalled bookkeeping after a huge step: start over

        // Exhaust: each bank's pulse through its pipe and body resonances, with a low thump and a raspy edge.
        double v = 0, edge_in = 0, exc_sum = 0;
        for (int bi = 0; bi < 2; ++bi) {
            Bank& b = s.bank[bi];
            b.age += dt;
            const double a = b.age;
            const double env = a < 0.0005 ? a / 0.0005 : std::exp(-(a - 0.0005) / b.tau);
            const double exc = b.amp * env;
            b.thump += (exc - b.thump) * (1.0 - std::exp(-dt * two_pi * (140.0 + 120.0 * (1.0 - t.muffler))));
            const double side = bi ? (vee ? 0.72 : 0.88) : 1.0;   // the far bank quieter
            const bool in_line = layout == kInline;
            double bank_out = 0.55 * b.pipe.run(exc) + 0.22 * b.pipe2.run(exc) + (in_line ? 0.45 : 0.32) * b.body.run(exc) +
                              (in_line ? 0.55 : 0.95) * b.thump;
            // A V's headers are driven hard under load: the pulses saturate into a growl.
            if (vee) { const double drive = 1.0 + 3.5 * std::fmax(0.0, t.rasp - 0.3) * s.load; if (drive > 1.001) bank_out = std::tanh(drive * bank_out) / std::sqrt(drive); }
            v += side * bank_out;
            edge_in += exc * b.noise;
            exc_sum += exc;
        }
        const double nz = s.noise();
        const double edge = edge_in * nz * (0.15 + 0.85 * t.rasp) * (0.5 + 0.6 * s.load + 0.8 * rpm01) * (1.0 - 0.6 * idle) * muffle *
                            (diesel && vee ? 0.25 : 1.0);   // a truck diesel's exhaust is a smooth drone, not a rasp
        v += 0.85 * s.rasp.run(edge) + 0.35 * t.rasp * s.rasp_hi.run(edge);

        // Diesel exhaust: a deep rushing roar that builds with revs and load (no turbo whistle: real turbos come
        // from AnyEngineParts).
        if (diesel) {
            const double want = running ? rpm01 * (0.25 + 0.75 * s.load) : 0.0;
            s.spool += (want - s.spool) * (1.0 - std::exp(-dt / (want > s.spool ? 0.9 : 0.6)));
            const double flow = s.spool * s.spool;
            s.roar_lp += (exc_sum - s.roar_lp) * (1.0 - std::exp(-dt * two_pi * 25.0));
            v += 0.9 * (0.15 + 0.85 * flow) * (0.3 + 0.7 * s.load) * s.roar.run(s.noise() * (0.3 + 1.5 * s.roar_lp)) * muffle;
        }

        // Intake roar: breathing noise that swells with load and revs, chopped by the intake strokes.
        s.intake_env += (exc_sum - s.intake_env) * (1.0 - std::exp(-dt * two_pi * 60.0));
        if (running) v += 0.5 * t.intake * s.load * (0.2 + rpm01) * s.intake.run(s.noise() * (0.4 + 2.0 * s.intake_env));
        else s.intake.run(0);

        // Valvetrain: two valve closings per cylinder per cycle, a tick that grows with revs; a faint gear whine.
        s.tick_phase += s.rps * n * dt;
        if (s.tick_phase >= 1.0) { s.tick_phase -= 1.0; s.tick_age = 0; }
        s.tick_age += dt;
        const double tick_env = s.tick_age < 0.0004 ? 1.0 : 0.0;
        v += (0.04 + 0.1 * t.mech) * (0.3 + 0.7 * rpm01) * s.tick.run(tick_env * s.noise());
        s.whine_phase += two_pi * s.rps * 7.0 * dt;
        if (s.whine_phase > two_pi) s.whine_phase -= two_pi;
        v += 0.03 * t.mech * rpm01 * rpm01 * std::sin(s.whine_phase);

        // Diesel knock: the sharp clatter of each combustion, loudest at idle.
        s.knock_age += dt;
        if (s.knock_age < 0.03) {
            // The pressure spike strikes the block like a hammer: a short knock that rings out, not a hiss.
            const double ke = s.knock_amp * (s.knock_age < 0.0003 ? 1.0 : 0.0) + 0.08 * s.knock_amp * std::exp(-s.knock_age / 0.002) * s.noise();
            v += (vee ? 0.4 : 0.32) * t.diesel * (0.9 * s.knock.run(ke) + 0.6 * s.knock_mid.run(ke) + 0.3 * s.knock_hi.run(ke));
        } else {
            s.knock.run(0); s.knock_mid.run(0); s.knock_hi.run(0);
        }

        // Burble tone: round and chunky, dropping in pitch as the pulse empties.
        s.blat_age += dt;
        if (s.blat_age < 0.2 && s.blat_amp > 0) {
            const double ba = s.blat_age;
            const double be = (ba < 0.0015 ? ba / 0.0015 : std::exp(-(ba - 0.0015) / s.blat_tau)) * s.blat_amp;
            s.blat_phase += two_pi * s.blat_hz * (1.0 - 0.2 * (1.0 - std::exp(-ba / 0.025))) * dt;
            if (s.blat_phase > two_pi) s.blat_phase -= two_pi;
            v += 1.4 * be * (std::tanh(1.6 * std::sin(s.blat_phase)) + 0.35 * std::sin(2.0 * s.blat_phase + 0.6));
        }

        // Pops: a sharp crack and a short boom.
        s.pop_age += dt;
        if (s.pop_age < 0.06) {
            const double pe = s.pop_age < 0.0004 ? s.pop_age / 0.0004 : std::exp(-(s.pop_age - 0.0004) / 0.006);
            const double pop = s.pop_amp * pe;
            v += 0.9 * s.pop_body.run(pop) + 0.5 * pop * s.noise();
        } else {
            s.pop_body.run(0);
        }

        // Starter motor while it turns the engine over.
        if (s.starter_env > 1e-4) {
            s.starter_phase += two_pi * (95.0 + 12.0 * s.rps) * dt;
            if (s.starter_phase > two_pi) s.starter_phase -= two_pi;
            v += 0.06 * clamp01(s.rps / 2.0) * (std::sin(s.starter_phase) + 0.4 * std::sin(3.0 * s.starter_phase)) * (1.0 - s.run_env) * s.starter_env;
        }

        // Muffler: rounds off the top end; a straight pipe keeps it all.
        double cutoff = (1300.0 + 9000.0 * (1.0 - t.muffler)) * (1.0 + 0.6 * rpm01);
        if (diesel && vee) cutoff = std::fmin(cutoff, 900.0 + 700.0 * rpm01);   // big, heavy and deep: nothing shrill
        const double k = 1.0 - std::exp(-dt * two_pi * cutoff);
        s.soft += (v - s.soft) * k;
        s.soft2 += (s.soft - s.soft2) * k;
        // Many small pulses at high revs land above the pipe's boom; keep big engines as loud at the top.
        v = s.soft2 * t.gain * muffle * 1.4 * std::pow(n / 4.0, 0.35 * rpm01);
        v = std::isfinite(v) ? v : 0;
        s.dc_y = v - s.dc_x + 0.9967 * s.dc_y;   // ~25 Hz high-pass: no DC from the one-sided pulses
        s.dc_x = v;
        v = std::isfinite(s.dc_y) ? s.dc_y : (s.dc_y = 0);
        out[i] = int16_t(std::lround(std::tanh(v * 1.1) * 29000.0));
    }
}

}  // namespace synth
}  // namespace enginesound
