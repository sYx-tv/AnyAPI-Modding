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

// Rewrites one stock ammo_box entry (its JSON text, every field kept) into the Cash definition. Fields it sets are
// replaced where they exist and added where they don't. With a mesh path, Cash gets that look (and no extra ammo
// meshes), and its floor physics box becomes `floor_min`..`floor_max`. Returns "" when the text isn't one JSON object.
inline std::string make_cash_entry(const std::string& entry, const std::string& mesh = {}, const double* floor_min = nullptr,
                                   const double* floor_max = nullptr) {
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


// ------------------------------------------------------------------ trader placement
struct Zone { double x{}, y{}, z{}; };

// Spots in the world tiles (spawn_zones: pos in world metres, and a filter such as gas_station, town, house).
struct Spot { Zone at; std::string filter; };

// Places a delivery can go: towns, houses, gas stations and other buildings, not open-country spawn areas.
inline bool delivery_place(const std::string& filter) {
    for (const char* f : {"town", "house", "gas_station"}) if (filter == f) return true;
    for (const char* f : {"farm", "shop", "store", "village", "industrial", "office", "garage", "barn", "station"})
        if (filter.find(f) != std::string::npos) return true;
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

// What the trader pays for something he sells for `price`: 40%, at least 1.
inline int sell_price(int price) { return std::max(1, price * 40 / 100); }

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
struct Offer { int item = -1; int count = 0; int spot = -1; int reward = 0; bool kill = false; };

inline int round5(double v) { return std::max(5, int(std::lround(v / 5)) * 5); }
inline int delivery_count(int price, uint64_t r) { return price <= 40 ? 2 + int(r % 4) : price <= 120 ? 1 + int(r % 3) : 1; }
// Half again the goods' trader price, plus a little per metre (about 60 Cash a kilometre).
inline int delivery_reward(int price, int count, double metres) { return round5(price * count * 1.5 + metres * 0.06); }

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
    uint64_t state = mix(seed ^ mix(uint64_t(board) + 0xde11));
    for (int k = 0; k < n; ++k) {
        Offer o;
        state = mix(state); o.item = int(state % item_prices.size());
        state = mix(state); o.spot = in_reach[size_t(state % in_reach.size())];
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
struct WorldState {
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
        } else if (key == "job_parts") {
            std::string id;
            while (row >> id && r.job_parts.size() < 3) r.job_parts.push_back(id);
        }
    }
    w = r;
    return true;
}

}  // namespace anycareer::logic
