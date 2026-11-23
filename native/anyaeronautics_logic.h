#pragma once
// AnyAeronautics pure logic: the Thrust Nozzle definitions (built from the game's own jet exhaust definition), their
// data channels and the thrust maths. No game or Windows dependencies, so tests/anyaeronautics_logic_test.cpp runs
// it on any platform.
#include "anyapi_asset_json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace anyaeronautics {

constexpr const char* kStockClass = "jet_engine_exhaust";

// Three nozzle strengths for the first test batch, so the thrust can be judged in game without a rebuild. Each is
// the stock jet exhaust (same mesh, faces, mass) with real thrust. Max thrust is at full spool (the combustion
// chamber's speed factor at 1).
struct Size { const char* id; const char* name; double max_newtons; };
inline constexpr Size kSizes[] = {
    {"anyaero_nozzle_s", "Thrust Nozzle (5 kN)", 5000.0},
    {"anyaero_nozzle_m", "Thrust Nozzle (15 kN)", 15000.0},
    {"anyaero_nozzle_l", "Thrust Nozzle (40 kN)", 40000.0},
};
constexpr int kSizeCount = int(sizeof kSizes / sizeof kSizes[0]);

inline int size_of_id(const char* id, size_t length) {
    for (int i = 0; i < kSizeCount; ++i) {
        size_t l = std::char_traits<char>::length(kSizes[i].id);
        if (l == length && std::char_traits<char>::compare(kSizes[i].id, id, l) == 0) return i;
    }
    return -1;
}

// Data channels added to the stock exhaust's own (outputs only; the throttle is the combustion chamber's input).
struct Channel { const char* name; const char* help; };
enum Slot { kThrust, kSpool, kSlots };
inline constexpr Channel kChannels[kSlots] = {
    {"thrust", "Thrust in kN"},
    {"spool", "Engine spool 0..1 (the combustion chamber's speed factor)"},
};
inline int channel_index(const char* name, size_t length) {
    for (int i = 0; i < kSlots; ++i) {
        size_t l = std::char_traits<char>::length(kChannels[i].name);
        if (l == length && std::char_traits<char>::compare(kChannels[i].name, name, l) == 0) return i;
    }
    return -1;
}

// ------------------------------------------------------------------ thrust
// Thrust grows with the square of spool (a turbojet's thrust goes roughly with shaft speed squared), so idle gives a
// light push and full throttle the full rating. Spool lag, throttle and fuel burn are the stock engine's own.
inline double thrust_newtons(double max_newtons, double spool) {
    if (!std::isfinite(spool) || !std::isfinite(max_newtons) || max_newtons <= 0) return 0;
    double s = std::clamp(spool, 0.0, 1.0);
    return max_newtons * s * s;
}

struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// mat34 as the game stores it: columns right, up, forward, translation (12 doubles).
struct Mat34 { double m[12]{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; };
inline Vec3 column(const Mat34& a, int c) { return {a.m[c * 3], a.m[c * 3 + 1], a.m[c * 3 + 2]}; }
inline Vec3 apply_rotation(const Mat34& a, Vec3 v) { return column(a, 0) * v.x + column(a, 1) * v.y + column(a, 2) * v.z; }
inline Mat34 compose(const Mat34& outer, const Mat34& inner) {   // outer * inner
    Mat34 r;
    for (int c = 0; c < 3; ++c) { Vec3 v = apply_rotation(outer, column(inner, c)); r.m[c * 3] = v.x; r.m[c * 3 + 1] = v.y; r.m[c * 3 + 2] = v.z; }
    Vec3 t = apply_rotation(outer, column(inner, 3)) + column(outer, 3);
    r.m[9] = t.x; r.m[10] = t.y; r.m[11] = t.z;
    return r;
}
inline bool finite(const Mat34& a) { for (double v : a.m) if (!std::isfinite(v)) return false; return true; }

// The push direction: from the nozzle toward the engine in front of it (the exhaust gas leaves the other way). The
// stock engine chain is straight, so the upstream part's centre gives the engine axis without guessing which local
// axis the nozzle mesh uses. False when the two centres coincide or are not finite.
inline bool push_direction(Vec3 nozzle, Vec3 upstream, Vec3& out) {
    Vec3 d = upstream - nozzle;
    double l = length(d);
    if (!finite(d) || !(l > 1e-4) || l > 50) return false;
    out = d * (1.0 / l);
    return true;
}

// ------------------------------------------------------------------ definitions
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
// Every key of `v` as written in `text` except those in `drop`, then `extra` ("\"key\": value" pieces).
inline std::string object_with(std::string_view text, const assetjson::Value& v, const std::vector<std::string>& drop, const std::vector<std::string>& extra) {
    std::string out = "{";
    bool first = true;
    for (auto& [key, value] : v.object) {
        if (std::find(drop.begin(), drop.end(), key) != drop.end()) continue;
        out += (first ? "" : ", ") + json_string(key) + ": " + slice(text, value);
        first = false;
    }
    for (auto& e : extra) { out += (first ? "" : ", ") + e; first = false; }
    return out + "}";
}

inline std::string description_of(const Size& s) {
    char kn[32];
    std::snprintf(kn, sizeof kn, "%.0f kN", s.max_newtons / 1000.0);
    return std::string("Jet exhaust that pushes. Fit it behind a jet combustion chamber in place of the stock exhaust: "
                       "the engine's throttle input sets the thrust, up to ") + kn +
           " at full spool. Spool-up and fuel burn are the engine's own. Outputs thrust (kN) and spool (0..1).";
}

// Our definitions file from the stock definitions file's text: one nozzle per size, each a copy of the first stock
// jet exhaust with a new id, name, description and the two extra data outputs. Empty, with `error` set, when the
// stock file has no jet exhaust.
inline std::string definitions_json(std::string_view stock_text, std::string* error = nullptr, std::string* stock_id = nullptr) {
    auto fail = [&](const char* why) { if (error) *error = why; return std::string(); };
    assetjson::Value root;
    try { root = assetjson::Parser(stock_text).parse(); } catch (...) { return fail("the stock definitions file is not valid JSON"); }
    const assetjson::Value& definitions = root.get("definitions");
    if (definitions.kind != 'a') return fail("the stock definitions file has no definitions list");
    const assetjson::Value* stock = nullptr;
    for (const assetjson::Value& d : definitions.array)
        if (d.kind == 'o' && d.get("class").text == kStockClass) { stock = &d; break; }
    if (!stock) return fail("the stock definitions file has no jet exhaust");
    if (stock_id) *stock_id = stock->get("id").text;
    std::string descriptors = "[";
    const assetjson::Value& own = stock->get("data_descriptors");
    bool first = true;
    if (own.kind == 'a')
        for (const assetjson::Value& d : own.array) {
            if (d.kind != 'o' || channel_index(d.get("name").text.data(), d.get("name").text.size()) >= 0) continue;
            descriptors += (first ? "" : ", ") + slice(stock_text, d);
            first = false;
        }
    for (const Channel& c : kChannels) {
        descriptors += std::string(first ? "" : ", ") + "{\"type\": \"type_f64_output\", \"name\": " + json_string(c.name) + "}";
        first = false;
    }
    descriptors += "]";
    std::string out = "{\n  \"definitions\": [\n";
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        std::vector<std::string> drop = {"id", "name", "description", "data_descriptors"};
        std::vector<std::string> extra = {"\"id\": " + json_string(s.id), "\"name\": " + json_string(s.name),
                                          "\"description\": " + json_string(description_of(s)), "\"data_descriptors\": " + descriptors};
        out += "    " + object_with(stock_text, *stock, drop, extra) + (i + 1 < kSizeCount ? ",\n" : "\n");
    }
    return out + "  ]\n}\n";
}

}  // namespace anyaeronautics
