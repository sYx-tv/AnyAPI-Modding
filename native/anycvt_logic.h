#pragma once
// AnyCVT pure logic: the three CVTs (their definitions, built from the stock gearboxes'), their settings, and what one
// tick of one of them does. No game or Windows dependencies, so tests/anycvt_logic_test.cpp runs it anywhere.
//
// Game facts this is built on (Anymaker 0.1.28, read from game.gcl):
//   - A CVT is the game's own gearbox (class "gearbox"). engine.add_mating_component keeps any part of class gearbox
//     that mates on its engine face in m_gearboxes, then every engine tick sets the mass of the gearbox's one torque
//     node ("torque", its output face), reads its rps as the engine's and pushes the engine's torque into it. So the
//     node spins at engine speed, and its m_gear_ratio (the gearbox sets it every tick) is how much faster than the
//     shaft on its output face: the torque solver's post_build reads m_gear_ratio and m_clutch_factor every tick
//     (node mass x clutch x ratio^2, node rps x clutch / ratio), so the ratio can slide tick by tick.
//   - The stock tick: ratio = m_gear_ratios[gear] x output factor / input factor x (-1 with Reverse ticked); neutral
//     (gear -1) is clutch 0. The gear_shift mechanical input shifts up past +0.75 and down past -0.75 and re-arms
//     inside +-0.25. AnyCVT's tick replaces it for its own parts and keeps the shift feel: up steps R, N, D and down
//     steps back.
//   - The small engine's face is surface type torque_engine, the medium's torque_engine_b, the large's
//     torque_engine_c. The CVTs carry the stock gearbox, gearbox_b and gearbox_c faces, so each fits its engine.
//   - Engines make fuel from 360 to 8,000 rpm, full power from 6,400 rpm (findings: the torque curve), so the default
//     target sits in that band.
//
// Settings live in the gearbox's own saved fields, so worlds and saved vehicles keep them with no save format change
// and a CVT loaded without the mod is simply gone (its definition is the mod's). m_gear_ratios is 8 s32 saved as
// gear_ratio_1..8 whatever the gear count: slots 0-4 hold the five Properties rows, 5 the selector, 7 a marker that
// says the part has had its defaults (a new gearbox starts with the stock 24, 18, 14, 10, 6, 4, 2, 1).
#include "anyapi_asset_json.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace anycvt {

// ------------------------------------------------------------------------------------------- sizes
struct Cell { int x, y, z; };
struct Size {
    const char* id;              // definition id
    const char* name;
    const char* stock_id;        // the stock gearbox it is built from
    const char* stock_mesh;      // rom/meshes/components/<stock_mesh>.mesh
    const char* mesh;            // ours, or nullptr when the stock mesh is used as it is
    const char* engine;          // for the description
    bool add_shifter;            // the stock part shifts by air: give it the small gearbox's shifter face and node
    Cell shifter;                // where it goes (on top, +y), cells
    double top_y;                // the stock mesh's top face, m (where the shifter connector sits)
    int tech_tier;
    double spawn_probability;
};
// Tiers as the stock gearboxes (2, 3, 4); rarity as AnyEngineParts' rule (Nate 2026-10-21): tier 3 0.15, tier 4 0.075.
constexpr Size kSizes[] = {
    {"anycvt_small", "Small CVT", "gearbox", "gear_box_a", nullptr, "small", false, {0, 1, 1}, 0.12, 2, 0.15},
    {"anycvt_medium", "Medium CVT", "gearbox_b", "gear_box_b", "anycvt_medium", "medium", true, {0, 2, 3}, 0.20, 3, 0.15},
    {"anycvt_large", "Large CVT", "gearbox_c", "gear_box_c", "anycvt_large", "large", true, {0, 3, 4}, 0.28, 4, 0.075},
};
constexpr int kSizeCount = int(sizeof(kSizes) / sizeof(kSizes[0]));

inline int size_of_id(const char* id, size_t length) {
    for (int i = 0; i < kSizeCount; ++i) {
        size_t n = std::strlen(kSizes[i].id);
        if (id && length == n && std::memcmp(id, kSizes[i].id, n) == 0) return i;
    }
    return -1;
}
inline std::string mesh_path(const Size& s) { return std::string("meshes/components/") + (s.mesh ? s.mesh : s.stock_mesh) + ".mesh"; }
inline std::string stock_mesh_path(const Size& s) { return std::string("meshes/components/") + s.stock_mesh + ".mesh"; }

// ------------------------------------------------------------------------------------------- settings
// One Properties row: the slot of m_gear_ratios it edits, its range, its default, and the label. The label is a
// stock e_localization_string id (the row shows that text where the client hook is missing) and the text AnyCVT
// shows in its place.
struct Setting { int slot; int32_t min, max, fallback; int32_t label; const char* text; };
enum : int { kLow, kHigh, kTarget, kResponse, kEngage, kSettingCount };
constexpr Setting kSettings[kSettingCount] = {
    {0, 1, 60, 24, 341, "Low ratio"},           // property_gear_ratio_1
    {1, 1, 60, 1, 342, "High ratio"},           // property_gear_ratio_2
    {2, 1000, 8000, 6800, 326, "Target RPM"},   // property_max
    {3, 1, 10, 5, 351, "Response"},             // property_stiffness
    {4, 400, 5000, 1500, 325, "Engage RPM"},    // property_min
};
constexpr int kSelectorSlot = 5, kMarkerSlot = 7, kSlots = 8;
constexpr int32_t kMarker = 2029;               // in slot 7 once the defaults are written (the stock default there is 1)

// Which of our rows a client row is: its label and range together (no stock row has those three).
inline int setting_of_row(int32_t label, int32_t min, int32_t max) {
    for (int i = 0; i < kSettingCount; ++i)
        if (kSettings[i].label == label && kSettings[i].min == min && kSettings[i].max == max) return i;
    return -1;
}

inline int32_t clamp_s32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

struct Settings { double low = 24, high = 1, target_rpm = 6800, response = 5, engage_rpm = 1500; };
// Gives a new CVT its defaults (true when it did); `slots` is m_gear_ratios.
inline bool ensure_defaults(int32_t* slots) {
    if (slots[kMarkerSlot] == kMarker) return false;
    for (const Setting& s : kSettings) slots[s.slot] = s.fallback;
    slots[kSelectorSlot] = 0;
    slots[kMarkerSlot] = kMarker;
    return true;
}
inline Settings read_settings(const int32_t* slots) {
    auto get = [&](int i) { const Setting& s = kSettings[i]; return double(clamp_s32(slots[s.slot], s.min, s.max)); };
    Settings out{get(kLow), get(kHigh), get(kTarget), get(kResponse), get(kEngage)};
    if (out.high > out.low) { double t = out.high; out.high = out.low; out.low = t; }   // either way round works
    return out;
}

// ------------------------------------------------------------------------------------------- the selector
// R = -1, N = 0, D = 1, stepped by the gear_shift link exactly as the stock gearbox steps its gears.
struct Shifter { bool armed = true; };
constexpr double kShiftOn = 0.75, kShiftRearm = 0.25;
inline int step_selector(Shifter& s, int selector, double link) {
    if (!std::isfinite(link)) return selector;
    if (std::fabs(link) < kShiftRearm) { s.armed = true; return selector; }
    if (!s.armed) return selector;
    if (link > kShiftOn) { s.armed = false; return selector < 1 ? selector + 1 : 1; }
    if (link < -kShiftOn) { s.armed = false; return selector > -1 ? selector - 1 : -1; }
    return selector;
}
inline int clamp_selector(int32_t v) { return v < -1 ? -1 : v > 1 ? 1 : int(v); }

// ------------------------------------------------------------------------------------------- one tick
struct Inputs {
    int selector = 0;
    bool engine = false;          // an engine drove this CVT in the last few ticks
    double engine_rpm = 0;        // |engine rps| x 60
    double throttle = 0;          // the engine's, 0..1
    double output_rps = 0;        // the shaft on the output face (signed); 0 with nothing mated
    double direction = 1;         // sign the output turns in D: output factor / input factor x (-1 with Reverse ticked)
    double dt = 1.0 / 60.0;
};
struct State { double ratio = 0; };   // the ratio it has slid to (0 = not started: starts at the low ratio)
struct Outputs {
    double ratio = 0;             // the ratio it holds (positive)
    double node_ratio = 0;        // what the torque node gets: ratio x direction, negative for R
    double clutch = 0;            // 0..1
    double goal_rpm = 0;          // the engine speed it is holding for
};
constexpr double kStopRps = 2.0;  // R and D wait for the output to slow below this when it turns the other way

// The engine speed D holds: the engage RPM with the throttle shut, rising to the target RPM at full throttle.
inline double goal_rpm(const Settings& s, double throttle) {
    double t = std::isfinite(throttle) ? (throttle < 0 ? 0 : throttle > 1 ? 1 : throttle) : 0;
    double target = s.target_rpm > s.engage_rpm ? s.target_rpm : s.engage_rpm;
    return s.engage_rpm + (target - s.engage_rpm) * t;
}
// A centrifugal clutch: open below the engage RPM, fully closed a band above it, smooth in between.
inline double clutch_at(const Settings& s, double engine_rpm) {
    double band = s.engage_rpm * 0.2 > 250 ? s.engage_rpm * 0.2 : 250;
    double x = (engine_rpm - s.engage_rpm) / band;
    if (!std::isfinite(x) || x <= 0) return 0;
    if (x >= 1) return 1;
    return x * x * (3 - 2 * x);
}
// How fast the ratio slides: Response 1 takes about 1.7 s to cover most of a change, 10 about 0.17 s.
inline double slide_rate(const Settings& s) { return 0.6 * s.response; }

inline Outputs step(const Settings& s, State& st, const Inputs& in) {
    Outputs o;
    double dt = std::isfinite(in.dt) && in.dt > 0 ? in.dt : 1.0 / 60.0;
    if (!(st.ratio > 0) || !std::isfinite(st.ratio)) st.ratio = s.low;
    o.goal_rpm = goal_rpm(s, in.engine ? in.throttle : 0);
    // The ratio that puts the engine at the goal for this output speed (the low ratio when stopped), between the
    // two ends. It follows the output in N too, so D engages at the right ratio while rolling. Output / Input works
    // as a fixed stage after the belt, as on the stock gearbox, so the belt ratio is the engine's over it.
    double dir = std::isfinite(in.direction) && in.direction != 0 ? in.direction : 1.0;
    double out = std::fabs(in.output_rps);
    double wanted = in.selector < 0 ? s.low : out > 0.05 ? (o.goal_rpm / 60.0) / out / std::fabs(dir) : s.low;
    if (!std::isfinite(wanted) || wanted > s.low) wanted = s.low;
    if (wanted < s.high) wanted = s.high;
    st.ratio += (wanted - st.ratio) * (1 - std::exp(-slide_rate(s) * dt));
    if (st.ratio > s.low) st.ratio = s.low;
    if (st.ratio < s.high) st.ratio = s.high;
    o.ratio = st.ratio;
    double sign = in.selector < 0 ? -1.0 : 1.0;
    o.node_ratio = st.ratio * dir * sign;
    // The clutch: shut in N or with no engine; in R and D it bites with engine speed, but not while the output still
    // turns the other way faster than kStopRps (it waits as the stock clutch would have to).
    if (in.selector == 0 || !in.engine) return o;
    double forward = in.output_rps * (dir > 0 ? 1.0 : -1.0) * sign;   // + when the output already turns the selected way
    if (forward < -kStopRps) return o;
    o.clutch = clutch_at(s, in.engine_rpm);
    return o;
}

// ------------------------------------------------------------------------------------------- definitions
// Each CVT is the stock gearbox of its size, read from the game's own rom/data/vehicle_component_definitions.json
// when the game starts (so no game data ships with the mod and every face stays exactly stock), with:
//   - our id, name, description, class "gearbox", tier and spawn chance, and AnyCVT's data channels;
//   - no stretching (a CVT has no gear count), so the size is the stock part's unstretched size;
//   - on the medium and large, the small gearbox's shifter: its mechanical face moved to the top of the case (the port
//     face it lands on is split around it) and its gear_shift node, and our mesh with the connector on it.
inline std::string description_of(const Size& s) {
    return std::string("Continuously variable. Fits a ") + s.engine +
           " engine. Shift by mechanical link like a gearbox: above 0.75 steps R, N, D up, below -0.75 steps down, "
           "return to centre between shifts. In D it slides between the low and high ratio to hold the engine at the "
           "target RPM (lower at part throttle); the clutch bites at the engage RPM. R uses the low ratio backwards. "
           "Ratios, RPMs and response set in properties. Output current_gear reads -1 in R, 0 in N, 1 in D.";
}

inline std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) < 32) continue;
        else out += c;
    }
    return out + "\"";
}
inline std::string slice(std::string_view text, const assetjson::Value& v) { return std::string(text.substr(v.begin, v.end - v.begin)); }
inline int as_int(const assetjson::Value& v, int fallback) { return v.kind == 'd' ? int(std::lround(v.number)) : fallback; }
inline bool vec3(const assetjson::Value& v, int out[3]) {
    if (v.kind != 'a' || v.array.size() != 3) return false;
    for (int i = 0; i < 3; ++i) { if (v.array[i].kind != 'd') return false; out[i] = int(std::lround(v.array[i].number)); }
    return true;
}
// An object with some keys replaced: every key of `v` as written in `text` except those in `drop`, then `extra`
// ("\"key\": value" pieces).
inline std::string object_with(std::string_view text, const assetjson::Value& v, const std::vector<std::string>& drop, const std::vector<std::string>& extra) {
    std::string out = "{";
    bool first = true;
    for (auto& [key, value] : v.object) {
        bool skip = false;
        for (auto& d : drop) if (d == key) skip = true;
        if (skip) continue;
        out += (first ? "" : ", ") + json_string(key) + ": " + slice(text, value);
        first = false;
    }
    for (auto& e : extra) { out += (first ? "" : ", ") + e; first = false; }
    return out + "}";
}
inline std::string pos_json(int x, int y, int z) {
    char b[64];
    std::snprintf(b, sizeof b, "\"pos\": [%d, %d, %d]", x, y, z);
    return b;
}

// The port face on top of the case, split around the shifter cell: the rows before and after it, and the cells left
// and right of it in its row. Sizes on a y face are [x, z].
struct Rect { int x, z, sx, sz; };
inline std::vector<Rect> split_around(const Rect& r, int cx, int cz) {
    if (cx < r.x || cx >= r.x + r.sx || cz < r.z || cz >= r.z + r.sz) return {r};
    std::vector<Rect> out;
    if (cz > r.z) out.push_back({r.x, r.z, r.sx, cz - r.z});
    if (r.z + r.sz - 1 > cz) out.push_back({r.x, cz + 1, r.sx, r.z + r.sz - 1 - cz});
    if (cx > r.x) out.push_back({r.x, cz, cx - r.x, 1});
    if (r.x + r.sx - 1 > cx) out.push_back({cx + 1, cz, r.x + r.sx - 1 - cx, 1});
    return out;
}

constexpr int kDirYPos = 3;
inline std::string surfaces_with_shifter(std::string_view text, const assetjson::Value& surfaces, const assetjson::Value* small_mechanical, const Cell& c) {
    std::string out = "[";
    bool first = true;
    auto add = [&](const std::string& s) { out += (first ? "" : ", ") + s; first = false; };
    for (const assetjson::Value& f : surfaces.array) {
        int p[3]{};
        bool top_port = f.kind == 'o' && f.get("type").text == "port" && as_int(f.get("dir"), -1) == kDirYPos && vec3(f.get("pos"), p) && p[1] == c.y;
        if (!top_port) { add(slice(text, f)); continue; }
        const assetjson::Value& size = f.get("size");
        Rect r{p[0], p[2], 1, 1};
        if (size.kind == 'a' && size.array.size() == 2) { r.sx = as_int(size.array[0], 1); r.sz = as_int(size.array[1], 1); }
        for (const Rect& piece : split_around(r, c.x, c.z)) {
            char sz[48];
            std::snprintf(sz, sizeof sz, "\"size\": [%d, %d]", piece.sx, piece.sz);
            add(object_with(text, f, {"pos", "size"}, {pos_json(piece.x, c.y, piece.z), sz}));
        }
    }
    if (small_mechanical) add(object_with(text, *small_mechanical, {"pos"}, {pos_json(c.x, c.y, c.z)}));
    else add("{" + pos_json(c.x, c.y, c.z) + ", \"dir\": 3, \"gender\": 2, \"type\": \"mechanical\"}");
    return out + "]";
}

constexpr const char* kDataDescriptors =
    "[{\"type\": \"type_f64_output\", \"name\": \"ratio\", \"documentation\": \"The ratio it is at (engine turns per output turn).\"}, "
    "{\"type\": \"type_f64_output\", \"name\": \"engine_rpm\", \"documentation\": \"Engine speed, rpm.\"}, "
    "{\"type\": \"type_f64_output\", \"name\": \"target_rpm\", \"documentation\": \"Engine speed it is holding for now, rpm.\"}, "
    "{\"type\": \"type_f64_output\", \"name\": \"selector\", \"documentation\": \"-1 in R, 0 in N, 1 in D.\"}, "
    "{\"type\": \"type_f64_output\", \"name\": \"clutch_factor\", \"documentation\": \"0 open to 1 closed.\"}, "
    "{\"type\": \"type_s32_output\", \"name\": \"current_gear\", \"documentation\": \"-1 in R, 0 in N, 1 in D.\"}]";

inline const assetjson::Value* find_definition(const assetjson::Value& definitions, const char* id) {
    for (const assetjson::Value& d : definitions.array)
        if (d.kind == 'o' && d.get("id").text == id) return &d;
    return nullptr;
}
inline const assetjson::Value* find_of_type(const assetjson::Value& list, const char* type) {
    for (const assetjson::Value& v : list.array)
        if (v.kind == 'o' && v.get("type").text == type) return &v;
    return nullptr;
}

// Our definitions file from the stock definitions file's text. `meshes[i]`: the mesh path for size i (ours, or the
// stock mesh when ours could not be written). Empty, with `error` set, when the stock gearboxes are not as expected.
inline std::string definitions_json(std::string_view stock_text, const std::string meshes[kSizeCount], std::string* error = nullptr) {
    auto fail = [&](const char* why) { if (error) *error = why; return std::string(); };
    assetjson::Value root;
    try { root = assetjson::Parser(stock_text).parse(); } catch (...) { return fail("the stock definitions file is not valid JSON"); }
    const assetjson::Value& definitions = root.get("definitions");
    if (definitions.kind != 'a') return fail("the stock definitions file has no definitions list");
    const assetjson::Value* small_def = find_definition(definitions, kSizes[0].stock_id);
    if (!small_def) return fail("the stock small gearbox is missing");
    const assetjson::Value* small_mechanical = find_of_type(small_def->get("surfaces"), "mechanical");
    const assetjson::Value* small_shift = find_of_type(small_def->get("logic_nodes"), "mechanical_in");
    std::string out = "{\n  \"definitions\": [\n";
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        const assetjson::Value* stock = find_definition(definitions, s.stock_id);
        if (!stock || stock->get("surfaces").kind != 'a' || stock->get("zones").kind != 'a') return fail("a stock gearbox is missing or has no faces");
        char tier[96];
        std::snprintf(tier, sizeof tier, "\"tech_tier\": %d, \"spawn_probability\": %.3f", s.tech_tier, s.spawn_probability);
        std::vector<std::string> drop = {"id", "name", "description", "class", "mesh_static", "data_descriptors", "tech_tier", "spawn_probability",
                                         "mode_x", "mode_y", "mode_z", "interval", "center_stretch", "ext_max"};
        std::vector<std::string> extra = {
            "\"id\": " + json_string(s.id), "\"name\": " + json_string(s.name), "\"description\": " + json_string(description_of(s)),
            "\"class\": \"gearbox\"", "\"data_descriptors\": " + std::string(kDataDescriptors), tier};
        const assetjson::Value& mesh = stock->get("mesh_static");
        std::string mesh_entry = "\"mesh_path\": " + json_string(meshes[i]);
        extra.push_back("\"mesh_static\": " + (mesh.kind == 'o' ? object_with(stock_text, mesh, {"mesh_path"}, {mesh_entry}) : "{" + mesh_entry + "}"));
        if (s.add_shifter) {
            drop.insert(drop.end(), {"surfaces", "logic_nodes", "tags_generated"});
            extra.push_back("\"surfaces\": " + surfaces_with_shifter(stock_text, stock->get("surfaces"), small_mechanical, s.shifter));
            std::string node = small_shift ? object_with(stock_text, *small_shift, {"pos"}, {pos_json(s.shifter.x, s.shifter.y, s.shifter.z)})
                                           : "{\"id\": \"gear_shift\", " + pos_json(s.shifter.x, s.shifter.y, s.shifter.z) + ", \"direction\": 3, \"type\": \"mechanical_in\"}";
            extra.push_back("\"logic_nodes\": [" + node + "]");
            std::string tags = stock->get("tags_generated").text;
            if (tags.find("mechanical") == std::string::npos) tags = "mechanical," + tags;
            extra.push_back("\"tags_generated\": " + json_string(tags));
        }
        out += "    " + object_with(stock_text, *stock, drop, extra) + (i + 1 < kSizeCount ? ",\n" : "\n");
    }
    return out + "  ]\n}\n";
}

}  // namespace anycvt
