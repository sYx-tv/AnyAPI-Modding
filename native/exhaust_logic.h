#pragma once
// AnyVehicleOverhaul exhaust parts (Nate, 2026-11-04): tips that are their own vents, mufflers, resonators, a cat and
// header collectors, each with its own sound, real backpressure, and pops when the tune or the pipes warrant them.
// Pure logic: no game access, tested in tests/exhaust_logic_test.cpp.
//
//   Tips (class gas_vent: the game vents the exhaust and draws its smoke): Street, Race and Classic looks, each as a
//     single, dual, quad or side pipe. Bigger tips flow more (the game's vent law grows with the part's size). The
//     engine's exhaust sound plays from each tip (dual and quad split left and right).
//   Inline parts (the impeller class, like the blow-off valve, with an inlet and an outlet): the mod moves exhaust
//     through them at flow = k x (pressure in - pressure out), so a restrictive one holds pressure back in the
//     manifold. The game itself takes power off an engine whose exhaust pressure climbs past 4 (its exhaust factor).
//   Header collectors also give the engine a little power (better scavenging) when they sit on its exhaust manifold.
// Sound: what is between the engine and a tip decides how it sounds (enginesound_synth.h Tune): mufflers set the
//   muffling, the cat muffles a little and calms the pops, resonators take the drone and rasp out, unequal headers
//   give the rumble, no cat or a straight-through can lets it pop on the overrun, and a turbo in the path quietens it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace exhaust {

enum Look { kStreet, kRace, kClassic, kLookCount };
enum Layout { kSingle, kDual, kQuad, kSide, kLayoutCount };
enum Inline { kChambered, kStraight, kGlasspack, kTurboMuffler, kResonator, kHelmholtz, kCat, kHeaderEqual, kHeaderUnequal, kInlineCount };

inline constexpr const char* kLookIds[kLookCount] = {"street", "race", "classic"};
inline constexpr const char* kLookNames[kLookCount] = {"Street", "Race", "Classic"};
inline constexpr const char* kLayoutIds[kLayoutCount] = {"single", "dual", "quad", "side"};
inline constexpr const char* kLayoutNames[kLayoutCount] = {"Single", "Dual", "Quad", "Side Pipe"};
inline constexpr const char* kInlineIds[kInlineCount] = {"muffler_chambered", "muffler_straight_through", "muffler_glasspack", "muffler_turbo",
                                                         "resonator_inline", "resonator_helmholtz", "catalytic_converter", "header_collector_equal",
                                                         "header_collector_unequal"};
inline constexpr const char* kInlineNames[kInlineCount] = {"Chambered Muffler", "Straight-Through Muffler", "Glasspack Muffler", "Turbo Muffler",
                                                           "Inline Resonator", "Helmholtz Resonator", "Catalytic Converter", "Header Collector (Equal Length)",
                                                           "Header Collector (Unequal Length)"};
constexpr const char* kTipPrefix = "exhaust_tip_";

inline std::string tip_id(int look, int layout) { return std::string(kTipPrefix) + kLookIds[look] + "_" + kLayoutIds[layout]; }
// A tip id's look and layout; false when it is not one of ours.
inline bool tip_from_id(const char* id, size_t n, int* look = nullptr, int* layout = nullptr) {
    size_t p = std::strlen(kTipPrefix);
    if (!id || n <= p || std::memcmp(id, kTipPrefix, p) != 0) return false;
    for (int l = 0; l < kLookCount; ++l)
        for (int y = 0; y < kLayoutCount; ++y) {
            std::string want = tip_id(l, y);
            if (want.size() == n && std::memcmp(want.data(), id, n) == 0) { if (look) *look = l; if (layout) *layout = y; return true; }
        }
    return false;
}
// An inline part's kind, or -1.
inline int inline_from_id(const char* id, size_t n) {
    for (int k = 0; k < kInlineCount; ++k)
        if (std::strlen(kInlineIds[k]) == n && std::memcmp(kInlineIds[k], id, n) == 0) return k;
    return -1;
}

// ------------------------------------------------------------------------------------- flow
// Litres a second per unit of pressure difference (the game's pressure: 1 = the air outside).
inline double flow_coefficient(int k) {
    static constexpr double kK[kInlineCount] = {260, 520, 420, 170, 600, 640, 300, 900, 900};
    return k >= 0 && k < kInlineCount ? kK[k] : 400;
}
// Litres to move this tick from the inlet to the outlet (negative: back), never past evening the two sides out.
inline double inline_move(int k, double v_in, double cap_in, double v_out, double cap_out, double dt) {
    if (!(cap_in > 0) || !(cap_out > 0) || !(dt > 0) || !std::isfinite(v_in) || !std::isfinite(v_out)) return 0;
    double p_in = v_in / cap_in, p_out = v_out / cap_out;
    double want = flow_coefficient(k) * (p_in - p_out) * dt;
    double level = (v_in * cap_out - v_out * cap_in) / (cap_in + cap_out);   // what evens them out
    if (want > 0) return std::min({want, std::max(0.0, level), std::max(0.0, v_in)});
    if (want < 0) return -std::min({-want, std::max(0.0, -level), std::max(0.0, v_out)});
    return 0;
}
// Power the engine gains from a header collector on its exhaust manifold.
inline double header_power(bool equal, bool unequal) { return equal ? 1.04 : unequal ? 1.03 : 1.0; }

// ------------------------------------------------------------------------------------- sound
// What lies between an engine and its tips, as the client finds it along the pipes.
struct Chain {
    int count[kInlineCount]{};
    int turbos = 0;
    int tips[kLookCount]{};          // tips of each look (stock vents count as none)
    int side_pipes = 0, outlets = 0;
    bool any() const {
        if (turbos || outlets) return true;
        for (int c : count) if (c) return true;
        return false;
    }
    bool has(int k) const { return count[k] > 0; }
};
// The sound fields Chain changes (a copy of enginesound synth::Tune's, so this file needs no audio headers).
struct SoundTune { double pipe_hz, body_hz, rasp, muffler, crackle, intake, uneven, gain; };
// The tune the engine's preset gives, changed by the parts on its pipes. `ecu_crackle` is the ECU's pops (0-1, or -1
// without an ECU). Parts only take over when there are some: an engine with stock pipes keeps its preset.
inline SoundTune apply(SoundTune t, const Chain& c, double ecu_crackle) {
    if (!c.any()) { if (ecu_crackle >= 0) t.crackle = std::max(t.crackle, ecu_crackle); return t; }
    // The loudest muffler on the way decides the base muffling; none at all is a straight pipe.
    double muffle = 0.0;
    if (c.has(kTurboMuffler)) muffle = std::max(muffle, 0.85);
    if (c.has(kChambered)) muffle = std::max(muffle, 0.55);
    if (c.has(kGlasspack)) muffle = std::max(muffle, 0.32);
    if (c.has(kStraight)) muffle = std::max(muffle, 0.18);
    int mufflers = c.count[kTurboMuffler] + c.count[kChambered] + c.count[kGlasspack] + c.count[kStraight];
    if (mufflers > 1) muffle = std::min(0.95, muffle + 0.08 * (mufflers - 1));   // two in a row quieten it more
    if (c.has(kCat)) muffle = std::min(0.95, muffle + 0.1);
    if (c.turbos) muffle = std::min(0.95, muffle + 0.2);
    t.muffler = muffle;
    if (c.has(kResonator)) { t.rasp *= 0.7; t.muffler = std::min(0.95, t.muffler + 0.04); }
    if (c.has(kHelmholtz)) { t.pipe_hz *= 1.06; t.rasp *= 0.85; }
    if (c.has(kHeaderUnequal)) t.uneven = 1.0;
    else if (c.has(kHeaderEqual)) t.uneven = 0.0;
    if (!c.has(kCat) && !c.has(kTurboMuffler)) t.rasp = std::min(1.0, t.rasp + 0.15);
    // Tips: Race brighter and louder, Classic deeper, side pipes loudest.
    if (c.tips[kRace]) { t.rasp = std::min(1.0, t.rasp + 0.1); t.gain *= 1.08; }
    if (c.tips[kClassic]) t.pipe_hz *= 0.92;
    if (c.side_pipes) { t.gain *= 1.15; t.muffler = std::max(0.0, t.muffler - 0.08); }
    // Pops: a free-flowing exhaust pops on the overrun; a cat or a turbo calms it; the ECU's tune can add more.
    double pop = 0.15;
    if (!c.has(kCat)) pop += 0.3;
    if (c.has(kStraight) || mufflers == 0) pop += 0.2;
    if (c.has(kCat)) pop *= 0.35;
    if (c.turbos) pop *= 0.6;
    t.crackle = std::clamp(std::max(pop, ecu_crackle), 0.0, 1.0);
    return t;
}

// ------------------------------------------------------------------------------------- part definitions
constexpr int kTier = 3;                     // after the 2nd bunker
constexpr double kSpawn = 0.15;
inline std::string mesh_path_tip(int look, int layout) { return "meshes/components/" + tip_id(look, layout) + ".mesh"; }
inline std::string mesh_path_inline(int k) { return std::string("meshes/components/") + kInlineIds[k] + ".mesh"; }
// Tip zones (cells): single 1x1x3, dual 3x1x3, quad 3x3x3, side 1x1x6. The inlet is cell (0, 0, 0)'s back face.
struct Box { int x0, y0, z0, x1, y1, z1; };
inline Box tip_box(int layout) {
    switch (layout) {
        case kDual: return {-1, 0, 0, 1, 0, 2};
        case kQuad: return {-1, -1, 0, 1, 1, 2};
        case kSide: return {0, 0, 0, 0, 0, 5};
    }
    return {0, 0, 0, 0, 0, 2};
}
inline Box inline_box(int k) {
    int last = (k == kResonator || k == kCat || k == kHelmholtz ? 4 : k == kHeaderEqual || k == kHeaderUnequal ? 3 : 6) - 1;
    switch (k) {
        case kChambered: case kTurboMuffler: return {-1, -1, 0, 1, 0, last};
        case kStraight: case kGlasspack: case kHeaderEqual: case kHeaderUnequal: return {-1, -1, 0, 1, 1, last};
        case kCat: return {-1, -1, 0, 1, 0, last};
        case kHelmholtz: return {-1, -2, 0, 1, 0, last};
        default: return {-1, -1, 0, 1, 1, last};   // resonator
    }
}
// Plain mounting faces on the box's sides, top and bottom (the pipe ends stay free).
inline std::string mounts(const Box& b) {
    auto face = [](int x, int y, int z, int su, int sw, int dir) {
        return ", {\"pos\": [" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "], \"size\": [" + std::to_string(su) + ", " +
               std::to_string(sw) + "], \"dir\": " + std::to_string(dir) + "}";
    };
    int nx = b.x1 - b.x0 + 1, ny = b.y1 - b.y0 + 1, nz = b.z1 - b.z0 + 1;
    return face(b.x0, b.y0, b.z0, nz, ny, 0) + face(b.x1, b.y0, b.z0, nz, ny, 1) + face(b.x0, b.y0, b.z0, nx, nz, 2) + face(b.x0, b.y1, b.z0, nx, nz, 3);
}
inline std::string box_json(const Box& b) {
    return "{\"bounds_min\": [" + std::to_string(b.x0) + ", " + std::to_string(b.y0) + ", " + std::to_string(b.z0) + "], \"bounds_max\": [" +
           std::to_string(b.x1) + ", " + std::to_string(b.y1) + ", " + std::to_string(b.z1) + "]}";
}
inline std::string loot() {
    return "      \"tech_tier\": " + std::to_string(kTier) + ",\n      \"spawn_probability\": 0.15,\n      \"loot_group\": \"mechanical\",\n";
}
inline std::string tip_entry(int look, int layout, const std::string& mesh) {
    Box b = tip_box(layout);
    std::string description = std::string(kLookNames[look]) + " exhaust tip (" + kLayoutNames[layout] + "): the end of an engine's exhaust, a vent of its own. "
        "Pipe the exhaust into its back. The engine's exhaust sound comes out of it" + (layout == kDual || layout == kQuad ? ", split left and right" : "") +
        ", and what is on the pipes on the way (mufflers, resonators, a cat, headers) changes how it sounds. Bigger tips flow more.";
    return std::string("    {\n") +
        "      \"id\": \"" + tip_id(look, layout) + "\",\n" +
        "      \"name\": \"Exhaust Tip (" + kLookNames[look] + " " + kLayoutNames[layout] + ")\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_vent\",\n" +
        "      \"zones\": [" + box_json(b) + "],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [" + mounts(b).substr(2) + "],\n" +
        "      \"logic_nodes\": [{\"pos\": [0, 0, 0], \"direction\": 4, \"type\": \"gas\"}],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [" + std::to_string(b.x0) + ".0, " + std::to_string(b.y0) + ".0, 0.0], \"p1\": [" + std::to_string(b.x1) + ".0, " +
            std::to_string(b.y1) + ".0, " + std::to_string(b.z1) + ".0]}],\n" +
        "      \"data_descriptors\": [],\n" +
        loot() +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
inline std::string inline_description(int k) {
    switch (k) {
        case kChambered: return "Chambered muffler (Street): baffled chambers, a deep muscle burble, fairly quiet. Moderate backpressure.";
        case kStraight: return "Straight-through muffler (Race): a perforated core in packing, loud and raspy, pops on the overrun. Little backpressure.";
        case kGlasspack: return "Glasspack muffler (Classic): a long packed tube, a loud old-school rumble. Low backpressure.";
        case kTurboMuffler: return "Turbo muffler: an S-flow box, the quietest. The most backpressure.";
        case kResonator: return "Inline resonator: takes the rasp and drone out without much backpressure.";
        case kHelmholtz: return "Helmholtz resonator: a tuned side chamber that cancels the cabin drone; almost no backpressure.";
        case kCat: return "Catalytic converter: a little quieter, calms the pops, a little backpressure. Leave it off for a cat delete.";
        case kHeaderEqual: return "Equal-length header collector: put it right on the exhaust manifold. Smooth note, about 4 % more power.";
        default: return "Unequal-length header collector: put it right on the exhaust manifold. The off-beat rumble (a boxer's), about 3 % more power.";
    }
}
inline std::string inline_entry(int k, const std::string& mesh) {
    Box b = inline_box(k);
    std::string description = inline_description(k) + " Pipe exhaust in its back end and out its front end.";
    return std::string("    {\n") +
        "      \"id\": \"" + kInlineIds[k] + "\",\n" +
        "      \"name\": \"" + kInlineNames[k] + "\",\n" +
        "      \"description\": \"" + description + "\",\n" +
        "      \"class\": \"gas_pump_centrifugal\",\n" +
        "      \"zones\": [" + box_json(b) + "],\n" +
        "      \"interaction_zones\": [],\n" +
        "      \"surfaces\": [{\"id\": \"torque\", \"pos\": [" + std::to_string(b.x0) + ", " + std::to_string(b.y0) + ", 0], \"dir\": 4, \"type\": \"torque\"}" +
            mounts(b) + "],\n" +
        "      \"logic_nodes\": [{\"pos\": [0, 0, 0], \"direction\": 4, \"type\": \"gas\"}, {\"pos\": [0, 0, " + std::to_string(b.z1) + "], \"direction\": 5, \"type\": \"gas\"}],\n" +
        "      \"mesh_static\": {\"mesh_path\": \"" + mesh + "\"},\n" +
        "      \"meshes_dynamic\": [],\n" +
        "      \"category\": \"gas\",\n" +
        "      \"tags_generated\": \"torque,port,\",\n" +
        "      \"inspection_geometry\": [{\"p0\": [" + std::to_string(b.x0) + ".0, " + std::to_string(b.y0) + ".0, 0.0], \"p1\": [" + std::to_string(b.x1) + ".0, " +
            std::to_string(b.y1) + ".0, " + std::to_string(b.z1) + ".0]}],\n" +
        "      \"density\": 800.0,\n" +
        "      \"mateable_port_types\": 2,\n" +
        "      \"data_descriptors\": [],\n" +
        loot() +
        "      \"constraint_orientations\": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],\n" +
        "      \"force_emitter_element_rotor\": {\"blades\": []}\n" +
        "    }";
}
// Every exhaust part's entry (without the file's braces), with the given meshes (empty: a stock look).
inline std::string entries(bool meshes) {
    std::string out;
    for (int l = 0; l < kLookCount; ++l)
        for (int y = 0; y < kLayoutCount; ++y)
            out += (out.empty() ? "" : ",\n") + tip_entry(l, y, meshes ? mesh_path_tip(l, y) : "meshes/components/gas_port_b.mesh");
    for (int k = 0; k < kInlineCount; ++k)
        out += ",\n" + inline_entry(k, meshes ? mesh_path_inline(k) : "meshes/components/turbo_a.mesh");
    return out;
}

}  // namespace exhaust
