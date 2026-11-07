#pragma once
// AnyVehicleOverhaul race and drag wheels (Nate 2026-11-07): new tyre items that fit the stock wheel and tyre mount
// parts and grip harder than stock tyres. The game has no grip value per tyre (every tyre grips the same), so the
// host ticks a wheel that wears one of these with a copy of its tuning whose grip is scaled (avo_wheels_mod.cpp).
// Pure C++: the catalogue, the item entry rewrite and the grip numbers, tested in tests/avo_wheels_logic_test.cpp.
#include "anyapi_asset_json.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace avowheels {

enum Kind { kRace = 0, kDragSlick = 1, kDragFront = 2, kKinds = 3 };
enum Style { kDrag5 = 0, kRace6 = 1, kMesh = 2, kStyles = 3 };

struct Tyre {
    Kind kind;
    double radius, width;       // the tyre size table entry (radius sets the wheel's rolling radius)
    double grip;                // x stock grip and friction-circle limit
    int tier;
    const char* spawn;          // spawn_probability, as written into the item entry
};
inline Tyre tyre(Kind k) {
    switch (k) {
    case kDragSlick: return {k, 0.36, 0.32, 2.5, 4, "0.075"};
    case kDragFront: return {k, 0.33, 0.10, 1.0, 3, "0.1"};
    default: return {k, 0.33, 0.24, 1.5, 3, "0.1"};
    }
}
inline const char* kind_key(Kind k) { return k == kDragSlick ? "drag_slick" : k == kDragFront ? "drag_front" : "race"; }
inline const char* kind_name(Kind k) { return k == kDragSlick ? "Drag Slick" : k == kDragFront ? "Drag Front Runner" : "Race Wheel"; }
inline const char* style_key(Style s) { return s == kDrag5 ? "5spoke" : s == kRace6 ? "6spoke" : "mesh"; }
inline const char* style_name(Style s) { return s == kDrag5 ? "5-spoke" : s == kRace6 ? "6-spoke" : "mesh"; }

// Item ids: never rename (saves keep the tyre on the wheel by id).
inline std::string item_id(Kind k, Style s) { return std::string("avo_wheel_") + kind_key(k) + "_" + style_key(s); }
inline std::string mesh_name(Kind k, Style s) { return item_id(k, s); }

struct Item { Kind kind; Style style; std::string id; };
inline std::vector<Item> catalogue() {
    std::vector<Item> out;
    for (int s = 0; s < kStyles; ++s)
        for (int k = 0; k < kKinds; ++k) out.push_back({Kind(k), Style(s), item_id(Kind(k), Style(s))});
    return out;
}
inline const Item* find(std::string_view id) {
    static const std::vector<Item> all = catalogue();
    if (id.size() < 10 || id.substr(0, 10) != "avo_wheel_") return nullptr;
    for (const auto& i : all) if (i.id == id) return &i;
    return nullptr;
}
inline double grip_of(std::string_view id) { const Item* i = find(id); return i ? tyre(i->kind).grip : 0; }

inline std::string description(Kind k) {
    const Tyre t = tyre(k);
    char b[320];
    if (k == kDragFront)
        std::snprintf(b, sizeof b, "Skinny front runner for drag cars: light and low drag, stock grip. Fits the wheel and tyre mount components. Radius %.2f m, width %.2f m. Cannot be stored in a container.", t.radius, t.width);
    else
        std::snprintf(b, sizeof b, "%s with %.1fx the grip of a stock tyre. Fits the wheel and tyre mount components. Radius %.2f m, width %.2f m. Cannot be stored in a container.",
                      k == kDragSlick ? "Wide drag slick" : "Sticky race tyre", t.grip, t.radius, t.width);
    return b;
}

// Our item entry from a stock wheel entry (the player's own rom/data/inventory_definitions.json, read at start-up;
// no game data ships with the mod). `full` and `rim` are our mesh paths; the damaged tyre shows the bare rim.
inline std::string make_entry(const std::string& stock, const Item& item, const std::string& full, const std::string& rim) {
    assetjson::Value v;
    try { v = assetjson::Parser(stock).parse(); } catch (const std::exception&) { return {}; }
    if (v.kind != 'o' || stock.empty() || stock.back() != '}') return {};
    const Tyre t = tyre(item.kind);
    auto quote = [](const std::string& s) {
        std::string q = "\"";
        for (char c : s) { if (c == '"' || c == '\\') q += '\\'; q += c; }
        return q + "\"";
    };
    auto vec = [](double x, double y, double z) { char b[96]; std::snprintf(b, sizeof b, "[%.4f, %.4f, %.4f]", x, y, z); return std::string(b); };
    struct Edit { size_t begin, end; std::string text; };
    std::vector<Edit> edits;
    std::string added;
    auto set_in = [&](const assetjson::Value& obj, const char* key, const std::string& value, bool add) {
        auto it = obj.object.find(key);
        if (it != obj.object.end()) edits.push_back({it->second.begin, it->second.end, value});
        else if (add) added += std::string(", \"") + key + "\": " + value;
    };
    auto set = [&](const char* key, const std::string& value) { set_in(v, key, value, true); };
    set("id", quote(item.id));
    set("name", quote(std::string(kind_name(item.kind)) + " (" + style_name(item.style) + ")"));
    set("description", quote(description(item.kind)));
    set("mesh_file", quote(full));
    set("meshes_dynamic", "[{\"id\": \"damaged_wheel\", \"mesh_file\": " + quote(full) + "}, {\"id\": \"damaged_tyre\", \"mesh_file\": " + quote(rim) + "}]");
    set("tech_tier", std::to_string(t.tier));
    set("spawn_probability", t.spawn);
    set("loot_group", "\"mechanical\"");
    set("patterns", "{\"patterns\": []}");
    const assetjson::Value& floor = v.get("floor");
    if (floor.kind == 'o') {
        const double r = t.radius + 0.01;
        set_in(floor, "physics_min", vec(-r, 0.0, -r), false);
        set_in(floor, "physics_max", vec(r, t.width + 0.07, r), false);
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    for (size_t i = 1; i < edits.size(); ++i) if (edits[i].end > edits[i - 1].begin) return {};   // overlapping edits: unexpected entry
    std::string out = stock;
    for (const auto& e : edits) out.replace(e.begin, e.end - e.begin, e.text);
    out.insert(out.size() - 1, added);
    return out;
}

// The tuning fields the grip scales (vehicle_element_definition.wheel, 0.2.0): sliding grip and the friction-circle
// limit. The static "memory" hold that keeps a parked car on a slope is left alone.
inline constexpr long kGripFriction = 232, kFrictionCircleMu = 248;
struct Grip { double grip_friction, circle_mu; };
inline Grip scaled(Grip stock, double k) { return {stock.grip_friction * k, stock.circle_mu * k}; }

}  // namespace avowheels
