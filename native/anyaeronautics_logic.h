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

// The two nozzles Nate picked (2026-11-23, options 1 and 3 of the three looks shown): straight jet nozzles with their
// own meshes (anyaeronautics_mesh.h), built on the stock jet exhaust's class, faces and mass. Thrust comes from the
// fuel the engine burns (fuel_thrust_newtons); a nozzle's thrust specific fuel consumption (kg of fuel per N of thrust
// per hour) sets how much push it gets from that fuel. Real turbojets run about 0.08 to 0.1.
struct Size { const char* id; const char* name; double tsfc; const char* mesh; };
inline constexpr Size kSizes[] = {
    {"anyaero_nozzle_cone", "Cone Nozzle", 0.10, "anyaero_nozzle_cone_a"},
    {"anyaero_nozzle_fighter", "Fighter Nozzle", 0.08, "anyaero_nozzle_fighter_a"},
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
// Thrust from the fuel the engine burns, like a real jet: fuel mass per second / TSFC. The combustion chamber's
// fuel_flow is taken as m^3/s of petrol (unproven units: the first test read 0.00176 at full throttle and 0.00032 at
// 17%, which gives a believable 1.3 kg/s). So throttle, fuel starvation, damage and power taken by a gearbox all show
// in the thrust. The engine's speed barely follows throttle with no load, so it is not used for thrust.
constexpr double kFuelDensity = 750.0;       // kg/m^3, petrol
constexpr double kMaxNewtons = 250000.0;     // safety cap
inline double fuel_thrust_newtons(double tsfc_kg_per_n_hour, double fuel_flow) {
    if (!std::isfinite(fuel_flow) || !std::isfinite(tsfc_kg_per_n_hour) || tsfc_kg_per_n_hour <= 0 || fuel_flow <= 0) return 0;
    return std::min(kMaxNewtons, fuel_flow * kFuelDensity / (tsfc_kg_per_n_hour / 3600.0));
}
// Spool lag: thrust follows the fuel-burn target with a first-order lag, slower going up than coming down.
constexpr double kSpoolUpSeconds = 1.6, kSpoolDownSeconds = 0.8;
inline double lag_thrust(double current, double target, double dt) {
    if (!std::isfinite(current) || current < 0) current = 0;
    if (!std::isfinite(target) || target < 0) target = 0;
    if (!std::isfinite(dt) || dt <= 0) return current;
    dt = std::min(dt, 0.25);
    double tau = target > current ? kSpoolUpSeconds : kSpoolDownSeconds;
    return target + (current - target) * std::exp(-dt / tau);
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
    return std::string("Straight jet nozzle that pushes. Fit it behind a jet combustion chamber in place of the stock exhaust: "
                       "thrust comes from the fuel the engine burns, so the engine's throttle input sets it and it builds up as the engine spools. ") +
           (s.tsfc < 0.09 ? "Fighter nozzle: more thrust from the same fuel. " : "") + "Outputs thrust (kN) and spool (0..1).";
}

inline std::string mesh_path(const Size& s) { return std::string("meshes/components/") + s.mesh + ".mesh"; }

// Our definitions file from the stock definitions file's text: one per nozzle, each a copy of the first stock jet
// exhaust with a new id, name, description, mesh (meshes[i]; empty keeps the stock mesh) and the two extra data
// outputs. Empty, with `error` set, when the stock file has no jet exhaust.
inline std::string definitions_json(std::string_view stock_text, const std::vector<std::string>& meshes = {}, std::string* error = nullptr, std::string* stock_id = nullptr) {
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
        if (size_t(i) < meshes.size() && !meshes[i].empty()) {
            drop.push_back("mesh_static");
            const assetjson::Value& mesh = stock->get("mesh_static");
            std::string entry = "\"mesh_path\": " + json_string(meshes[i]);
            extra.push_back("\"mesh_static\": " + (mesh.kind == 'o' ? object_with(stock_text, mesh, {"mesh_path"}, {entry}) : "{" + entry + "}"));
        }
        out += "    " + object_with(stock_text, *stock, drop, extra) + (i + 1 < kSizeCount ? ",\n" : "\n");
    }
    return out + "  ]\n}\n";
}

}  // namespace anyaeronautics
