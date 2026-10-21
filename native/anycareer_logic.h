#pragma once
// AnyCareer logic with no game calls: the Cash definition rewrite, the first-pass price list, and the trader's
// placement, day count, stock, payments and saved state. Unit-tested by tests/anycareer_logic_test.cpp.
#include "anyapi_asset_json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
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

// Loot Cash: the definition's spawn chance (0.2 is a normal item), and what one find holds. Mostly a little, now and
// then more (Nate: "maybe 20 here and 100 there"): 55% 3-25, 30% 25-60, 12% 60-120, 3% 120-250.
inline constexpr const char* kCashLootChance = "0.12";
inline int loot_cash_amount(uint64_t r) {
    int roll = int(r % 100), span = int((r >> 16) % 1000);
    auto in = [&](int lo, int hi) { return lo + span % (hi - lo + 1); };
    return roll < 55 ? in(3, 25) : roll < 85 ? in(25, 60) : roll < 97 ? in(60, 120) : in(120, 250);
}

// Rewrites one stock ammo_box entry (its JSON text, every field kept) into the Cash definition. Fields it sets are
// replaced where they exist and added where they don't. With a mesh path, Cash gets that look (and no extra ammo
// meshes), and its floor physics box becomes `floor_min`..`floor_max`. Returns "" when the text isn't one JSON object.
// `loot` false keeps Cash out of loot (when the mod can't set found amounts, a find would hold a full 9999 stack).
inline std::string make_cash_entry(const std::string& entry, const std::string& mesh = {}, const double* floor_min = nullptr,
                                   const double* floor_max = nullptr, bool loot = true) {
    assetjson::Value v;
    try { v = assetjson::Parser(entry).parse(); } catch (const std::exception&) { return {}; }
    if (v.kind != 'o' || entry.empty() || entry.back() != '}') return {};
    const std::pair<const char*, std::string> fields[] = {
        {"id", std::string("\"") + kCashId + "\""},
        {"name", "\"Cash\""},
        {"description", "\"Trader money. Stacks to 9999. Spend it at AnyCareer traders.\""},
        {"category", "\"miscellaneous\""},
        {"gun_magazine_capacity", std::to_string(kCashStack)},
        // Found as loot in houses, offices and shops (the clothing group is only in the residential and office
        // furniture masks), in small amounts (loot_cash_amount, set by the mod's ammo_box init hook).
        {"tech_tier", loot ? "1" : "99"},
        {"spawn_probability", loot ? kCashLootChance : "0.0"},
        {"loot_group", loot ? "\"clothing\"" : "\"none\""},
    };
    struct Edit { size_t begin, end; std::string text; };
    std::vector<Edit> edits;
    std::string added;
    auto set = [&](const char* key, const std::string& value) {
        auto it = v.object.find(key);
        if (it != v.object.end()) edits.push_back({it->second.begin, it->second.end, value});
        else added += std::string(", \"") + key + "\": " + value;
    };
    for (const auto& [key, value] : fields) set(key, value);
    if (!mesh.empty()) {
        std::string quoted = "\"";
        for (char c : mesh) quoted += c == '\\' ? '/' : c;
        set("mesh_file", quoted + "\"");
        set("meshes_dynamic", "[]");
        const assetjson::Value& floor = v.get("floor");
        auto vec = [](const double* p) {
            char b[96];
            std::snprintf(b, sizeof b, "[%.4f, %.4f, %.4f]", p[0], p[1], p[2]);
            return std::string(b);
        };
        if (floor_min && floor_max && floor.kind == 'o')
            for (const auto& [key, p] : {std::pair<const char*, const double*>{"physics_min", floor_min}, {"physics_max", floor_max}}) {
                auto it = floor.object.find(key);
                if (it != floor.object.end()) edits.push_back({it->second.begin, it->second.end, vec(p)});
            }
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
// "normal"; rarer items cost up to 4x, common ones down to half), plus a quarter on top (Nate: "make everything a
// little more expensive"). What the trader pays and what jobs pay stay where they were before that markup.
inline int base_price(int32_t loot_group, bool part) {
    static constexpr int kBase[] = {10, 30, 10, 15, 80, 20, 40, 35, 20, 15, 25};
    int b = loot_group >= 0 && loot_group < kLootGroupCount ? kBase[loot_group] : 10;
    return part ? b + 10 : b;
}
inline constexpr double kMarkup = 1.25;
inline int price(const Entry& e) {
    int tier = std::clamp(e.tech_tier, 1, 6);
    double rarity = e.spawn_probability > 0 ? std::clamp(0.2 / e.spawn_probability, 0.5, 4.0) : 4.0;
    return std::max(1, int(std::lround(base_price(e.loot_group, e.part) * (1 + tier) * rarity * kMarkup)));
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


// ------------------------------------------------------------------ trader placement
struct Zone { double x{}, y{}, z{}; };

// Spots in the world tiles (spawn_zones: pos in world metres, and a filter such as gas_station, town, house).
struct Spot { Zone at; std::string filter; };

// Places a delivery can go: the building kinds of the stock tiles (test 6 listed them), not open-country spawn
// areas or one-off tutorial and starter spots.
inline constexpr const char* kPlaceKinds[] = {"town", "house", "gas_station", "farm", "industrial", "roadside", "office", "research", "sawmill"};
inline bool delivery_place(const std::string& filter) {
    for (const char* f : kPlaceKinds) if (filter == f) return true;
    return false;
}
// "gas_station" -> "gas station"
inline std::string place_name(std::string filter) {
    for (char& c : filter) if (c == '_') c = ' ';
    return filter.empty() ? "place" : filter;
}

// Only the spawn_zones array is parsed (tiles also hold thousands of props), found by bracket matching.
inline void spawn_spots(const std::string& tile_json, std::vector<Spot>& out) {
    size_t key = tile_json.find("\"spawn_zones\"");
    if (key == std::string::npos) return;
    size_t open = tile_json.find('[', key);
    if (open == std::string::npos) return;
    int depth = 0;
    bool in_string = false;
    size_t close = std::string::npos;
    for (size_t i = open; i < tile_json.size(); ++i) {
        char c = tile_json[i];
        if (in_string) { if (c == '\\') ++i; else if (c == '"') in_string = false; continue; }
        if (c == '"') in_string = true;
        else if (c == '[' || c == '{') ++depth;
        else if ((c == ']' || c == '}') && --depth == 0) { close = i; break; }
    }
    if (close == std::string::npos) return;
    assetjson::Value v;
    try { v = assetjson::Parser(tile_json.substr(open, close - open + 1)).parse(); } catch (const std::exception&) { return; }
    for (const auto& zone : v.array) {
        if (zone.kind != 'o') continue;
        const std::string& filter = zone.get("filter").text;
        const auto& pos = zone.get("pos").array;
        if (filter.empty() || filter.size() > 40 || filter.find_first_of(" \t\r\n") != std::string::npos || pos.size() != 3) continue;
        out.push_back({{pos[0].number, pos[1].number, pos[2].number}, filter});
    }
}

// Gas station spots only (where traders stand).
inline void gas_stations(const std::string& tile_json, std::vector<Zone>& out) {
    std::vector<Spot> spots;
    spawn_spots(tile_json, spots);
    for (const auto& s : spots) if (s.filter == "gas_station") out.push_back(s.at);
}

// The spot nearest to (x, z) on the ground plane; -1 when there are none.
inline int nearest(const std::vector<Zone>& zones, double x, double z) {
    int best = -1;
    double best_d = 0;
    for (size_t i = 0; i < zones.size(); ++i) {
        double dx = zones[i].x - x, dz = zones[i].z - z, d = dx * dx + dz * dz;
        if (best < 0 || d < best_d) { best = int(i); best_d = d; }
    }
    return best;
}

// "1.2 km, ahead left": distance and where the point is relative to the way you face (yaw 0 faces +Z, +pi/2 faces +X).
inline std::string direction_text(double px, double pz, double yaw, double tx, double tz) {
    double dx = tx - px, dz = tz - pz, d = std::sqrt(dx * dx + dz * dz);
    char dist[32];
    if (d >= 1000) std::snprintf(dist, sizeof dist, "%.1f km", d / 1000);
    else std::snprintf(dist, sizeof dist, "%d m", int(std::lround(d)));
    if (d < 1) return dist;
    const double pi = 3.14159265358979323846;
    double rel = std::atan2(dx, dz) - yaw;
    while (rel > pi) rel -= 2 * pi;
    while (rel < -pi) rel += 2 * pi;
    double deg = rel * 180 / pi;   // + is to your right (towards +X when facing +Z)
    const char* where = deg > -22.5 && deg <= 22.5    ? "ahead"
                        : deg > 22.5 && deg <= 67.5   ? "ahead right"
                        : deg > 67.5 && deg <= 112.5  ? "right"
                        : deg > 112.5 && deg <= 157.5 ? "behind right"
                        : deg > -67.5 && deg <= -22.5 ? "ahead left"
                        : deg > -112.5 && deg <= -67.5 ? "left"
                        : deg > -157.5 && deg <= -112.5 ? "behind left"
                                                         : "behind";
    return std::string(dist) + ", " + where;
}

// ------------------------------------------------------------------ in-game days
// Counts midnights from the day/night cycle factor (0 = midnight, 0.5 = noon). A drop from late evening to early
// morning is a new day; a big jump (time set, world loaded) is not counted twice.
struct DayClock {
    double last = -1;
    int64_t days = 0;
    bool update(double factor) {
        if (!(factor >= 0 && factor < 1)) return false;
        bool wrapped = last >= 0.75 && factor < 0.25;
        last = factor;
        if (wrapped) ++days;
        return wrapped;
    }
};

// ------------------------------------------------------------------ stock
inline uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

struct StockRow { int candidate = 0; int quantity = 0; };

// Up to `count` different candidates (indices into a list sorted the same way on every PC), each with 1-3 in
// stock. The same seed and restock number always give the same shelf.
inline std::vector<StockRow> pick_stock(size_t candidates, uint64_t seed, int64_t restock, int count) {
    std::vector<int> order(candidates);
    for (size_t i = 0; i < candidates; ++i) order[i] = int(i);
    uint64_t state = mix(seed ^ mix(uint64_t(restock) + 0x5157));
    for (size_t i = candidates; i > 1; --i) {
        state = mix(state);
        std::swap(order[i - 1], order[size_t(state % i)]);
    }
    std::vector<StockRow> out;
    for (size_t i = 0; i < candidates && int(i) < count; ++i) {
        state = mix(state);
        out.push_back({order[i], 1 + int(state % 3)});
    }
    std::sort(out.begin(), out.end(), [](const StockRow& a, const StockRow& b) { return a.candidate < b.candidate; });
    return out;
}

// What the trader pays for something he sells for `price`: 32% (40% of the price before the markup), at least 1.
// The Junkyard pays scrap: 15%.
inline int sell_price(int price, int pct = 32) { return std::max(1, price * pct / 100); }

// ------------------------------------------------------------------ the traders
// The Mechanic is first (the jobs board is his). Each trader stands at a kind of place, tried in order: the nearest
// such spot to the Mechanic, at least kShopGap from the other traders and at most kShopReach away.
// kMechanic is shown as "Hayzeco Autoparts" (Nate, 2026-10-21); its id stays "mechanic" for saves and pins.
enum Shop : int { kMechanic, kBuilder, kJunkyard, kGrocer, kMedic, kMarineAir, kModShop, kOutfitter, kGunsmith, kShopCount };
struct ShopKind {
    const char* id;
    const char* name;
    const char* places[3];
    int price_pct;     // of the normal price
    int sell_pct;      // of the normal price, paid for what he buys
    bool daily;        // restocks every day (else every kRestockDays)
};
inline constexpr ShopKind kShops[] = {
    {"mechanic", "Hayzeco Autoparts", {"gas_station", nullptr, nullptr}, 100, 32, false},
    {"builder", "Builder", {"industrial", "sawmill", "town"}, 100, 32, false},
    {"junkyard", "Junkyard", {"farm", "roadside", "industrial"}, 60, 15, true},
    {"grocer", "Grocer", {"town", "house", nullptr}, 100, 32, false},
    {"medic", "Medic", {"town", "office", "house"}, 100, 32, false},
    {"marine_air", "Marine & Air", {"roadside", "industrial", "town"}, 100, 32, false},
    {"mod_shop", "Mod Shop", {"office", "town", "house"}, 100, 32, false},
    {"outfitter", "Outfitter", {"town", "house", "office"}, 100, 32, false},
    {"gunsmith", "Gunsmith", {"roadside", "industrial", "town"}, 115, 32, false},
};
static_assert(sizeof(kShops) / sizeof(kShops[0]) == kShopCount, "one entry per trader");
inline constexpr double kShopGap = 120, kShopReach = 5000;
inline int shop_by_id(const std::string& id) {
    for (int i = 0; i < kShopCount; ++i) if (id == kShops[i].id) return i;
    return -1;
}

// Where trader `shop` stands: index into `spots`, or -1. `placed` holds the traders already standing.
inline int place_shop(int shop, const std::vector<Spot>& spots, Zone anchor, const std::vector<Zone>& placed) {
    for (const char* kind : kShops[shop].places) {
        if (!kind) break;
        int best = -1;
        double best_d = 0;
        for (size_t i = 0; i < spots.size(); ++i) {
            if (spots[i].filter != kind) continue;
            double d = std::hypot(spots[i].at.x - anchor.x, spots[i].at.z - anchor.z);
            if (d > kShopReach) continue;
            bool clear = true;
            for (const auto& p : placed) clear = clear && std::hypot(spots[i].at.x - p.x, spots[i].at.z - p.z) >= kShopGap;
            if (clear && (best < 0 || d < best_d)) { best = int(i); best_d = d; }
        }
        if (best >= 0) return best;
    }
    return -1;
}

// The ids of a definitions file ({"definitions": [{"id": ...}, ...]}), sorted; empty if it can't be read.
inline std::vector<std::string> definition_ids(const std::string& json) {
    std::vector<std::string> out;
    try {
        assetjson::Value root = assetjson::Parser(json).parse();
        for (const auto& d : root.get("definitions").array)
            if (d.kind == 'o' && d.get("id").kind == 's' && !d.get("id").text.empty()) out.push_back(d.get("id").text);
    } catch (const std::exception&) { out.clear(); }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Spread over the map (layout 2, Nate: "one of each, spread out over the map"): the spot of the trader's kinds
// farthest from every trader already standing, outside the `avoid` boxes (the top island). -1 if none.
struct Box { double x0, z0, x1, z1; bool contains(const Zone& p) const { return p.x >= x0 && p.x <= x1 && p.z >= z0 && p.z <= z1; } };
// The island at the top of the map (Nate: "dont use the top island"), in world metres; measured from the final
// test's trader pins on the map (about 10.35 m a pixel, z grows up the map). The west island's north coast is ~600 m south.
inline constexpr Box kTopIsland{77500, 54700, 82500, 58500};
inline int place_spread(int shop, const std::vector<Spot>& spots, const std::vector<Zone>& placed, const std::vector<Box>& avoid) {
    int best = -1;
    double best_d = -1;
    for (size_t i = 0; i < spots.size(); ++i) {
        bool kind = false;
        for (const char* k : kShops[shop].places) if (k && spots[i].filter == k) kind = true;
        if (!kind) continue;
        bool out = false;
        for (const auto& b : avoid) out = out || b.contains(spots[i].at);
        if (out) continue;
        double d = 1e18;
        for (const auto& p : placed) d = std::min(d, std::hypot(spots[i].at.x - p.x, spots[i].at.z - p.z));
        if (d > best_d) { best = int(i); best_d = d; }
    }
    return best;
}

// A boat or plane part, by its category or id (the Marine & Air trader's goods; the Mechanic leaves them to him).
inline bool marine_air(const std::string& category, const std::string& id) {
    static constexpr const char* kWords[] = {"boat", "marine", "ship", "hull", "propeller", "plane", "aero", "aircraft", "wing", "rudder",
                                             "aileron", "rotor", "heli", "jet_", "sail", "pontoon", "float"};
    for (const std::string* s : {&category, &id})
        for (const char* w : kWords)
            if (s->find(w) != std::string::npos) return true;
    return false;
}

// What a trader stocks and buys. `part`: a vehicle part (else an item); `building`: a building part; `modded`: a part
// the game's own files don't list. Loot groups: 1 medical, 2 food, 3 clothing, 4 weapons, 5 ammo, 6 mechanical,
// 7 electronics, 8 survival, 9 construction. (0.1.27: parts are mechanical, electronics, construction or furniture; the only boat/plane
// category is "aircraft"; no item is in the construction group.)
struct Goods { bool part = false, building = false, modded = false, marine_air = false; int32_t loot_group = 0; };
inline bool shop_sells(int shop, const Goods& g) {
    switch (shop) {
    case kMechanic: return g.part && !g.building && (g.loot_group == 6 || g.loot_group == 7) && !g.marine_air && !g.modded;
    case kBuilder: return (g.part && g.building && !g.modded) || (!g.part && g.loot_group == 9);
    case kJunkyard: return g.part && !g.building && g.loot_group > 0 && !g.modded;
    case kGrocer: return !g.part && (g.loot_group == 2 || g.loot_group == 8);   // food, drink and survival supplies
    case kMedic: return !g.part && g.loot_group == 1;
    case kMarineAir: return g.part && g.marine_air && !g.modded;
    case kModShop: return g.part && g.modded;
    case kOutfitter: return !g.part && g.loot_group == 3;
    case kGunsmith: return !g.part && (g.loot_group == 4 || g.loot_group == 5);   // guns cost a bit more (price_pct 115)
    }
    return false;
}
inline bool shop_buys(int shop, const Goods& g) {
    if (shop == kJunkyard) return g.loot_group > 0 || g.part;
    if (shop == kMechanic) return (g.loot_group == 6 || g.loot_group == 7) && !g.marine_air && !g.modded && !g.building;   // parts and items
    return shop_sells(shop, g);
}
// What a trader's delivery jobs ask for (Nate: "jobs pertaining to their class of items"): what he buys, the
// Junkyard only scrap parts, never another mod's parts, and the Mod Shop has no jobs.
inline bool job_goods(int shop, const Goods& g) {
    if (shop == kModShop || g.modded) return false;
    return shop == kJunkyard ? shop_sells(shop, g) : shop_buys(shop, g);
}
// A specialist whose goods all start at a later loot tier sells that first tier early, at half again his price.
inline constexpr int kEarlyPct = 150;
inline int early_price(int price) { return std::max(1, int(int64_t(price) * kEarlyPct / 100)); }
// Kill jobs are the Gunsmith's (they were the Mechanic's before every trader had a board).
inline constexpr int kKillShop = kGunsmith;

// ------------------------------------------------------------------ paying with Cash stacks
struct Stack { int32_t item_id = 0; int32_t count = 0; };
struct Payment { int32_t item_id = 0; int32_t new_count = 0; };   // new_count 0: remove the stack

// Takes `cost` from the stacks, smallest first so you keep fewer, fuller stacks. False (and no plan) when the
// stacks don't cover it.
inline bool plan_payment(std::vector<Stack> stacks, int64_t cost, std::vector<Payment>& out) {
    out.clear();
    if (cost <= 0) return cost == 0;
    int64_t total = 0;
    for (const auto& s : stacks) total += std::max(0, s.count);
    if (total < cost) return false;
    std::stable_sort(stacks.begin(), stacks.end(), [](const Stack& a, const Stack& b) { return a.count < b.count; });
    for (const auto& s : stacks) {
        if (cost <= 0) break;
        if (s.count <= 0) continue;
        int64_t take = std::min<int64_t>(s.count, cost);
        out.push_back({s.item_id, int32_t(s.count - take)});
        cost -= take;
    }
    return true;
}

// Stacks of at most kCashStack that add up to `amount`.
inline std::vector<int32_t> cash_stacks(int64_t amount) {
    std::vector<int32_t> out;
    while (amount > 0) {
        int32_t n = int32_t(std::min<int64_t>(amount, kCashStack));
        out.push_back(n);
        amount -= n;
    }
    return out;
}

// ------------------------------------------------------------------ delivery jobs
// "Bring N of an item to a place". Items are findable ones the caller lists (sorted the same way on every PC) with
// their prices; places are world spots. The same seed and board number always give the same offers.
// A delivery (item: index into the goods, spot: index into the places) or a kill job (item: -1 for any zombie, else an
// index into the special zombies; no spot).
// Hard offers (see make_hard_offers) are worth more and unlock after kHardUnlock finished jobs.
struct Offer { int item = -1; int count = 0; int spot = -1; int reward = 0; bool kill = false; bool hard = false; };

inline int round5(double v) { return std::max(5, int(std::lround(v / 5)) * 5); }
inline int delivery_count(int price, uint64_t r) { return price <= 40 ? 2 + int(r % 4) : price <= 120 ? 1 + int(r % 3) : 1; }
// Half again the goods' price before the markup (1.2 x the shop price), plus a little per metre (about 60 Cash a
// kilometre).
inline int delivery_reward(int price, int count, double metres) { return round5(price * count * 1.2 + metres * 0.06); }

inline std::vector<Offer> make_offers(const std::vector<int>& item_prices, const std::vector<Spot>& spots, Zone from, uint64_t seed,
                                      int64_t board, int n) {
    std::vector<Offer> out;
    if (item_prices.empty() || spots.empty()) return out;
    auto dist = [&](const Spot& s) { return std::hypot(s.at.x - from.x, s.at.z - from.z); };
    std::vector<int> in_reach;
    for (double lo : {400.0, 150.0}) {   // 400 m to 3 km away; failing that, anywhere from 150 m to 6 km
        in_reach.clear();
        double hi = lo > 200 ? 3000 : 6000;
        for (size_t i = 0; i < spots.size(); ++i) { double d = dist(spots[i]); if (d >= lo && d <= hi) in_reach.push_back(int(i)); }
        if (int(in_reach.size()) >= n) break;
    }
    if (in_reach.empty()) return out;
    // Pick the kind of place first (so the many industrial spots don't crowd out farms and towns), then a spot of it.
    std::vector<std::string> kinds;
    for (int i : in_reach)
        if (std::find(kinds.begin(), kinds.end(), spots[size_t(i)].filter) == kinds.end()) kinds.push_back(spots[size_t(i)].filter);
    std::sort(kinds.begin(), kinds.end());
    uint64_t state = mix(seed ^ mix(uint64_t(board) + 0xde11));
    for (int k = 0; k < n; ++k) {
        Offer o;
        state = mix(state); o.item = int(state % item_prices.size());
        state = mix(state);
        const std::string& kind = kinds[size_t(state % kinds.size())];
        std::vector<int> of_kind;
        for (int i : in_reach) if (spots[size_t(i)].filter == kind) of_kind.push_back(i);
        state = mix(state); o.spot = of_kind[size_t(state % of_kind.size())];
        state = mix(state); o.count = delivery_count(item_prices[size_t(o.item)], state);
        o.reward = delivery_reward(item_prices[size_t(o.item)], o.count, dist(spots[size_t(o.spot)]));
        out.push_back(o);
    }
    return out;
}

// ------------------------------------------------------------------ kill jobs
// Zombie kinds from the player's own rom/data/zombie_definitions.json. zombie_basic is the ordinary one; the rest
// (acid, ambush, ...) are "special".
inline constexpr const char* kBasicZombie = "zombie_basic";
struct ZombieKind { std::string id, name; int tier = 0; double spawn_probability = 0; };

inline void find_zombies(const assetjson::Value& v, std::vector<ZombieKind>& out) {
    if (v.kind == 'o') {
        const std::string& id = v.get("id").text;
        if (!id.empty() && id.find("zombie") != std::string::npos && v.get("id").kind == 's') {
            ZombieKind z;
            z.id = id;
            z.name = v.get("display_name").text.empty() ? v.get("name").text : v.get("display_name").text;
            z.tier = v.get("spawn_tier").kind == 'd' ? int(v.get("spawn_tier").number) : 0;
            z.spawn_probability = v.get("spawn_probability").kind == 'd' ? v.get("spawn_probability").number : 0;
            out.push_back(std::move(z));
            return;
        }
        for (const auto& [key, child] : v.object) find_zombies(child, out);
    } else if (v.kind == 'a') {
        for (const auto& child : v.array) find_zombies(child, out);
    }
}
inline std::vector<ZombieKind> parse_zombies(const std::string& json) {
    std::vector<ZombieKind> out;
    try { find_zombies(assetjson::Parser(json).parse(), out); } catch (const std::exception&) { out.clear(); }
    std::sort(out.begin(), out.end(), [](const ZombieKind& a, const ZombieKind& b) { return a.id < b.id; });
    return out;
}
// Special zombies the world can spawn by now (their spawn tier reached, a spawn chance), as indices.
inline std::vector<int> special_zombies(const std::vector<ZombieKind>& kinds, int loot_level) {
    std::vector<int> out;
    for (size_t i = 0; i < kinds.size(); ++i)
        if (kinds[i].id != kBasicZombie && kinds[i].spawn_probability > 0 && std::max(kinds[i].tier, 1) <= std::max(loot_level, 1)) out.push_back(int(i));
    return out;
}
inline int kill_reward(int count, bool special) { return round5(count * (special ? 45.0 : 12.0) + 30); }

// Two kill jobs a board: some ordinary zombies, and (when the world has any yet) a few of one special kind.
inline std::vector<Offer> make_kill_offers(const std::vector<int>& specials, uint64_t seed, int64_t board) {
    std::vector<Offer> out;
    uint64_t state = mix(seed ^ mix(uint64_t(board) + 0x6b11));
    Offer basic;
    basic.kill = true;
    state = mix(state); basic.count = 6 + int(state % 10);
    basic.reward = kill_reward(basic.count, false);
    out.push_back(basic);
    Offer second;
    second.kill = true;
    if (!specials.empty()) {
        state = mix(state); second.item = specials[size_t(state % specials.size())];
        state = mix(state); second.count = 2 + int(state % 4);
    } else {
        state = mix(state); second.count = 15 + int(state % 11);
    }
    second.reward = kill_reward(second.count, second.item >= 0);
    out.push_back(second);
    return out;
}

// ------------------------------------------------------------------ hard jobs
// Two hard offers a board, after the ordinary ones: a delivery of one of the pricier half of the goods, one or two more
// than usual, 3 to 6 km out (failing that, the farthest third of the places), paying double; and a kill job for 6-10
// of one special kind (40-60 ordinary zombies while the world has none yet), paying half again. Hard jobs open once
// kHardUnlock jobs have been finished in the world.
inline constexpr int kHardUnlock = 3;
inline int hard_delivery_reward(int price, int count, double metres) { return round5((price * count * 1.2 + metres * 0.06) * 2); }
inline int hard_kill_reward(int count, bool special) { return round5(kill_reward(count, special) * 1.5); }

inline std::vector<Offer> make_hard_offers(const std::vector<int>& item_prices, const std::vector<Spot>& spots, Zone from,
                                           const std::vector<int>& specials, uint64_t seed, int64_t board, bool kills) {
    std::vector<Offer> out;
    uint64_t state = mix(seed ^ mix(uint64_t(board) + 0x4a2d));
    if (!item_prices.empty() && !spots.empty()) {
        auto dist = [&](const Spot& s) { return std::hypot(s.at.x - from.x, s.at.z - from.z); };
        std::vector<int> distant;
        for (size_t i = 0; i < spots.size(); ++i) { double d = dist(spots[i]); if (d >= 3000 && d <= 6000) distant.push_back(int(i)); }
        if (distant.empty()) {
            std::vector<int> all(spots.size());
            for (size_t i = 0; i < all.size(); ++i) all[i] = int(i);
            std::stable_sort(all.begin(), all.end(), [&](int a, int b) { return dist(spots[size_t(a)]) > dist(spots[size_t(b)]); });
            all.resize(std::max<size_t>(1, all.size() / 3));
            distant = all;
        }
        std::vector<int> sorted = item_prices;
        std::sort(sorted.begin(), sorted.end());
        int median = sorted[sorted.size() / 2];
        std::vector<int> dear;
        for (size_t i = 0; i < item_prices.size(); ++i) if (item_prices[i] >= median) dear.push_back(int(i));
        Offer o;
        o.hard = true;
        state = mix(state); o.item = dear[size_t(state % dear.size())];
        state = mix(state); o.spot = distant[size_t(state % distant.size())];
        state = mix(state); o.count = delivery_count(item_prices[size_t(o.item)], state) + 1 + int((state >> 20) % 2);
        o.reward = hard_delivery_reward(item_prices[size_t(o.item)], o.count, dist(spots[size_t(o.spot)]));
        out.push_back(o);
    }
    if (kills) {
        Offer k;
        k.hard = true; k.kill = true;
        if (!specials.empty()) {
            state = mix(state); k.item = specials[size_t(state % specials.size())];
            state = mix(state); k.count = 6 + int(state % 5);
        } else {
            state = mix(state); k.count = 40 + int(state % 21);
        }
        k.reward = hard_kill_reward(k.count, k.item >= 0);
        out.push_back(k);
    }
    return out;
}

// Up to three different parts to offer instead of the Cash reward: worth about the same (half to 1.3 times),
// else the closest prices.
inline std::vector<int> reward_parts(const std::vector<int>& part_prices, int reward, uint64_t seed) {
    std::vector<int> pool, out;
    for (size_t i = 0; i < part_prices.size(); ++i)
        if (part_prices[i] >= reward / 2 && part_prices[i] * 10 <= reward * 13) pool.push_back(int(i));
    if (pool.size() < 3) {
        std::vector<int> all(part_prices.size());
        for (size_t i = 0; i < all.size(); ++i) all[i] = int(i);
        std::sort(all.begin(), all.end(), [&](int a, int b) { return std::abs(part_prices[size_t(a)] - reward) < std::abs(part_prices[size_t(b)] - reward); });
        for (int i : all) { if (pool.size() >= 3) break; if (std::find(pool.begin(), pool.end(), i) == pool.end()) pool.push_back(i); }
    }
    uint64_t state = mix(seed);
    while (!pool.empty() && out.size() < 3) {
        state = mix(state);
        size_t pick = size_t(state % pool.size());
        out.push_back(pool[pick]);
        pool.erase(pool.begin() + std::ptrdiff_t(pick));
    }
    return out;
}

// ------------------------------------------------------------------ per-world saved state
// One small text file per world (mods/AnyCareer/worlds/<seed>.txt), "key value" lines; unknown keys are kept out.
// A trader other than the Mechanic: where he stands, and his shelf's restock number and sales.
struct ShopSave { bool placed = false; Zone at; int64_t restock = -1; std::vector<int> sold, taken; };

struct WorldState {
    ShopSave shops[kShopCount];   // [kMechanic] unused: the Mechanic's are the fields below (older saves)
    bool placed = false;
    Zone trader;
    int64_t days = 0;
    int64_t restock = -1;
    std::vector<int> sold;    // per shelf row, how many have been bought since the last restock
    // Delivery jobs: the board number the offers came from, which of them were taken, and the job in hand.
    int64_t board = -1;
    std::vector<int> taken;
    int job_state = 0;        // 0 none, 1 carrying it, 2 delivered: reward to pick
    std::string job_item;     // item definition id
    int job_count = 0, job_reward = 0;
    std::string job_place;    // the spot's filter (gas_station, town, ...)
    Zone job_at;
    std::vector<std::string> job_parts;   // the three part choices once delivered
    bool job_kill = false;    // a kill job: job_item is the zombie kind ("any" for any zombie), no place
    int job_progress = 0;     // kills so far
    bool job_hard = false;    // taken from a hard offer
    int job_shop = kMechanic; // whose board the job came from (kill jobs end there; the reward choices are his goods)
    bool job_part = false;    // job_item is a vehicle part (delivered as part items), else an item
    int layout = 0;           // 2: the traders are spread over the map (older worlds had them around the Mechanic)
    int jobs_done = 0;        // jobs finished (reward claimed) in this world; hard jobs open at kHardUnlock
};

inline std::string save_world(const WorldState& w) {
    std::ostringstream o;
    o.precision(17);
    o << "anycareer_world 1\n";
    if (w.placed) o << "mechanic " << w.trader.x << " " << w.trader.y << " " << w.trader.z << "\n";
    o << "days " << w.days << "\nrestock " << w.restock << "\nsold";
    for (int n : w.sold) o << " " << n;
    o << "\nboard " << w.board << "\ntaken";
    for (int n : w.taken) o << " " << n;
    o << "\n";
    if (w.job_state && !w.job_item.empty())
        o << "job " << w.job_state << " " << w.job_count << " " << w.job_reward << " " << (w.job_place.empty() ? "place" : w.job_place) << " " << w.job_at.x << " " << w.job_at.y << " "
          << w.job_at.z << " " << w.job_item << "\n";
    if (w.job_state && w.job_kill) o << "job_kill " << w.job_progress << "\n";
    if (w.job_state && w.job_hard) o << "job_hard\n";
    if (w.job_state) o << "job_shop " << kShops[w.job_shop >= 0 && w.job_shop < kShopCount ? w.job_shop : 0].id << (w.job_part ? " part" : " item") << "\n";
    o << "layout " << w.layout << "\n";
    o << "done " << w.jobs_done << "\n";
    for (int i = 1; i < kShopCount; ++i) {
        const ShopSave& s = w.shops[i];
        if (s.placed) o << "shop " << kShops[i].id << " " << s.at.x << " " << s.at.y << " " << s.at.z << "\n";
        if (s.restock >= 0 || !s.sold.empty()) {
            o << "shop_sold " << kShops[i].id << " " << s.restock;
            for (int n : s.sold) o << " " << n;
            o << "\n";
        }
        if (!s.taken.empty()) {
            o << "shop_taken " << kShops[i].id;
            for (int n : s.taken) o << " " << n;
            o << "\n";
        }
    }
    if (!w.job_parts.empty()) {
        o << "job_parts";
        for (const auto& id : w.job_parts) o << " " << id;
        o << "\n";
    }
    return o.str();
}

inline bool load_world(const std::string& text, WorldState& w) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "anycareer_world 1") return false;
    WorldState r;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (key == "mechanic") {
            Zone z;
            if (row >> z.x >> z.y >> z.z && std::isfinite(z.x) && std::isfinite(z.y) && std::isfinite(z.z)) { r.trader = z; r.placed = true; }
        } else if (key == "days") {
            int64_t d;
            if (row >> d && d >= 0) r.days = d;
        } else if (key == "restock") {
            int64_t d;
            if (row >> d && d >= -1) r.restock = d;
        } else if (key == "sold") {
            int n;
            while (row >> n && r.sold.size() < 64) r.sold.push_back(std::max(0, n));
        } else if (key == "board") {
            int64_t d;
            if (row >> d && d >= -1) r.board = d;
        } else if (key == "taken") {
            int n;
            while (row >> n && r.taken.size() < 16) r.taken.push_back(n);
        } else if (key == "job") {
            int state = 0, count = 0, reward = 0;
            Zone z;
            std::string item, place;
            if (row >> state >> count >> reward >> place >> z.x >> z.y >> z.z >> item && (state == 1 || state == 2) && count > 0 && reward >= 0 &&
                std::isfinite(z.x) && std::isfinite(z.z)) {
                r.job_state = state; r.job_count = count; r.job_reward = reward; r.job_place = place; r.job_at = z; r.job_item = item;
            }
        } else if (key == "job_kill") {
            int n = 0;
            r.job_kill = true;
            if (row >> n && n >= 0) r.job_progress = n;
        } else if (key == "job_shop") {
            std::string id, kind;
            row >> id >> kind;
            int i = shop_by_id(id);
            r.job_shop = i >= 0 ? i : kMechanic;
            r.job_part = kind == "part";
        } else if (key == "layout") {
            int n = 0;
            if (row >> n && n >= 0) r.layout = n;
        } else if (key == "shop_taken") {
            std::string id;
            row >> id;
            int i = shop_by_id(id), n;
            if (i <= 0) continue;
            while (row >> n && r.shops[i].taken.size() < 16) r.shops[i].taken.push_back(n);
        } else if (key == "shop" || key == "shop_sold") {
            std::string id;
            row >> id;
            int i = shop_by_id(id);
            if (i <= 0) continue;
            ShopSave& s = r.shops[i];
            if (key == "shop") {
                Zone z;
                if (row >> z.x >> z.y >> z.z && std::isfinite(z.x) && std::isfinite(z.y) && std::isfinite(z.z)) { s.at = z; s.placed = true; }
            } else {
                int64_t d;
                int n;
                if (row >> d && d >= -1) s.restock = d;
                s.sold.clear();
                while (row >> n && s.sold.size() < 64) s.sold.push_back(std::max(0, n));
            }
        } else if (key == "job_hard") {
            r.job_hard = true;
        } else if (key == "done") {
            int n = 0;
            if (row >> n && n >= 0) r.jobs_done = n;
        } else if (key == "job_parts") {
            std::string id;
            while (row >> id && r.job_parts.size() < 3) r.job_parts.push_back(id);
        }
    }
    w = r;
    return true;
}

}  // namespace anycareer::logic
