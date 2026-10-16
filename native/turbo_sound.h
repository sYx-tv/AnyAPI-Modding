#pragma once
// Turbo sounds, synthesized: spool whistle, compressor surge puffs, four blow-off valve sounds, antilag bangs and the
// superchargers' gear whine. No game or Windows
// dependencies, so tests/turbo_logic_test.cpp renders them on any platform.
//
// The game plays voice chat by streaming 48 kHz mono 16-bit PCM into a positional voice effect (application.audio
// play_voice + effect_3d.append_audio_data; its Opus decoder writes 960-sample frames). The mod streams these
// sounds the same way, one voice per turbo and per blow-off valve, on the client. The server sends what each
// client needs through the impeller's existing replicated speed factor (see encode_turbo_factor), so every
// player hears them.
#include <cmath>
#include <cstdint>

namespace turbo {

constexpr int kSampleRate = 48000;

// The impeller's replicated speed factor (0-1, sent as one byte, so steps of 1/255) carries a turbo's shaft speed (over
// its full-boost speed), surge and whether it has blown: bytes 0-124 are the speed, 127 is a blown turbo (126-128 read
// as blown, whichever way the game rounds), and 0.51-1 is the speed while surging. A blow-off valve sends 0 when shut, else 0.5-1 for how hard it vents.
// A supercharger sends the same as a turbo with the engine's speed (blower_sound_level) in place of the spool: it never blows.
inline double encode_turbo_factor(double spool, bool surging, bool blown = false) {
    if (blown) return 127.0 / 255.0;
    double s = std::isfinite(spool) ? (spool < 0 ? 0 : spool > 1 ? 1 : spool) : 0;
    return surging ? 0.51 + 0.49 * s : s * 124.0 / 255.0;
}
inline void decode_turbo_factor(double factor, double& spool, bool& surging, bool& blown) {
    double f = std::isfinite(factor) ? (factor < 0 ? 0 : factor > 1 ? 1 : factor) : 0;
    surging = f > 0.505;
    blown = !surging && f * 255.0 > 125.5;
    spool = surging ? (f - 0.51) / 0.49 : blown ? 0.0 : f * 255.0 / 124.0;
    spool = spool < 0 ? 0 : spool > 1 ? 1 : spool;
}
inline void decode_turbo_factor(double factor, double& spool, bool& surging) { bool blown; decode_turbo_factor(factor, spool, surging, blown); }
// Blow-off sounds the valve can make, picked in the Properties Tool ("Sound effect").
enum BlowOffSound { kWhoosh = 0, kFlutter = 1, kChirp = 2, kDump = 3, kBlowOffSoundCount = 4 };
// A blow-off valve sends 0 when shut. Open, it sends one byte b = 51 * (sound + 1) + sub, where sub carries the
// size of the turbo venting through it and how hard (10 levels): sub = 12 * (size - 2) + 1 + level. The spare
// step either side keeps it readable if the game rounds the byte one way or the other.
inline double encode_blow_off_factor(bool open, double intensity, int sound = kWhoosh, int size = 3) {
    if (!open) return 0.0;
    double i = std::isfinite(intensity) ? (intensity < 0 ? 0 : intensity > 1 ? 1 : intensity) : 0;
    int k = sound < 0 || sound >= kBlowOffSoundCount ? 0 : sound;
    int z = size < 2 ? 2 : size > 5 ? 5 : size;
    int level = int(i * 9.0 + 0.5);
    return (51.0 * (k + 1) + 12 * (z - 2) + 1 + level) / 255.0;
}
inline void decode_blow_off_factor(double factor, bool& open, double& intensity, int& sound, int& size) {
    double f = std::isfinite(factor) ? (factor < 0 ? 0 : factor > 1 ? 1 : factor) : 0;
    int b = int(f * 255.0 + 0.5);
    open = b >= 26;
    if (!open) { intensity = 0; return; }                 // shut keeps the last sound and size for the tail
    int k = b / 51 - 1, sub = b % 51;
    sound = k < 0 ? 0 : k >= kBlowOffSoundCount ? kBlowOffSoundCount - 1 : k;
    int z = 2 + sub / 12;
    size = z > 5 ? 5 : z;
    int level = sub % 12 - 1;
    intensity = (level < 0 ? 0 : level > 9 ? 9 : level) / 9.0;
}

// RBJ band-pass biquad (constant 0 dB peak gain).
struct BandPass {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(double centre_hz, double q) {
        double w = 2.0 * 3.14159265358979 * centre_hz / kSampleRate, alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = alpha / a0; b2 = -alpha / a0; a1 = -2.0 * std::cos(w) / a0; a2 = (1.0 - alpha) / a0;
    }
    double run(double x) {
        double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        if (!std::isfinite(y)) { x1 = x2 = y1 = y2 = 0; y = 0; }
        return y;
    }
};

// State-variable band-pass whose centre can move every sample (for the surge chirps).
struct Sweep {
    double ic1 = 0, ic2 = 0;
    double run(double x, double centre_hz, double q) {
        double g = std::tan(3.14159265358979 * centre_hz / kSampleRate), k = 1.0 / q;
        double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1;
        double v1 = a1 * ic1 + a2 * (x - ic2), v2 = ic2 + g * v1;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        if (!std::isfinite(v1)) { ic1 = ic2 = 0; return 0; }
        return v1;
    }
};

// A wastegate firing antilag sends the same byte as a venting whoosh valve (encode_blow_off_factor with kWhoosh):
// firing, how hard, and the turbo's size.
enum class Voice { kTurbo, kBlowOff, kAntilag, kSupercharger };

// A supercharger's voice follows the engine closely: the whine of its step-up gears (straight cut, a buzzy tone
// with harmonics that wavers with the gear mesh), its impeller's thin whistle above it and the air it gulps. The
// level it gets is its pulley's speed over the fastest a belt turns it (the large Engine Wheel at the engine's
// 8,000 RPM fuel cut); on the medium wheel the fuel cut is kBlowerMediumCut. The large one sings lower.
constexpr double kBlowerSoundRps = 8000.0 / 60.0 * 6.675;
constexpr double kBlowerMediumCut = 3.875 / 6.675;
inline double blower_sound_level(double pulley_rps) {
    double l = std::isfinite(pulley_rps) ? std::abs(pulley_rps) / kBlowerSoundRps : 0;
    return l > 1 ? 1 : l;
}
struct BlowerVoice {
    double gear_base, gear_span;       // gear whine pitch at rest and the rise to the fuel cut (Hz)
    double whistle_base, whistle_span; // impeller whistle
    double gear_gain, whistle_gain, air_gain;
};
inline BlowerVoice blower_voice(bool large) {
    return large ? BlowerVoice{460, 5900, 1900, 8800, 0.20, 0.05, 0.12} : BlowerVoice{650, 7900, 2700, 11700, 0.18, 0.05, 0.09};
}

// Each turbo size has its own voice: the 2x2 screams high and puffs fast, the 5x5 (and the 6x6) whooshes deep and slow.
struct Character {
    double base, span;                 // whistle pitch at rest and the rise to full spool (Hz)
    double overtone, overtone_gain;    // second whistle partial (ratio of the pitch)
    double air_gain;                   // breathy whoosh that follows the whistle (big turbos move more air)
    double hiss_hz, hiss_gain;         // intake hiss
    double puff_pitch;                 // surge puff pitch (x the 3x3's)
    double puff_fast, puff_slow;       // surge puffs a second, at the start and as boost runs out
    double puff_decay, puff_gain;      // surge puff length (s) and loudness
};
inline Character character(int size) {
    switch (size) {
    case 2: return {2400, 7400, 2.0, 0.10, 0.05, 6500, 0.14, 1.30, 22, 11, 0.022, 1.00};
    // Nate's ears (2026-10-13): the big ones were too deep. The 4x4 sits halfway between the 3x3 and the old
    // 4x4, and the 5x5 just past the old 4x4.
    case 4: return {1275, 5000, 1.5, 0.10, 0.13, 4700, 0.17, 0.90, 16.5, 8.25, 0.032, 1.28};
    case 5:
    case 6: return { 945, 4015, 1.5, 0.127, 0.19, 3920, 0.19, 0.74, 14, 7, 0.039, 1.40};   // the 6x6 keeps the 5x5's voice
    default: return {1500, 5600, 1.5, 0.08, 0.10, 5200, 0.16, 1.00, 18, 9, 0.028, 1.20};
    }
}

struct Synth {
    Voice voice = Voice::kTurbo;
    int size = 3;                      // turbo size 2-6 (see character())
    int sound = kWhoosh;               // blow-off valve sound (BlowOffSound)
    int tuned_size = 0;                // size the filters were set for
    // smoothed state
    double spool = 0, pitch = 0, phase1 = 0, phase2 = 0;
    double surge_env = 0, surge_age = 0, surge_clock = 1, pulse_age = 1, pulse_gain = 0, pulse_period = 0.07;
    double whoosh_env = 0;
    double flutter_phase = 0, flutter_rate = 32, flutter_depth = 0.6, pop_age = 1, open_age = 0;
    double bang_env = 0, bang_clock = 1, bang_age = 1, bang_gain = 0, bang_period = 0.1, bang_pitch = 90, thump_phase = 0, crackle_age = 1;
    bool was_open = false;
    // Blowing up (turbo voice): once, when a turbo the client has been hearing for a while goes from working to blown.
    // A turbo that is already blown when first heard (loaded, or driven into range) stays quiet.
    bool blown_known = false, was_blown = false;
    double heard = 0, blow_age = 1e9, blow_pitch = 0, screech_phase = 0, boom_phase = 0;
    double gear_phase = 0, mesh_phase = 0, gear_pitch = 0, glide = 0;   // supercharger gear whine
    struct Ping { double age = 1e9, f = 0, decay = 0, gain = 0, p1 = 0, p2 = 0; } pings[6];
    uint32_t rng = 0x12345678u;
    BandPass hiss, click, whoosh, whoosh_low, rumble, bang_body, bang_low, crackle, blow_crack, blow_low, blow_smoke;
    Sweep chirp, chirp_low, air, bov_chirp;
    bool init = false;
    double noise() {                   // white noise, -1..1
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return double(rng) / 2147483648.0 - 1.0;
    }
};

// Blowing up: a sharp crack and a deep boom, shrapnel pinging round the housing, the wheel screeching to a stop and
// a hiss of boost and oil smoke that dies away over a couple of seconds. Bigger turbos boom deeper and longer.
constexpr double kBlowSeconds = 3.0;
inline double blow_sample(Synth& s, double bov_pitch, double bov_length, double dt) {
    const double two_pi = 6.283185307179586, t = s.blow_age;
    if (t > kBlowSeconds) return 0;
    double nz = s.noise();
    double crack = t < 0.0006 ? t / 0.0006 : std::exp(-(t - 0.0006) / 0.014);
    s.boom_phase += two_pi * 52.0 * bov_pitch * (1.0 + 2.2 * std::exp(-t / 0.025)) * dt;
    if (s.boom_phase > two_pi) s.boom_phase -= two_pi;
    double boom = (t < 0.003 ? t / 0.003 : 1.0) * std::exp(-t / (0.22 * bov_length));
    double v = 2.6 * crack * (0.6 * nz + 1.8 * s.blow_crack.run(nz)) + 2.2 * boom * (std::sin(s.boom_phase) + 0.9 * s.blow_low.run(nz));
    // Shrapnel: bright pings, thick at first, thinning out.
    if (0.5 + 0.5 * s.noise() < 70.0 * std::exp(-t / 0.18) * dt)
        for (auto& p : s.pings)
            if (p.age > 6 * p.decay) {
                p.age = 0; p.f = 2400.0 + 4200.0 * (0.5 + 0.5 * s.noise()); p.decay = 0.03 + 0.06 * (0.5 + 0.5 * s.noise());
                p.gain = (0.25 + 0.35 * (0.5 + 0.5 * s.noise())) * (0.4 + 0.6 * std::exp(-t / 0.3)); p.p1 = p.p2 = 0;
                break;
            }
    for (auto& p : s.pings) {
        if (p.age > 6 * p.decay) continue;
        p.p1 += two_pi * p.f * dt; p.p2 += two_pi * p.f * 2.76 * dt;
        if (p.p1 > two_pi) p.p1 -= two_pi;
        if (p.p2 > two_pi) p.p2 -= two_pi;
        v += p.gain * std::exp(-p.age / p.decay) * (std::sin(p.p1) + 0.5 * std::sin(p.p2));
        p.age += dt;
    }
    // The wheel: its whistle drops away fast, grinding.
    double f = s.blow_pitch * (0.12 + 0.88 * std::exp(-t / 0.22));
    s.screech_phase += two_pi * f * dt;
    if (s.screech_phase > two_pi) s.screech_phase -= two_pi;
    double grind = 0.6 + 0.4 * s.noise();
    v += 0.45 * std::exp(-t / 0.5) * grind * std::sin(s.screech_phase);
    // Boost and smoke hissing out.
    double smoke = (t < 0.03 ? t / 0.03 : 1.0) * std::exp(-t / (0.8 * bov_length));
    v += 0.9 * smoke * s.blow_smoke.run(nz);
    return v;
}

// Renders n samples. Turbo voice: level = spool 0-1, flag = surging, blown = the turbo has blown (the first time it
// turns true it plays the blow-up). Supercharger voice: level = blower_sound_level, flag = surging; size 2 is the
// small, 4 the large. Blow-off voice: flag = venting, level = how hard (0-1), in the synth's sound, voiced for the
// turbo size venting through it.
inline void render(Synth& s, double level, bool flag, int16_t* out, int n, bool blown = false) {
    const Character c = character(s.size);
    // A blow-off valve on a bigger turbo dumps more air: lower, longer and, for the flutter, slower.
    const double bov_pitch = s.size <= 2 ? 1.25 : s.size == 4 ? 0.91 : s.size >= 5 ? 0.77 : 1.0;
    const double bov_length = s.size <= 2 ? 0.75 : s.size == 4 ? 1.12 : s.size >= 5 ? 1.34 : 1.0;
    if (!s.init || s.tuned_size != s.size) {
        s.hiss.set(c.hiss_hz, 0.9); s.click.set(6500.0, 0.7);
        s.whoosh.set(3600.0 * bov_pitch, 0.7); s.whoosh_low.set(1400.0 * bov_pitch, 0.8); s.rumble.set(500.0 * bov_pitch, 0.8);
        s.bang_body.set(1500.0 * bov_pitch, 0.6); s.bang_low.set(260.0 * bov_pitch, 0.9); s.crackle.set(4200.0, 0.8);
        s.blow_crack.set(2200.0 * bov_pitch, 0.5); s.blow_low.set(140.0 * bov_pitch, 0.8); s.blow_smoke.set(2600.0 * bov_pitch, 0.6);
        s.init = true; s.tuned_size = s.size;
    }
    if (s.voice == Voice::kTurbo) {
        if (!s.blown_known) { s.blown_known = true; s.was_blown = blown; }
        if (blown && !s.was_blown && s.heard > 1.0) { s.blow_age = 0; s.blow_pitch = s.pitch > 0 ? s.pitch : c.base; }
        s.was_blown = blown;
        s.heard += double(n) / kSampleRate;
    }
    double target = std::isfinite(level) ? (level < 0 ? 0 : level > 1 ? 1 : level) : 0;
    const double dt = 1.0 / kSampleRate, two_pi = 6.283185307179586;
    const bool blower = s.voice == Voice::kSupercharger;
    const BlowerVoice bv = blower_voice(s.size >= 4);
    if (blower) blown = false;
    // A supercharger's level arrives in steps (one byte, at the network's pace) and its pitch spans thousands of Hz,
    // so each step was a jump you could hear. Two gentle stages turn the steps into one smooth slide (about 0.1 s
    // behind the engine). A turbo's spool already moves smoothly.
    const double k_glide = 1.0 - std::exp(-dt / 0.04), k_spool = 1.0 - std::exp(-dt / 0.05);
    for (int i = 0; i < n; ++i) {
        double v = 0;
        if (s.voice == Voice::kTurbo || blower) {
            if (blower) {
                s.glide += (target - s.glide) * k_glide;
                s.spool += (s.glide - s.spool) * k_glide;
            } else {
                s.spool += (target - s.spool) * k_spool;
            }
            // Spool whistle: a whine that rises with shaft speed, the air rushing through, and intake hiss.
            double f = blower ? bv.whistle_base + bv.whistle_span * s.spool : c.base + c.span * s.spool;
            s.pitch = s.pitch <= 0 ? f : s.pitch + (f - s.pitch) * 0.002;
            // Surge ("stu-stu-stu"): boost slams back through the compressor in quick puffs. Each puff is a burst of
            // air: a sharp "t" then a breathy "u" that falls in pitch, like a short blow-off. The puffs come fast
            // and close together, then slow and fade as the boost runs out. The whistle keeps winding down
            // underneath and ducks a little on each puff.
            s.surge_env += ((flag ? 1.0 : 0.0) - s.surge_env) * (1.0 - std::exp(-dt / (flag ? 0.01 : 0.15)));
            s.surge_age = flag ? s.surge_age + dt : 0.0;
            s.surge_clock += dt;
            if (s.surge_env > 0.05 && s.surge_clock >= s.pulse_period) {
                s.surge_clock = 0;
                s.pulse_age = 0;
                double fade = std::exp(-s.surge_age / 0.9);
                s.pulse_gain = s.surge_env * (0.35 + 0.65 * fade) * (0.85 + 0.3 * (0.5 + 0.5 * s.noise()));
                double rate = c.puff_slow + (c.puff_fast - c.puff_slow) * fade;
                s.pulse_period = (1.0 / rate) * (0.9 + 0.2 * (0.5 + 0.5 * s.noise()));
            }
            s.pulse_age += dt;
            double pa = s.pulse_age;
            double body = pa < 0.004 ? pa / 0.004 : std::exp(-(pa - 0.004) / c.puff_decay);   // "u": breath of air
            double tick = std::exp(-pa / 0.0025);                                             // "t": the slam
            double centre = c.puff_pitch * (900.0 + 2600.0 * std::exp(-pa / (0.7 * c.puff_decay)));   // falls
            double nz = s.noise();
            double puff = 0.9 * s.chirp.run(nz, centre, 2.2) + 0.5 * s.chirp_low.run(nz, centre * 0.5, 1.4);
            double puffs = s.pulse_gain * (body * puff + 0.35 * tick * s.click.run(s.noise()));
            s.phase1 += two_pi * s.pitch * dt;
            s.phase2 += two_pi * s.pitch * c.overtone * dt;
            if (s.phase1 > two_pi) s.phase1 -= two_pi;
            if (s.phase2 > two_pi) s.phase2 -= two_pi;
            // A turbo's level is its shaft speed (the whistle's pitch follows it); its loudness follows the boost it
            // makes, which goes with the speed squared.
            double loud = std::pow(s.spool, 3.2) * (1.0 - 0.35 * s.surge_env) * (1.0 - 0.4 * s.pulse_gain * body);
            if (blower) {
                // The gears: a buzzy whine (three harmonics) that wavers at the gear mesh rate, there from idle up.
                double fg = bv.gear_base + bv.gear_span * s.spool;
                s.gear_pitch = s.gear_pitch <= 0 ? fg : s.gear_pitch + (fg - s.gear_pitch) * 0.01;
                s.gear_phase += two_pi * s.gear_pitch * dt;
                s.mesh_phase += two_pi * s.gear_pitch / 17.0 * dt;
                if (s.gear_phase > two_pi) s.gear_phase -= two_pi;
                if (s.mesh_phase > two_pi) s.mesh_phase -= two_pi;
                double g = s.gear_phase;
                double whine = std::sin(g) + 0.45 * std::sin(2 * g + 0.4) + 0.22 * std::sin(3 * g + 1.1);
                // As loud as it gets by the medium wheel's fuel cut; overdriven it only climbs in pitch. A faint
                // whine from idle up, fading to silence as the pulley stops (below about 300 engine RPM on the
                // medium wheel), so a supercharger at rest makes no sound.
                double drive = std::min(1.0, s.spool / kBlowerMediumCut);
                double turning = std::min(1.0, drive / 0.04);
                double gear_loud = turning * std::pow(0.06 + 0.94 * drive, 1.3) * (1.0 + 0.22 * std::sin(s.mesh_phase)) * (1.0 - 0.3 * s.surge_env);
                double air_loud = std::pow(drive, 1.6) * (1.0 - 0.35 * s.surge_env) * (1.0 - 0.4 * s.pulse_gain * body);
                v += bv.gear_gain * gear_loud * whine;
                v += air_loud * bv.whistle_gain * (std::sin(s.phase1) + 0.4 * std::sin(s.phase2));
                v += air_loud * bv.air_gain * 4.0 * s.air.run(s.noise(), std::min(s.pitch * 0.4, 16000.0), 3.0);
                v += 0.6 * c.hiss_gain * drive * drive * s.hiss.run(s.noise());
            } else {
                v += loud * (0.30 * std::sin(s.phase1) + c.overtone_gain * std::sin(s.phase2));
                v += loud * c.air_gain * 4.0 * s.air.run(s.noise(), std::min(s.pitch * 0.85, 16000.0), 5.0);
                v += c.hiss_gain * s.spool * s.spool * s.spool * s.spool * s.hiss.run(s.noise());
            }
            v += c.puff_gain * puffs;
            if (s.blow_age <= kBlowSeconds) { v += blow_sample(s, bov_pitch, bov_length, dt); s.blow_age += dt; }
        } else if (s.voice == Voice::kAntilag) {
            // Antilag: fuel lit in the exhaust manifold keeps the turbo spinning off throttle. Irregular gunshot bangs
            // (a sharp crack, a low thump that drops in pitch) with crackles in between; harder antilag bangs
            // more often. Bigger turbos bang deeper.
            s.bang_env += ((flag ? 1.0 : 0.0) - s.bang_env) * (1.0 - std::exp(-dt / (flag ? 0.02 : 0.25)));
            s.bang_clock += dt;
            if (s.bang_env > 0.05 && s.bang_clock >= s.bang_period) {
                s.bang_clock = 0;
                s.bang_age = 0;
                double r = 0.5 + 0.5 * s.noise();
                s.bang_gain = s.bang_env * (r < 0.25 ? 0.45 : 0.7 + 0.3 * r);                // some pops, some bangs
                s.bang_pitch = bov_pitch * (75.0 + 35.0 * (0.5 + 0.5 * s.noise()));
                double rate = 5.0 + 11.0 * target;                                           // bangs a second
                double n2 = 0.5 + 0.5 * s.noise();
                s.bang_period = n2 < 0.2 ? 0.03 + 0.03 * n2 : (1.0 / rate) * (0.5 + 1.0 * n2);   // now and then a double
            }
            s.bang_age += dt;
            double ba = s.bang_age;
            double crack = ba < 0.0008 ? ba / 0.0008 : std::exp(-(ba - 0.0008) / 0.006);
            double boom = ba < 0.002 ? ba / 0.002 : std::exp(-(ba - 0.002) / (0.05 * bov_length));
            s.thump_phase += two_pi * s.bang_pitch * (1.0 + 1.5 * std::exp(-ba / 0.012)) * dt;
            if (s.thump_phase > two_pi) s.thump_phase -= two_pi;
            double nz = s.noise();
            double bang = 2.2 * crack * (0.7 * nz + 1.6 * s.bang_body.run(nz)) + 1.6 * boom * (std::sin(s.thump_phase) + 0.8 * s.bang_low.run(nz));
            // Crackle: sparse ticks while it fires, thickest just after a bang.
            s.crackle_age += dt;
            double chance = s.bang_env * (0.0006 + 0.004 * std::exp(-ba / 0.08));
            if (0.5 + 0.5 * s.noise() < chance) s.crackle_age = 0;
            double tick = std::exp(-s.crackle_age / 0.0012) * (0.4 + 0.4 * (0.5 + 0.5 * s.noise()));
            v += s.bang_gain * bang + 0.9 * tick * s.crackle.run(s.noise());
        } else {
            // Blow-off: a burst of air when the valve opens, held while it vents, then a tail.
            //   Whoosh:  the classic "pssshh".
            //   Flutter: the same air chopped by the valve chattering on its seat (25-45 Hz, wandering): "brrrrsh".
            //   Chirp:   a sharp falling "tsu" as it cracks open, then the air.
            //   Dump:    a big, deep, longer "whoomph".
            if (flag && !s.was_open) { s.whoosh_env = 1.0; s.pop_age = 0; s.open_age = 0; }
            s.was_open = flag;
            s.open_age += dt;
            bool dump = s.sound == kDump;
            // Held open, the air follows how hard it vents and settles down as the boost bleeds away.
            double hold = flag ? std::sqrt(target) * (0.35 + 0.65 * std::exp(-s.open_age / (2.0 * bov_length))) : 0.0;
            if (s.whoosh_env < hold) s.whoosh_env += (hold - s.whoosh_env) * (1.0 - std::exp(-dt / 0.01));
            else {   // the burst holds for about its length, then tails off like a shut valve down to what it still vents
                bool burst = flag && s.open_age < bov_length;
                s.whoosh_env = hold + (s.whoosh_env - hold) * std::exp(-dt / (bov_length * (burst ? (dump ? 0.9 : 0.6) : (dump ? 0.45 : 0.28))));
            }
            double nz = s.noise();
            double air = dump ? 0.35 * s.whoosh.run(nz) + 0.55 * s.whoosh_low.run(nz) + 0.6 * s.rumble.run(nz)
                              : 0.55 * s.whoosh.run(nz) + 0.30 * s.whoosh_low.run(nz);
            double gain = 1.0;
            if (s.sound == kFlutter) {
                s.flutter_phase += s.flutter_rate * dt;
                if (s.flutter_phase >= 1.0) {
                    s.flutter_phase -= 1.0;
                    s.flutter_rate = (30.0 + 12.0 * s.whoosh_env + 6.0 * s.noise()) / bov_length;
                    s.flutter_depth = 0.55 + 0.35 * (0.5 + 0.5 * s.noise());
                }
                gain = 1.25 * (1.0 - s.flutter_depth * (0.5 + 0.5 * std::cos(two_pi * s.flutter_phase)));
            }
            v += s.whoosh_env * gain * air;
            s.pop_age += dt;
            if (s.sound == kChirp && s.pop_age < 0.2) {
                double centre = bov_pitch * (1700.0 + 3300.0 * std::exp(-s.pop_age / 0.035));
                double env = s.pop_age < 0.003 ? s.pop_age / 0.003 : std::exp(-(s.pop_age - 0.003) / 0.05);
                v += 0.5 * env * s.bov_chirp.run(s.noise(), centre, 9.0);   // band-pass peak gain is its Q
            }
        }
        v = std::isfinite(v) ? v : 0;
        out[i] = int16_t(std::lround(std::tanh(v) * 30000.0));   // soft clip: loud puffs round off, never crack
    }
}

}  // namespace turbo
