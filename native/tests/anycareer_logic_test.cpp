#include "../anycareer_logic.h"
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
    return 0;
}
