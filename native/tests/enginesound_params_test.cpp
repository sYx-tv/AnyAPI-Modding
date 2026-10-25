#include "../enginesound_params.h"
#include "../enginesound_save.h"
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

    // Saves hold only stock sounds (enginesound_save.h).
    assert(!needs_stock_save(kVanillaMin) && !needs_stock_save(kVanillaMax) && needs_stock_save(kFirstPreset) && needs_stock_save(kCustomIndex));
    assert(needs_stock_save(-1) && needs_stock_save(INT32_MIN) && needs_stock_save(INT32_MAX) && kSavedStockSound == 0);
    Params tune{}; tune.v[kNote] = 9;
    const int32_t packed = pack(tune);
    SoundMap saved;
    assert(note_saved(saved, {12, 34}, kFirstPreset + 2) && !note_saved(saved, {12, 34}, kFirstPreset + 2));
    assert(note_saved(saved, {12, 40}, packed) && note_saved(saved, {7, 1}, kCustomIndex) && saved.size() == 3);
    assert(!note_saved(saved, {9, 9}, 1) && saved.size() == 3);           // a stock sound is never kept
    assert(note_saved(saved, {7, 1}, 2) && saved.size() == 2);            // switched back to a stock sound: forgotten
    // Read back: only while the engine still holds the stock sound it was saved with.
    assert(restored(saved, {12, 34}, kSavedStockSound) == kFirstPreset + 2 && restored(saved, {12, 40}, kSavedStockSound) == packed);
    assert(restored(saved, {12, 34}, 1) == 1);                            // changed while the mod was off: kept
    assert(restored(saved, {12, 34}, kFirstPreset + 4) == kFirstPreset + 4);   // an older save holding the choice itself
    assert(restored(saved, {12, 35}, kSavedStockSound) == kSavedStockSound && restored(saved, {13, 34}, kSavedStockSound) == kSavedStockSound);
    // The world file round-trips, ids and tunes of any sign.
    assert(note_saved(saved, {-3, 2147483647}, INT32_MIN));
    std::string file = encode_saved(saved);
    SoundMap back = decode_saved(file);
    assert(back.size() == saved.size());
    for (auto& [k, v] : saved) { auto f = back.find(k); assert(f != back.end() && f->second.choice == v.choice && f->second.stock == v.stock); }
    assert(file.find("\"version\": 1") != std::string::npos && file.find("\"12:34\": [5, 0]") != std::string::npos);
    // Junk and entries the game could not have saved are skipped.
    SoundMap junk = decode_saved("{\"version\": 1, \"sounds\": {\"1:2\": [1, 0], \"3:4\": [5, 9], \"x:1\": [5, 0], \"5\": [5, 0], \"6:\": [5, 0],"
                                 " \"7:8\": 5, \"9:10\": [5 0], \"11:12\": [6, 0], \"13:14\": [99999999999, 0]}}");
    assert(junk.size() == 1 && junk.begin()->first.vehicle == 11 && junk.begin()->second.choice == 6);
    assert(decode_saved("").empty() && decode_saved("{\"").empty() && encode_saved(SoundMap{}) == "{\"version\": 1, \"sounds\": {\n}}\n");
    // Saved vehicles: the choice under its own key comes back only onto the stock sound it was saved with.
    assert(restored_from_key(kSavedStockSound, kFirstPreset) == kFirstPreset && restored_from_key(kSavedStockSound, packed) == packed);
    assert(restored_from_key(kSavedStockSound, kSavedStockSound) == kSavedStockSound);   // no key: the default
    assert(restored_from_key(2, kFirstPreset) == 2 && restored_from_key(kFirstPreset + 1, kFirstPreset) == kFirstPreset + 1);
    assert(restored_from_key(kSavedStockSound, 1) == kSavedStockSound);
    std::puts("enginesound params: ok");
    return 0;
}
