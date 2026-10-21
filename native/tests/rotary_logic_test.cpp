// Rotary logic, mesh and sound tests. With "--wav <dir>" it also writes the sound previews.
#include "../rotary_logic.h"
#include "../rotary_mesh.h"
#include "../rotary_sound.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
using namespace rotary;

namespace {
struct Level { double peak = 0, rms = 0; };
Level level(const std::vector<int16_t>& pcm, size_t from = 0, size_t to = SIZE_MAX) {
    Level l; double sum = 0; size_t n = 0;
    for (size_t i = from; i < pcm.size() && i < to; ++i) { double v = pcm[i] / 32768.0; l.peak = std::fmax(l.peak, std::abs(v)); sum += v * v; ++n; }
    l.rms = n ? std::sqrt(sum / double(n)) : 0;
    return l;
}
// Renders a scenario: f(t) sets the input at time t (seconds), updated every 1/60 s like the game's tick.
std::vector<int16_t> scenario(double seconds, int rotors, Port port, const std::function<void(double, Input&)>& f) {
    Synth s; Input in; in.rotors = rotors; in.port = port;
    std::vector<int16_t> pcm(size_t(seconds * kSampleRate));
    const int step = kSampleRate / 60;
    for (size_t at = 0; at < pcm.size(); at += step) {
        f(double(at) / kSampleRate, in);
        render(s, in, pcm.data() + at, int(std::min<size_t>(step, pcm.size() - at)));
    }
    return pcm;
}
double ramp(double t, double t0, double t1, double a, double b) { double x = (t - t0) / (t1 - t0); x = x < 0 ? 0 : x > 1 ? 1 : x; return a + (b - a) * x; }
// Crank on the starter, catch at 6 rps, idle.
void idle(double t, Input& in) {
    in.rps = t < 1.0 ? ramp(t, 0, 1.0, 0, 6.5) : 7.5 + 0.6 * std::sin(t * 1.3);
    in.running = t >= 1.0;
    in.load = t < 1.0 ? 0 : 0.08;
}
// Idle, a full-throttle pull to the 8000 rpm limit, lift off (pops) and fall back to idle.
void sweep(double t, Input& in) {
    in.running = true;
    if (t < 2.0) { in.rps = 7.5; in.load = 0.08; }
    else if (t < 7.0) { in.rps = ramp(t, 2.0, 7.0, 7.5, kMaxRps); in.load = 1.0; }
    else if (t < 7.6) { in.rps = kMaxRps; in.load = 0.9; }
    else { in.rps = ramp(t, 7.6, 10.5, kMaxRps, 7.5); in.load = 0.0; }
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
    // Ids, rotors, power.
    assert(is_rotary_id("rotary_engine", 13) && !is_rotary_id("rotary_engine", 12) && !is_rotary_id("engine", 6));
    assert(rotors_from_extend(0) == 1 && rotors_from_extend(2) == 2 && rotors_from_extend(4) == 3 && rotors_from_extend(6) == 4);
    assert(rotors_from_extend(40) == 4 && rotors_from_extend(-2) == 2 && rotors_from_extend(1) == 1);
    assert(boosted_volume(36) == 45 && boosted_volume(144) == 180 && boosted_volume(0) == 0);
    assert(port_from_index(0) == kBridgePort && port_from_index(1) == kStreetPort && port_from_index(2) == kPeripheralPort && port_from_index(7) == kBridgePort);
    assert(std::abs(factor_to_rps(1.0) - kMaxRps) < 1e-9 && factor_to_rps(-1) == 0 && factor_to_rps(NAN) == 0);
    // The definition is the small engine's, as a rotary, stretchable to 4 rotors.
    std::string json = definition_json(mesh_path());
    assert(json.find("\"class\": \"engine\"") != std::string::npos && json.find("\"id\": \"rotary_engine\"") != std::string::npos);
    assert(json.find("\"tech_tier\": 4,") != std::string::npos);   // world loot after the 3rd bunker
    assert(json.find("\"mode_z\": \"tile\"") != std::string::npos && json.find("\"interval\": [0, 0, 2]") != std::string::npos);
    assert(json.find("\"ext_max\": [0, 0, 6]") != std::string::npos && json.find("rotary_engine_0_0_0.mesh") != std::string::npos);
    assert(json.find("\"torque_engine\"") != std::string::npos && json.find("\"mateable_port_types\": 498") != std::string::npos);
    assert(json.find('\\') == std::string::npos);
    for (char ch : json) assert(ch != '\t' && (unsigned char)ch < 0x80);

    // Meshes: within the small engine's cells (the front and back lips 0.02 m past them, like the stock tiles),
    // every index in range, every triangle clockwise seen from outside like the game's own meshes.
    const double z_lo[3] = {-0.0601, -0.0401, -0.0401}, z_hi[3] = {0.0401, 0.1201, 0.0601};
    for (int t = 0; t < 3; ++t) {
        Mesh m = build_tile(t);
        assert(!m.v.empty() && m.i.size() % 3 == 0 && m.v.size() < 65536);
        for (auto& v : m.v) {
            assert(v.pos[0] >= -0.1201 && v.pos[0] <= 0.1201 && v.pos[1] >= -0.1201 && v.pos[1] <= 0.3601);
            assert(v.pos[2] >= z_lo[t] && v.pos[2] <= z_hi[t]);
            double n = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
            assert(std::abs(n - 1) < 1e-3);
        }
        for (size_t k = 0; k < m.i.size(); k += 3) {
            assert(m.i[k] < m.v.size() && m.i[k + 1] < m.v.size() && m.i[k + 2] < m.v.size());
            const float *a = m.v[m.i[k]].pos, *b = m.v[m.i[k + 1]].pos, *c = m.v[m.i[k + 2]].pos, *n = m.v[m.i[k]].normal;
            double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            double cr[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            assert(cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] <= 1e-12);
        }
        std::vector<uint8_t> file = encode_mesh(m, tile_name(t));
        uint32_t name_len = 0, vbytes = 0;
        std::memcpy(&name_len, file.data() + 12, 4);
        assert(std::memcmp(file.data(), "mesh", 4) == 0 && name_len == tile_name(t).size());
        std::memcpy(&vbytes, file.data() + 16 + name_len + 88 + 48, 4);
        assert(vbytes == m.v.size() * sizeof(MeshVertex));
        assert(file.size() == 16 + name_len + 88 + 48 + 4 + vbytes + 4 + m.i.size() * 4 + 8);
    }
    assert(tile_name(1) == "rotary_engine_0_0_1");

    // Sound: silent at rest, puffs while cranking, alive at idle, louder at full load, never clipping hard.
    {
        std::vector<int16_t> rest = scenario(1.0, 2, kBridgePort, [](double, Input& in) { in.rps = 0; in.running = false; });
        assert(level(rest).peak < 0.01);
        std::vector<int16_t> pcm = scenario(6.0, 2, kBridgePort, idle);
        Level crank = level(pcm, 0, kSampleRate), run = level(pcm, 2 * kSampleRate);
        assert(crank.rms > 0.002 && run.rms > crank.rms * 2 && run.peak < 0.99);
        std::vector<int16_t> pull = scenario(10.5, 2, kBridgePort, sweep);
        Level top = level(pull, size_t(6.0 * kSampleRate), size_t(7.5 * kSampleRate));
    assert(top.rms > 0.15 && level(pull, 0, 2 * kSampleRate).rms > 0.1 && top.peak < 0.995);
        for (int16_t v : pull) assert(v > -32000 && v < 32000);
    }

    // Previews: tests/rotary_logic_test --wav <dir>
    if (argc == 3 && std::strcmp(argv[1], "--wav") == 0) {
        std::string dir = argv[2];
        struct Clip { const char* name; int rotors; Port port; double seconds; void (*f)(double, Input&); };
        const Clip clips[] = {
            {"1-idle-brap-2-rotor-bridge-port", 2, kBridgePort, 8.0, idle},
            {"2-rev-sweep-2-rotor-bridge-port", 2, kBridgePort, 10.5, sweep},
            {"3-idle-2-rotor-street-port", 2, kStreetPort, 6.0, idle},
            {"4-rev-sweep-2-rotor-street-port", 2, kStreetPort, 10.5, sweep},
            {"5-idle-2-rotor-peripheral-port", 2, kPeripheralPort, 6.0, idle},
            {"6-rev-sweep-2-rotor-peripheral-port", 2, kPeripheralPort, 10.5, sweep},
            {"7-rev-sweep-1-rotor", 1, kBridgePort, 10.5, sweep},
            {"8-rev-sweep-3-rotor", 3, kBridgePort, 10.5, sweep},
            {"9-rev-sweep-4-rotor", 4, kBridgePort, 10.5, sweep},
        };
        for (const Clip& c : clips) {
            std::vector<int16_t> pcm = scenario(c.seconds, c.rotors, c.port, [&](double t, Input& in) { c.f(t, in); });
            std::string path = dir + "/rotary-" + c.name + ".wav";
            Level l = level(pcm);
            std::printf("%s: peak %.2f, rms %.3f, %s\n", path.c_str(), l.peak, l.rms, write_wav(path, pcm) ? "written" : "NOT WRITTEN");
        }
    }
    std::puts("rotary logic, mesh and sound: ok");
    return 0;
}
