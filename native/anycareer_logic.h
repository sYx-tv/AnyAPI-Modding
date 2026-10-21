#pragma once
// AnyCareer logic with no game calls: the Cash definition rewrite and the first-pass price list. Unit-tested by
// tests/anycareer_logic_test.cpp.
#include "anyapi_asset_json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace anycareer::logic {

inline constexpr const char* kCashId = "anycareer_cash";
inline constexpr int kCashStack = 9999;

// e_loot_group, in the game's order (reference metadata).
inline constexpr const char* kLootGroups[] = {"none",     "medical",     "food",     "clothing",     "weapons",  "ammo",
                                              "mechanical", "electronics", "survival", "construction", "furniture"};
inline constexpr int kLootGroupCount = int(sizeof(kLootGroups) / sizeof(kLootGroups[0]));
inline const char* loot_group_name(int32_t g) { return g >= 0 && g < kLootGroupCount ? kLootGroups[g] : "?"; }

// Rewrites one stock ammo_box entry (its JSON text, every field kept) into the Cash definition. Fields it sets are
// replaced where they exist and added where they don't. Returns "" when the text isn't one JSON object.
inline std::string make_cash_entry(const std::string& entry) {
    assetjson::Value v;
    try { v = assetjson::Parser(entry).parse(); } catch (const std::exception&) { return {}; }
    if (v.kind != 'o' || entry.empty() || entry.back() != '}') return {};
    const std::pair<const char*, std::string> fields[] = {
        {"id", std::string("\"") + kCashId + "\""},
        {"name", "\"Cash\""},
        {"description", "\"Trader money. Stacks to 9999. Spend it at AnyCareer traders.\""},
        {"category", "\"miscellaneous\""},
        {"gun_magazine_capacity", std::to_string(kCashStack)},
        {"tech_tier", "99"},
        {"spawn_probability", "0.0"},
        {"loot_group", "\"none\""},
    };
    struct Edit { size_t begin, end; std::string text; };
    std::vector<Edit> edits;
    std::string added;
    for (const auto& [key, value] : fields) {
        auto it = v.object.find(key);
        if (it != v.object.end()) edits.push_back({it->second.begin, it->second.end, value});
        else added += std::string(", \"") + key + "\": " + value;
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    std::string out = entry;
    for (const auto& e : edits) out.replace(e.begin, e.end - e.begin, e.text);
    out.insert(out.size() - 1, added);
    try { assetjson::Parser(out).parse(); } catch (const std::exception&) { return {}; }
    return out;
}

// One item or part definition, as the price list sees it.
struct Entry {
    bool part = false;
    std::string id, name, cls, category;
    int32_t tech_tier = 0;
    double spawn_probability = 0;
    int32_t loot_group = 0;
    int price = 0;
    bool findable = false;
};

// First-pass price: a base per loot group, times (1 + tier), times rarity from the spawn probability (0.2 is
// "normal"; rarer items cost up to 4x, common ones down to half). Hand overrides come later.
inline int base_price(int32_t loot_group, bool part) {
    static constexpr int kBase[] = {10, 30, 10, 15, 80, 20, 40, 35, 20, 15, 25};
    int b = loot_group >= 0 && loot_group < kLootGroupCount ? kBase[loot_group] : 10;
    return part ? b + 10 : b;
}
inline int price(const Entry& e) {
    int tier = std::clamp(e.tech_tier, 1, 6);
    double rarity = e.spawn_probability > 0 ? std::clamp(0.2 / e.spawn_probability, 0.5, 4.0) : 4.0;
    return std::max(1, int(std::lround(base_price(e.loot_group, e.part) * (1 + tier) * rarity)));
}

// Can drop as loot by now: in a loot group, a spawn chance, and a tier the world has reached. Items with no tier
// set (0 or below) count as tier 1. Whether older tiers keep dropping is checked in game before missions rely on it.
inline bool findable(const Entry& e, int32_t loot_level) {
    if (e.loot_group <= 0 || e.spawn_probability <= 0 || e.id == kCashId) return false;
    int tier = e.tech_tier < 1 ? 1 : e.tech_tier;
    return tier <= loot_level;
}

inline std::string tsv_header() { return "kind\tid\tname\tclass\tcategory\tloot_group\ttech_tier\tspawn_probability\tprice\tfindable_now"; }
inline std::string clean(std::string s) {
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
inline std::string tsv_row(const Entry& e) {
    char prob[32];
    std::snprintf(prob, sizeof prob, "%.4f", e.spawn_probability);
    return std::string(e.part ? "part" : "item") + "\t" + clean(e.id) + "\t" + clean(e.name) + "\t" + clean(e.cls) + "\t" + clean(e.category) +
           "\t" + loot_group_name(e.loot_group) + "\t" + std::to_string(e.tech_tier) + "\t" + prob + "\t" + std::to_string(e.price) + "\t" +
           (e.findable ? "1" : "0");
}

}  // namespace anycareer::logic
