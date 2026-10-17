// EngineSound synth tests. With "--wav <dir>" it also writes the sound previews.
#include "../enginesound_synth.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace enginesound::synth;

namespace {
double ramp(double t, double t0, double t1, double a, double b) { return a + (b - a) * clamp01((t - t0) / (t1 - t0)); }

// Crank, catch, idle, two blips, a full-throttle pull into the limiter, lift off (pops) and back to idle.
// rps here is the game's engine speed.
void drive(double t, Input& in) {
    in.running = t >= 0.9;
    if (t < 0.9) { in.rps = ramp(t, 0.0, 0.3, 0.0, 3.5); in.load = 0; }
    else if (t < 3.5) { in.rps = kStartRps; in.load = 0.05; }
    else if (t < 3.8) { in.rps = ramp(t, 3.5, 3.8, kStartRps, 60); in.load = 1; }
    else if (t < 4.6) { in.rps = ramp(t, 3.8, 4.6, 60, kStartRps); in.load = 0; }
    else if (t < 5.5) { in.rps = kStartRps; in.load = 0.05; }
    else if (t < 10.5) { in.rps = ramp(t, 5.5, 10.5, 12, kMaxRps); in.load = 1; }
    else if (t < 11.6) { in.rps = kMaxRps; in.load = 1; }
    else if (t < 14.0) { in.rps = ramp(t, 11.6, 14.0, kMaxRps, kStartRps); in.load = 0; }
    else { in.rps = kStartRps; in.load = 0.05; }
}
constexpr double kSeconds = 16.0;

std::vector<int16_t> render_clip(Layout layout, int cylinders, const Tune& tune, double seconds) {
    Synth s;
    Input in;
    in.layout = layout; in.cylinders = cylinders; in.tune = tune;
    std::vector<int16_t> pcm(size_t(seconds * kSampleRate));
    const int block = 480;   // the game streams in ~10 ms ticks
    for (size_t at = 0; at < pcm.size(); at += block) {
        drive(double(at) / kSampleRate, in);
        Input game = in;
        game.rps = sound_rps(in.rps);
        render(s, game, pcm.data() + at, int(std::min<size_t>(block, pcm.size() - at)));
    }
    return pcm;
}
struct Level { double peak, rms; };
Level level(const std::vector<int16_t>& pcm, size_t from = 0, size_t to = SIZE_MAX) {
    if (to > pcm.size()) to = pcm.size();
    double peak = 0, sum = 0;
    for (size_t i = from; i < to; ++i) { double v = pcm[i] / 32768.0; peak = std::fmax(peak, std::fabs(v)); sum += v * v; }
    return {peak, to > from ? std::sqrt(sum / double(to - from)) : 0};
}
bool write_wav(const std::string& path, const std::vector<int16_t>& pcm) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t data = uint32_t(pcm.size() * 2), riff = 36 + data, fmt = 16, rate = kSampleRate, byte_rate = kSampleRate * 2;
    uint16_t pcm_fmt = 1, channels = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmt, 4, 1, f);
    std::fwrite(&pcm_fmt, 2, 1, f); std::fwrite(&channels, 2, 1, f); std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    return std::fclose(f) == 0;
}
}  // namespace

int main(int argc, char** argv) {
    // Speed map: the start speed sounds like a real idle, the top stays the top.
    assert(std::fabs(sound_rps(kStartRps) - kIdleRps) < 1e-9 && std::fabs(sound_rps(kMaxRps) - kMaxRps) < 1e-9);
    assert(sound_rps(0) == 0 && sound_rps(-3) == 0 && sound_rps(NAN) == 0 && sound_rps(3) > 0 && sound_rps(3) < kIdleRps);

    // Firing tables: ascending angles inside the cycle, the right banks.
    for (int l = 0; l < kLayoutCount; ++l)
        for (int n = 1; n <= kMaxCylinders; ++n)
            for (double u : {0.0, 0.5, 1.0}) {
                Firing f = firing(Layout(l), n, u);
                assert(f.count == n);
                for (int i = 0; i < n; ++i) {
                    assert(f.angle[i] >= 0 && f.angle[i] < 720 && (f.bank[i] == 0 || f.bank[i] == 1));
                    if (i) assert(f.angle[i] > f.angle[i - 1]);
                    if (l == kInline) assert(f.bank[i] == 0);
                }
            }
    Firing i4 = firing(kInline, 4, 0);
    assert(i4.angle[1] == 180 && i4.angle[3] == 540);
    Firing vtwin = firing(kVee, 2, 0);
    assert(vtwin.angle[1] == 405 && vtwin.bank[1] == 1);
    Firing cross = firing(kVee, 8, 0), flat = firing(kVee, 8, 1);
    assert(cross.bank[2] == 0 && cross.bank[3] == 0 && flat.bank[2] == 0 && flat.bank[3] == 1);
    Firing v4 = firing(kVee, 4, 0);
    assert(v4.angle[2] == 450);

    // Pulse rate: an inline-4 at 3000 rpm fires 100 times a second (count the pulse onsets of a straight pipe).
    {
        Synth s;
        Input in;
        in.layout = kInline; in.cylinders = 4; in.tune = kPresets[kInline][0].tune; in.running = true; in.rps = 50; in.load = 0.5;
        std::vector<int16_t> warm(kSampleRate);
        render(s, in, warm.data(), int(warm.size()));
        int fired = 0;
        for (int i = 0; i < kSampleRate; ++i) {
            int before = s.bank[0].age == 0 ? 1 : 0;
            (void)before;
            int16_t one;
            render(s, in, &one, 1);
            if (s.bank[0].age == 1.0 / kSampleRate) ++fired;
        }
        assert(fired >= 98 && fired <= 102);
    }

    // Every layout, count and preset renders clean, loud enough and never clipping hard.
    for (int l = 0; l < kLayoutCount; ++l)
        for (int n : {1, 2, 3, 4, 6, 8, 12}) {
            if (l != kInline && n == 1) continue;
            for (int p = 0; p < kPresetsPerLayout; ++p) {
                std::vector<int16_t> pcm = render_clip(Layout(l), n, kPresets[l][p].tune, kSeconds);
                Level idle = level(pcm, size_t(1.5 * kSampleRate), size_t(3.4 * kSampleRate));
                Level pull = level(pcm, size_t(8.0 * kSampleRate), size_t(10.4 * kSampleRate));
                if (idle.rms < 0.03 || pull.rms < 1.2 * idle.rms || pull.peak > 0.999)
                    std::fprintf(stderr, "level: %s-%d %s idle %.3f pull %.3f peak %.3f\n", layout_name(Layout(l)), n, kPresets[l][p].name, idle.rms, pull.rms, pull.peak);
                assert(idle.rms > 0.03 && pull.rms > 1.2 * idle.rms && pull.peak < 0.9995);
            }
        }

    // Previews: tests/enginesound_synth_test --wav <dir>
    if (argc == 3 && std::strcmp(argv[1], "--wav") == 0) {
        std::string dir = argv[2];
        struct Clip { Layout layout; int cylinders; int preset; };
        const Clip clips[] = {
            {kInline, 1, 0}, {kInline, 3, 1}, {kInline, 4, 0}, {kInline, 4, 1}, {kInline, 4, 3}, {kInline, 5, 1},
            {kInline, 6, 2}, {kInline, 6, 4}, {kVee, 2, 1}, {kVee, 6, 0}, {kVee, 8, 1}, {kVee, 8, 2}, {kVee, 8, 4},
            {kVee, 10, 3}, {kVee, 12, 2}, {kBoxer, 2, 2}, {kBoxer, 4, 1}, {kBoxer, 6, 2},
        };
        int k = 0;
        for (const Clip& c : clips) {
            std::vector<int16_t> pcm = render_clip(c.layout, c.cylinders, kPresets[c.layout][c.preset].tune, kSeconds);
            std::string name = kPresets[c.layout][c.preset].name;
            for (char& ch : name) ch = ch == ' ' ? '-' : char(std::tolower((unsigned char)ch));
            char file[160];
            std::snprintf(file, sizeof file, "/%02d-%s-%d-%s.wav", ++k, c.layout == kVee ? "v" : c.layout == kBoxer ? "boxer" : "inline", c.cylinders, name.c_str());
            Level lv = level(pcm);
            std::printf("%s%s: peak %.2f, rms %.3f, %s\n", dir.c_str(), file, lv.peak, lv.rms, write_wav(dir + file, pcm) ? "written" : "NOT WRITTEN");
        }
    }
    std::puts("enginesound synth: ok");
    return 0;
}
