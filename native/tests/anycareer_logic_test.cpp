#include "../anycareer_logic.h"
#include "../anycareer_cash_mesh.h"
#include <cstring>
#include <cassert>
using namespace anycareer::logic;
int main() {
    // A made-up ammo box entry in the stock file's shape (not game data): fields it has are replaced, missing
    // ones are added, and everything else is kept as it was.
    const std::string box =
        "{\"id\": \"test_box\", \"name\": \"Test Box\", \"description\": \"x\", \"class\": \"ammo_box\", "
        "\"floor\": {\"physics_min\": [-0.1, 0, -0.1]}, \"spawn_probability\": 0.2, \"loot_group\": \"ammo\", "
        "\"category\": \"weapon\", \"gun_magazine_capacity\": 30}";
    std::string cash = make_cash_entry(box);
    assert(!cash.empty());
    assetjson::Value v = assetjson::Parser(cash).parse();
    assert(v.get("id").text == kCashId && v.get("name").text == "Cash" && v.get("class").text == "ammo_box");
    assert(v.get("gun_magazine_capacity").number == kCashStack && v.get("spawn_probability").number == 0);
    assert(v.get("loot_group").text == "none" && v.get("tech_tier").number == 99 && v.get("category").text == "miscellaneous");
    assert(v.get("floor").get("physics_min").array.size() == 3);   // untouched fields survive
    assert(make_cash_entry("not json").empty() && make_cash_entry("[1]").empty());

    // Prices: rarer and higher tier costs more; a part costs more than an item of the same group.
    Entry common; common.loot_group = 2; common.tech_tier = 1; common.spawn_probability = 0.2;   // food
    Entry rare = common; rare.spawn_probability = 0.05;
    Entry high = common; high.tech_tier = 4;
    assert(price(common) == 20 && price(rare) == 80 && price(high) == 50);
    Entry part = common; part.part = true;
    assert(price(part) > price(common));
    Entry never = common; never.spawn_probability = 0;
    assert(price(never) == price(rare));

    // Findable: needs a loot group, a spawn chance and a reached tier; no tier counts as tier 1; Cash never.
    assert(findable(common, 1) && !findable(high, 3) && findable(high, 4));
    Entry untiered = common; untiered.tech_tier = 0; assert(findable(untiered, 1));
    Entry nogroup = common; nogroup.loot_group = 0; assert(!findable(nogroup, 5));
    assert(!findable(never, 5));
    Entry c = common; c.id = kCashId; assert(!findable(c, 5));

    common.price = price(common);
    assert(tsv_row(common).find("\tfood\t1\t0.2000\t20\t") != std::string::npos);

    // Gas station spots from a made-up tile in the stock format.
    std::vector<Zone> zones;
    gas_stations("{\"spawn_zones\": [{\"pos\": [10, 2, 30], \"filter\": \"house\"}, {\"pos\": [100, 3, 200], \"filter\": \"gas_station\"}],"
                 " \"prop_spawn_zones\": []}", zones);
    gas_stations("{\"spawn_zones\": [{\"pos\": [-50, 1, 0], \"filter\": \"gas_station\", \"size\": 2}]}", zones);
    gas_stations("{\"spawn_zones\": [{\"pos\": [1, 1], \"filter\": \"gas_station\"}]}", zones);   // bad pos: skipped
    gas_stations("not json gas_station", zones);
    assert(zones.size() == 2 && zones[0].x == 100 && zones[0].z == 200 && zones[1].x == -50);
    assert(nearest(zones, 0, 0) == 1 && nearest(zones, 90, 180) == 0 && nearest({}, 0, 0) == -1);

    // Directions: yaw 0 faces +Z, +pi/2 faces +X.
    assert(direction_text(0, 0, 0, 0, 500) == "500 m, ahead");
    assert(direction_text(0, 0, 0, 1500, 0) == "1.5 km, right");
    assert(direction_text(0, 0, 0, -100, 0) == "100 m, left");
    assert(direction_text(0, 0, 1.5707963, 0, -300) == "300 m, right");
    assert(direction_text(0, 0, 0, 0, -50) == "50 m, behind");
    assert(direction_text(0, 0, 0, 100, 100) == "141 m, ahead right");

    // Days: count each midnight once.
    DayClock clock;
    assert(!clock.update(0.5) && !clock.update(0.9) && clock.update(0.05) && clock.days == 1);
    assert(!clock.update(0.3) && !clock.update(0.1) && clock.days == 1);   // time set back: not a new day
    assert(!clock.update(0.8) && clock.update(0.0) && clock.days == 2 && !clock.update(-1) && !clock.update(1.5));

    // Stock: same inputs, same shelf; a restock changes it; never more than there are candidates.
    auto a = pick_stock(40, 1234, 0, 10), b = pick_stock(40, 1234, 0, 10), next = pick_stock(40, 1234, 1, 10);
    assert(a.size() == 10 && next.size() == 10);
    bool same = true, differs = false;
    for (size_t i = 0; i < a.size(); ++i) {
        same = same && a[i].candidate == b[i].candidate && a[i].quantity == b[i].quantity;
        differs = differs || a[i].candidate != next[i].candidate;
        assert(a[i].quantity >= 1 && a[i].quantity <= 3 && a[i].candidate >= 0 && a[i].candidate < 40);
        if (i) assert(a[i].candidate > a[i - 1].candidate);   // sorted, no repeats
    }
    assert(same && differs && pick_stock(3, 1, 0, 10).size() == 3 && pick_stock(0, 1, 0, 10).empty());
    assert(sell_price(100) == 40 && sell_price(1) == 1);

    // Paying: smallest stacks first; not enough cash means no plan.
    std::vector<Payment> plan;
    assert(plan_payment({{1, 500}, {2, 30}, {3, 9999}}, 100, plan));
    assert(plan.size() == 2 && plan[0].item_id == 2 && plan[0].new_count == 0 && plan[1].item_id == 1 && plan[1].new_count == 430);
    assert(!plan_payment({{1, 50}}, 51, plan) && plan.empty());
    assert(plan_payment({{1, 50}}, 50, plan) && plan.size() == 1 && plan[0].new_count == 0);
    auto stacks = cash_stacks(12000);
    assert(stacks.size() == 2 && stacks[0] == 9999 && stacks[1] == 2001 && cash_stacks(0).empty());

    // World file round trip; junk is rejected.
    WorldState w;
    w.placed = true; w.trader = {123.25, 4, -987.5}; w.days = 7; w.restock = 3; w.sold = {0, 2, 1};
    WorldState r;
    assert(load_world(save_world(w), r) && r.placed && r.trader.x == 123.25 && r.trader.z == -987.5 && r.days == 7 && r.restock == 3);
    assert(r.sold.size() == 3 && r.sold[1] == 2);
    assert(!load_world("hello", r) && load_world("anycareer_world 1\n", r) && !r.placed && r.restock == -1);

    // Cash with its own look: mesh path, no ammo meshes, floor box resized; untouched otherwise.
    const double lo[3] = {-0.08, 0, -0.035}, hi[3] = {0.08, 0.035, 0.035};
    std::string looked = make_cash_entry(box, "meshes\\items\\anycareer_cash_a.mesh", lo, hi);
    assetjson::Value lv = assetjson::Parser(looked).parse();
    assert(lv.get("mesh_file").text == "meshes/items/anycareer_cash_a.mesh" && lv.get("meshes_dynamic").kind == 'a');
    assert(lv.get("floor").get("physics_min").array[0].number == -0.08 && lv.get("name").text == "Cash");
    assert(make_cash_entry(box).find("mesh_file") == std::string::npos);

    // The bundle mesh on a made-up template in the part-mesh layout (not a game file).
    std::vector<uint8_t> tmpl;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); tmpl.insert(tmpl.end(), c, c + n); };
    uint32_t u = 5, one = 1, name_len = 4;
    put("mesh", 4); put(&u, 4); put(&one, 4); put(&name_len, 4); put("tmpl", 4);
    std::vector<uint8_t> header(88, 0); header[0] = 7; put(header.data(), 88);
    double tb[6] = {-1, -1, -1, 1, 1, 1}; put(tb, sizeof tb);
    gpspart::MeshVertex tv[3]{}; uint32_t vb = sizeof tv; put(&vb, 4); put(tv, sizeof tv);
    uint32_t ti[3] = {0, 1, 2}, ib = sizeof ti; put(&ib, 4); put(ti, sizeof ti);
    uint8_t trailer[8] = {1, 2, 3, 4, 5, 6, 7, 8}; put(trailer, 8);
    gpspart::MeshLayout tl; assert(gpspart::parse_mesh(tmpl, tl));
    auto cash_mesh = anycareer::cashmesh::build(tmpl, "anycareer_cash_a");
    gpspart::MeshLayout ml;
    assert(!cash_mesh.empty() && gpspart::parse_mesh(cash_mesh, ml));
    assert(ml.vertices == anycareer::cashmesh::bundle().size() * 48 && ml.indices == anycareer::cashmesh::bundle().size() * 72);
    assert(cash_mesh[16 + 16] == 7 && std::memcmp(cash_mesh.data() + cash_mesh.size() - 8, trailer, 8) == 0);
    double mb[6]; std::memcpy(mb, cash_mesh.data() + ml.bounds, sizeof mb);
    assert(mb[1] < 0 && mb[3] > 0.079 && mb[4] > anycareer::cashmesh::kHeight && mb[0] < -0.079);
    assert(anycareer::cashmesh::build({1, 2, 3}, "x").empty());
    // Spots and delivery jobs.
    {
        std::vector<Spot> spots;
        spawn_spots("{\"props\": [1, 2], \"spawn_zones\": [{\"pos\": [0, 0, 500], \"filter\": \"town\"}, {\"pos\": [0, 0, 900], \"filter\": \"house\", "
                    "\"x\": [1, {\"y\": \"]\"}]}, {\"pos\": [9, 9], \"filter\": \"house\"}], \"after\": {\"filter\": \"gas_station\"}}", spots);
        assert(spots.size() == 2 && spots[0].filter == "town" && spots[1].at.z == 900);
        assert(delivery_place("town") && delivery_place("gas_station") && delivery_place("big_farm") && !delivery_place("zombie_horde"));
        assert(place_name("gas_station") == "gas station");
        std::vector<Spot> places;
        for (int i = 0; i < 40; ++i) places.push_back({{double(i * 200), 0, 0}, "house"});
        std::vector<int> prices = {20, 60, 300, 35};
        auto a = make_offers(prices, places, {0, 0, 0}, 77, 3, 3), b = make_offers(prices, places, {0, 0, 0}, 77, 3, 3),
             c = make_offers(prices, places, {0, 0, 0}, 77, 4, 3);
        assert(a.size() == 3 && c.size() == 3);
        bool same = true, differ = false;
        for (int i = 0; i < 3; ++i) {
            same = same && a[size_t(i)].item == b[size_t(i)].item && a[size_t(i)].spot == b[size_t(i)].spot && a[size_t(i)].reward == b[size_t(i)].reward;
            differ = differ || a[size_t(i)].item != c[size_t(i)].item || a[size_t(i)].spot != c[size_t(i)].spot;
            double d = places[size_t(a[size_t(i)].spot)].at.x;
            assert(d >= 400 && d <= 3000);
            int price = prices[size_t(a[size_t(i)].item)];
            assert(a[size_t(i)].count >= 1 && (price > 120 ? a[size_t(i)].count == 1 : a[size_t(i)].count <= 5));
            assert(a[size_t(i)].reward == delivery_reward(price, a[size_t(i)].count, d) && a[size_t(i)].reward % 5 == 0);
        }
        assert(same && differ);
        assert(delivery_reward(20, 3, 1000) == 150 && delivery_reward(1, 1, 0) == 5);
        // Nothing far enough at first: falls back to 150 m - 6 km.
        std::vector<Spot> close = {{{200, 0, 0}, "town"}, {{5000, 0, 0}, "town"}, {{9000, 0, 0}, "town"}};
        auto f = make_offers(prices, close, {0, 0, 0}, 1, 1, 3);
        assert(f.size() == 3);
        for (const auto& o : f) assert(o.spot == 0 || o.spot == 1);
        assert(make_offers({}, close, {}, 1, 1, 3).empty() && make_offers(prices, {}, {}, 1, 1, 3).empty());
        // Part rewards: three different parts near the reward.
        std::vector<int> parts = {10, 90, 100, 110, 400, 120, 95};
        auto r = reward_parts(parts, 100, 5);
        assert(r.size() == 3 && r[0] != r[1] && r[1] != r[2] && r[0] != r[2]);
        for (int i : r) assert(parts[size_t(i)] >= 50 && parts[size_t(i)] <= 130);
        auto few = reward_parts({10, 1000}, 100, 5);
        assert(few.size() == 2);
        // A job in hand survives a save.
        WorldState w;
        w.placed = true; w.board = 6; w.taken = {0, 2}; w.job_state = 2; w.job_item = "bandage"; w.job_count = 3; w.job_reward = 140;
        w.job_place = "gas_station"; w.job_at = {10.5, 2, -40}; w.job_parts = {"engine_a", "wheel_b", "pipe_c"};
        WorldState back;
        assert(load_world(save_world(w), back));
        assert(back.board == 6 && back.taken.size() == 2 && back.taken[1] == 2 && back.job_state == 2 && back.job_item == "bandage" &&
               back.job_count == 3 && back.job_reward == 140 && back.job_place == "gas_station" && back.job_at.z == -40 && back.job_parts.size() == 3 &&
               back.job_parts[2] == "pipe_c");
        WorldState none;
        assert(load_world(save_world(WorldState{}), none) && none.job_state == 0 && none.board == -1);
    }
    return 0;
}
