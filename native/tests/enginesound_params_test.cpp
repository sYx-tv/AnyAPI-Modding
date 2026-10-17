#include "../enginesound_params.h"
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace enginesound;

int main() {
    // Slider fields don't overlap and fit the 31 payload bits.
    uint32_t used = 0;
    for (auto& s : kSliders) { uint32_t m = uint32_t((1 << s.bits) - 1) << s.shift; assert((used & m) == 0); used |= m; }
    assert(used < 0x80000000u);
    // Packed tunes round-trip, are always negative and never collide with an index.
    for (int s = 0; s < kSliderCount; ++s) {
        Params q{}; q.v[s] = uint8_t(max_step(s));
        Params r = unpack(pack(q));
        for (int t = 0; t < kSliderCount; ++t) assert(r.v[t] == q.v[t]);
        assert(is_packed(pack(q)));
    }
    Params zero{};
    assert(pack(zero) == INT32_MIN && is_packed(INT32_MIN));
    Params over{}; over.v[kNote] = 200;
    assert(unpack(pack(over)).v[kNote] == max_step(kNote));

    // The game's engines.
    auto kind = [](const char* id) { return engine_kind(id, std::strlen(id)); };
    assert(kind("engine").layout == synth::kInline && kind("engine").size == kSmall && kind("engine").known);
    assert(kind("engine_block_c").layout == synth::kInline && kind("engine_block_c").size == kLarge);
    assert(kind("v_engine_b").layout == synth::kVee && kind("v_engine_b").size == kMedium);
    assert(kind("opposed_engine_c").layout == synth::kBoxer && kind("opposed_engine_c").size == kLarge);
    assert(kind("rotary_engine").rotary && !kind("engine").rotary && !kind("enginex").known && !kind("v_engine_d").known);
    // Cylinders from the stretch: inline +1 per step, V and boxer +2.
    assert(cylinders(kind("engine"), 0) == 1 && cylinders(kind("engine"), 2) == 2 && cylinders(kind("engine"), 6) == 4);
    assert(cylinders(kind("engine_block_b"), 3) == 2 && cylinders(kind("engine_block_c"), 20) == 5);
    assert(cylinders(kind("v_engine"), 0) == 2 && cylinders(kind("v_engine"), 6) == 8 && cylinders(kind("v_engine_c"), 20) == 10);
    assert(cylinders(kind("opposed_engine"), 2) == 4 && cylinders(kind("opposed_engine_b"), 6) == 6);
    assert(cylinders(kind("v_engine"), 200) == synth::kMaxCylinders && cylinders(kind("engine"), -4) == 3 && cylinders(kind("engine"), INT32_MIN) == 1);
    char name[32];
    engine_name(synth::kVee, 8, name, sizeof name); assert(std::strcmp(name, "V8") == 0);
    engine_name(synth::kVee, 2, name, sizeof name); assert(std::strcmp(name, "V-twin") == 0);
    engine_name(synth::kBoxer, 4, name, sizeof name); assert(std::strcmp(name, "Flat-4") == 0);
    engine_name(synth::kInline, 1, name, sizeof name); assert(std::strcmp(name, "Single") == 0);

    // Row tags round-trip every engine and stay inside the span.
    for (int l = 0; l < synth::kLayoutCount; ++l)
        for (int s = 0; s < 3; ++s)
            for (int n = 1; n <= synth::kMaxCylinders; ++n) {
                EngineKind k{Layout(l), Size(s), false, true}, back{};
                int cyl = 0;
                int32_t tag = row_tag(k, n);
                assert(tag > 0 && tag < kTagSpan && is_modded_max(kServerMax - tag));
                decode_tag(tag, back, cyl);
                assert(back.layout == l && back.size == s && cyl == n && back.known);
            }
    assert(!is_modded_max(kVanillaMax) && is_modded_max(kServerMax));
    { EngineKind k{}; int cyl = 0; decode_tag(0, k, cyl); assert(!k.known && cyl == 4); }

    // Decoding: vanilla stays vanilla, presets and Custom per layout, old values past Custom open Custom.
    for (int l = 0; l < synth::kLayoutCount; ++l) {
        for (int32_t i = kVanillaMin; i <= kVanillaMax; ++i) assert(decode(i, Layout(l)).mode == Mode::Vanilla);
        for (int i = 0; i < synth::kPresetsPerLayout; ++i) {
            Choice c = decode(kFirstPreset + i, Layout(l));
            assert(c.mode == Mode::Preset && c.preset == i);
            assert(std::strcmp(choice_name(kFirstPreset + i, Layout(l)), synth::kPresets[l][i].name) == 0);
            // "Customize" copies a preset into Custom: the packed sliders land close to the preset.
            Tune a = synth::kPresets[l][i].tune, b = to_tune(unpack(pack(c.params)));
            assert(std::fabs(a.pipe_hz - b.pipe_hz) <= 4.01 && std::fabs(a.body_hz - b.body_hz) <= 30.01);
            assert(std::fabs(a.muffler - b.muffler) < 0.08 && std::fabs(a.cam - b.cam) < 0.08 && a.diesel == b.diesel);
            assert(std::fabs(a.gain - b.gain) < 1e-9);
        }
        assert(decode(kCustomIndex, Layout(l)).mode == Mode::Custom && decode(10, Layout(l)).mode == Mode::Custom);
        assert(decode(-5, Layout(l)).mode == Mode::Custom);
    }
    // Size: a large block sounds deeper than a small one.
    EngineKind small{synth::kVee, kSmall, false, true}, large{synth::kVee, kLarge, false, true};
    Choice muscle = decode(kFirstPreset + 1, synth::kVee);
    assert(tune_of(muscle, large).pipe_hz < tune_of(muscle, small).pipe_hz);
    assert(std::strcmp(firing_name(synth::kVee, 8, 0), "Cross-plane") == 0 && std::strcmp(firing_name(synth::kVee, 8, 3), "Flat-plane") == 0);
    std::puts("enginesound params: ok");
    return 0;
}
