#pragma once
// Rotary engine sound, synthesized: the exhaust pulses of every rotor face firing, through the exhaust's
// resonances. No game or Windows dependencies, so tests/rotary_logic_test.cpp renders it on any platform (and
// writes the preview WAVs).
//
// A rotary fires once per rotor per eccentric-shaft turn, so the pulse rate is rps x rotors: 6 rps (the game's
// start speed) on a 2-rotor is 12 pulses a second, a lumpy "brap brap brap"; at 8000 rpm it is 267 a second, a
// smooth buzzing scream. At idle the pulses vary in strength and timing, some miss, and they bunch into a lope
// (the bridge port's overlap); load and revs clean that up. Lifting off at speed pops and crackles. Below the
// start speed the starter turns it over: soft compression puffs, no combustion.
//
// The game streams it like the Turbo mod's sounds: 48 kHz mono 16-bit PCM into a positional voice effect.
// The ports (the engine's own "Sound effect" row): 0 bridge port (default), 1 street port, 2 peripheral port.
#include "rotary_logic.h"
#include <cmath>
#include <cstdint>

namespace rotary {

constexpr int kSampleRate = 48000;

// Band-pass biquad (RBJ, constant skirt gain: peak gain = Q).
struct Resonator {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(double hz, double q) {
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

struct Character {
    double pipe_hz, pipe_q;       // exhaust pipe boom
    double body_hz, body_q;       // mid "bap"
    double rasp_hz, rasp_gain;    // raspy edge of each pulse
    double jitter;                // pulse-to-pulse strength and timing variation at idle
    double misfire;               // chance a pulse misses at idle
    double lope;                  // depth of the idle lope
    double lope_hz;               // lope rate
    double idle_span;             // rps above the start speed where the idle character has gone
    double crackle;               // overrun pops a second
    double gain;
    double blat;                  // strength of the idle blat
    double blat_ratio;            // its pitch over pipe_hz
};
inline Character character(Port p) {
    switch (p) {
    case kStreetPort: return {112, 3.2, 390, 2.5, 2200, 0.30, 0.10, 0.01, 0.22, 3.0, 14, 5, 0.85, 0.7, 1.05};
    case kPeripheralPort: return {70, 5.0, 300, 2.2, 2700, 0.85, 0.40, 0.14, 0.85, 2.4, 30, 16, 1.10, 0.8, 1.05};
    default: return {84, 3.6, 330, 2.5, 1900, 0.55, 0.26, 0.07, 0.75, 2.7, 24, 11, 1.0, 0.75, 1.0};
    }
}

struct Input {
    double rps = 0;        // eccentric shaft speed
    double load = 0;       // the engine's power factor, 0-1
    bool running = false;  // combustion on (the game plays its engine sound)
    int rotors = 2;
    Port port = kBridgePort;
};

struct Synth {
    // smoothed inputs
    double rps = 0, load = 0, run_env = 0;
    // firing
    double phase = 0, period_scale = 1, pulse_age = 1, pulse_amp = 0, pulse_tau = 0.004, noise_amp = 0;
    int rotor = 0;
    double lope_phase = 0, lope_rate = 2.7;
    // overrun
    double load_peak = 0, crackle_until = -1, next_pop = 0, pop_age = 1, pop_amp = 0, time = 0;
    double catch_boost = 0;
    double thump = 0, whirr_phase = 0;
    // idle "blat": the reedy tone each pulse rings with at low revs
    double blat_phase = 0, blat_hz = 100, blat_amp = 0, blat_age = 1, blat_tau = 0.03;
    double dc_x = 0, dc_y = 0, soft = 0;   // DC blocker (the pulses are one-sided)
    uint32_t rng = 0x2545f491u;
    Resonator pipe, body, rasp, rasp_hi, pop_body, whirr;
    Port tuned = kPortCount;
    double noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return double(rng) / 2147483648.0 - 1.0; }
    double random01() { return 0.5 + 0.5 * noise(); }
};

inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Per-rotor strength: no two rotors fire quite alike, which gives the rotary its rough buzz at the shaft rate.
inline double rotor_bias(int rotors, int r) {
    static const double b[4][4] = {{0, 0, 0, 0}, {0.09, -0.09, 0, 0}, {0.07, -0.1, 0.03, 0}, {0.06, -0.08, 0.08, -0.06}};
    int n = rotors < 1 ? 1 : rotors > 4 ? 4 : rotors;
    return b[n - 1][r % n];
}

inline void render(Synth& s, const Input& in, int16_t* out, int count) {
    const Character c = character(in.port);
    if (s.tuned != in.port) {
        s.pipe.set(c.pipe_hz, c.pipe_q); s.body.set(c.body_hz, c.body_q); s.rasp.set(c.rasp_hz, 1.1);
        s.rasp_hi.set(c.rasp_hz * 1.9, 1.4); s.pop_body.set(c.pipe_hz * 1.6, 2.0); s.whirr.set(950, 1.5);
        s.tuned = in.port;
    }
    const int rotors = in.rotors < kMinRotors ? kMinRotors : in.rotors > kMaxRotors ? kMaxRotors : in.rotors;
    const double dt = 1.0 / kSampleRate, two_pi = 6.283185307179586;
    const double target_rps = std::isfinite(in.rps) && in.rps > 0 ? (in.rps > kMaxRps ? kMaxRps : in.rps) : 0;
    const double target_load = std::isfinite(in.load) ? clamp01(in.load) : 0;
    const double k_rps = 1.0 - std::exp(-dt / 0.06), k_load = 1.0 - std::exp(-dt / 0.04), k_run = 1.0 - std::exp(-dt / 0.03);
    for (int i = 0; i < count; ++i) {
        s.time += dt;
        s.rps += (target_rps - s.rps) * k_rps;
        s.load += (target_load - s.load) * k_load;
        bool was_running = s.run_env > 0.5;
        s.run_env += ((in.running ? 1.0 : 0.0) - s.run_env) * k_run;
        bool running = s.run_env > 0.5;
        if (running && !was_running) s.catch_boost = 1.0;   // it catches: the first pulses bark
        s.catch_boost *= std::exp(-dt / 0.35);
        const double rpm01 = s.rps / kMaxRps;
        // How much of the idle character is left: all of it at the start speed, gone by idle_span rps above, and
        // load cleans it up too.
        const double idle = std::pow(clamp01(1.0 - (s.rps - kStartRps) / c.idle_span), 1.5) * (1.0 - 0.75 * s.load);
        // Lope: a slow swell the pulses bunch into ("brap ... brap ... brap").
        s.lope_phase += s.lope_rate * dt;
        if (s.lope_phase >= 1.0) { s.lope_phase -= 1.0; s.lope_rate = c.lope_hz * (0.8 + 0.4 * s.random01()); }
        const double swell = 0.5 - 0.5 * std::cos(two_pi * s.lope_phase);   // 0 trough, 1 crest

        // Overrun: lifting off at speed starts a burst of pops.
        s.load_peak = std::fmax(s.load, s.load_peak - 1.2 * dt);
        if (running && s.load < 0.15 && s.load_peak > 0.5 && rpm01 > 0.28) {
            s.crackle_until = s.time + 0.9 + 0.8 * rpm01;
            s.next_pop = s.time + 0.05;
            s.load_peak = 0;
        }
        if (running && s.time < s.crackle_until && s.time >= s.next_pop) {
            s.pop_age = 0;
            s.pop_amp = (0.6 + 0.6 * s.random01()) * (0.5 + 0.5 * rpm01);
            s.next_pop = s.time + (0.3 + 1.4 * s.random01()) / c.crackle;
        }

        // Pulses: one per rotor per shaft turn while it turns.
        double rate = s.rps * rotors;
        if (rate > 0.2) {
            s.phase += rate * dt / s.period_scale;
            if (s.phase >= 1.0) {
                s.phase -= 1.0;
                s.rotor = (s.rotor + 1) % rotors;
                s.pulse_age = 0;
                s.period_scale = 1.0 + c.jitter * 0.35 * idle * s.noise();   // timing wander at idle
                if (running) {
                    double strength = 1.0 + rotor_bias(rotors, s.rotor) + c.jitter * idle * s.noise();
                    double lope_gain = 1.0 - c.lope * idle * (1.0 - swell);
                    bool miss = s.random01() < c.misfire * idle * (1.4 - swell);
                    double drive = (0.7 + 0.3 * s.load) * (0.8 + 0.2 * rpm01) * (1.0 + 0.6 * idle);
                    if (s.time < s.crackle_until) drive *= 0.6 + 0.6 * s.random01();   // unburnt, uneven on overrun
                    s.pulse_amp = miss ? 0.08 : strength * lope_gain * drive * (1.0 + 0.8 * s.catch_boost);
                    s.noise_amp = miss ? 0.25 : 1.0;
                } else {
                    s.pulse_amp = 0.3 * clamp01(s.rps / kStartRps);   // cranking: compression puffs only
                    s.noise_amp = 0.4;
                }
                // Each pulse lasts about a third of the gap to the next, shorter under load.
                s.pulse_tau = std::fmin(0.010, std::fmax(0.0012, 0.33 / rate)) * (1.0 - 0.3 * s.load);
                // At idle each pulse also rings out as a short reedy blat that fills most of the gap: the "brap".
                s.blat_age = 0;
                s.blat_amp = s.pulse_amp * (0.85 + 0.3 * s.random01());
                s.blat_hz = c.pipe_hz * c.blat_ratio * (1.0 + 0.04 * s.noise()) * (1.0 + 0.5 * rpm01);
                s.blat_tau = std::fmin(0.045, 0.55 / rate);
            }
        }
        s.pulse_age += dt;
        const double a = s.pulse_age;
        const double env = a < 0.0006 ? a / 0.0006 : std::exp(-(a - 0.0006) / s.pulse_tau);
        const double exc = s.pulse_amp * env;
        const double nz = s.noise();
        // The pressure pulse through the pipe and body resonances, its low thump, and the rasp of the port.
        s.thump += (exc - s.thump) * (1.0 - std::exp(-dt * 6.283185307179586 * 180.0));
        double v = 0.55 * s.pipe.run(exc) + 0.35 * s.body.run(exc) + 0.9 * s.thump;
        const double edge = exc * nz * c.rasp_gain * s.noise_amp * (0.6 + 0.6 * s.load + 0.9 * rpm01) * (1.0 - 0.8 * idle);
        v += 0.9 * s.rasp.run(edge) + 0.45 * s.rasp_hi.run(edge);
        // The idle blat: a squarish tone that drops in pitch as the pulse empties, gone once the revs smooth out.
        s.blat_age += dt;
        if (idle > 0.001 && s.blat_age < 0.25) {
            const double ba = s.blat_age;
            const double be = (ba < 0.0015 ? ba / 0.0015 : std::exp(-(ba - 0.0015) / s.blat_tau)) * s.blat_amp;
            s.blat_phase += two_pi * s.blat_hz * (1.0 - 0.22 * (1.0 - std::exp(-ba / 0.03))) * dt;
            if (s.blat_phase > two_pi) s.blat_phase -= two_pi;
            const double tone = std::tanh(1.8 * std::sin(s.blat_phase)) + 0.4 * std::sin(2.0 * s.blat_phase + 0.6) +
                                0.18 * std::sin(3.0 * s.blat_phase + 1.1);   // round and chunky, not buzzy
            v = v * (1.0 - 0.45 * idle) + c.blat * idle * be * tone;
        }
        // Overrun pops: a sharp crack and a short boom.
        s.pop_age += dt;
        if (s.pop_age < 0.06) {
            double pe = s.pop_age < 0.0004 ? s.pop_age / 0.0004 : std::exp(-(s.pop_age - 0.0004) / 0.006);
            double pop = s.pop_amp * pe;
            v += 0.9 * s.pop_body.run(pop) + 0.5 * pop * s.noise();
        } else {
            s.pop_body.run(0);
        }
        // The eccentric shaft and gears: a faint whirr that rises with speed.
        v += 0.05 * clamp01(rpm01 * 1.5) * s.whirr.run(s.noise());
        s.soft += (v - s.soft) * (1.0 - std::exp(-dt * two_pi * (1100.0 + 9000.0 * (1.0 - idle))));
        v = s.soft;
        v *= c.gain * (1.0 + 0.12 * idle);   // the idle a touch louder; the rev untouched
        v = std::isfinite(v) ? v : 0;
        s.dc_y = v - s.dc_x + 0.9967 * s.dc_y;   // ~25 Hz high-pass: no DC from the one-sided pulses
        s.dc_x = v;
        v = std::isfinite(s.dc_y) ? s.dc_y : (s.dc_y = 0);
        out[i] = int16_t(std::lround(std::tanh(v * 1.1) * 29000.0));   // soft clip: big pulses round off, never crack
    }
}

}  // namespace rotary
