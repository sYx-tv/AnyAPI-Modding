#include "../customgame_logic.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
using namespace customgame;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e; }
int main() {
    // Settings table: unique keys, every default is one of its listed values, ids line up with the table.
    std::set<std::string> keys;
    for (int i = 0; i < kCount; ++i) {
        const Setting& s = kSettings[i];
        assert(keys.insert(s.key).second && s.count > 1 && s.category >= 0 && s.category < kCategoryCount);
        assert(s.values[value_index(i, s.def)] == s.def);
    }
    assert(!std::strcmp(kSettings[kMode].key, "mode") && !std::strcmp(kSettings[kMaxPlayers].key, "max_players"));
    assert(!std::strcmp(kSettings[kJoints].key, "joints") && !std::strcmp(kSettings[kSight].key, "sight"));
    // Steppers clamp at both ends; odd values snap to the nearest step.
    Config c;
    assert(c.all_default() && c.mode() == 0 && c[kMaxPlayers] == 6);
    for (int i = 0; i < 40; ++i) step(c, kMaxPlayers, +1);
    assert(c[kMaxPlayers] == 20);
    for (int i = 0; i < 40; ++i) step(c, kMaxPlayers, -1);
    assert(c[kMaxPlayers] == 2);
    step(c, kMode, +1); assert(c.mode() == 1 && !c.all_default());
    c.v[kLoot] = 160; snap(c); assert(c[kLoot] == 150);

    // Files round-trip, ignore junk and unknown keys, and snap hand edits.
    WorldFile w; w.config = c; w.seed = -123456; w.tier_applied = true; w.config.v[kJoints] = kUnbreakable; w.config.v[kStartHour] = 7;
    WorldFile back;
    assert(parse(serialize(w, true), back));
    assert(back.seed == -123456 && back.tier_applied && back.config[kJoints] == kUnbreakable && back.config[kStartHour] == 7);
    for (int i = 0; i < kCount; ++i) assert(back.config[i] == w.config[i]);
    assert(parse("# x\r\nfoo=3\nhunger=abc\nthirst=190\r\n", back) && back.config[kThirst] == 200 && back.config[kHunger] == 100 && back.seed == 0);
    assert(!parse("nothing here\n", back));
    WorldFile preset; preset.config = c;
    assert(serialize(preset, false).find("seed=") == std::string::npos);

    // Labels.
    GameDefaults d; d.day_minutes = 24; d.temperature_min = -20; d.temperature_max = 40;
    assert(value_text(kMode, 2, d) == "Sandbox" && value_text(kDaySpeed, -1, d) == "Normal");
    assert(value_text(kDaySpeed, 2, d).find("12 min") != std::string::npos);
    assert(!value_text(kWeather, 3, d).empty() && !value_text(kJoints, kUnbreakable, d).empty());
    assert(!summary(c).empty());

    // Game values.
    assert(near(scaled_duration(600, 200), 300) && scaled_duration(600, 0) >= 1e15 && near(scaled(10, 50), 5));
    assert(scaled_count(10, 0, 0) == 0 && scaled_count(10, 25, 1) == 3 && scaled_count(2, 10, 1) == 1);

    // Day clock: normal settings reproduce the game's own formula; speed, start hour and frozen work.
    Config t;
    assert(!needs_time_override(t));
    const int day = 36000, offset = 9000;
    for (int tick : {0, 1, 17999, 36000, 99999}) assert(near(day_factor(t, tick, day, offset, 0.4), double((tick + offset) % day) / day));
    t.v[kDaySpeed] = 2; assert(needs_time_override(t) && near(day_factor(t, 9000, day, offset, 0), 0.75));
    t.v[kDaySpeed] = 0; t.v[kStartHour] = 18; assert(near(day_factor(t, 123456, day, offset, 0), 0.75));
    Config sb; sb.v[kMode] = 2; assert(near(day_factor(sb, 5000, day, offset, 0.4), 0.4));
    sb.v[kDaySpeed] = 1; assert(near(day_factor(sb, 3600, day, offset, 0.4), 0.5));
    Config wx; wx.v[kWeather] = kWeatherStorm; assert(needs_time_override(wx));

    // Payload: survives the round trip for every weather and density, barely moves the clock, rejects plain times.
    for (int weather = 0; weather < kWeatherCount; ++weather)
        for (int p = 0; p <= 100; p += 10)
            for (double f : {0.0, 0.25, 0.4999, 0.999999}) {
                double v = encode(f, pack(weather, p));
                int w2 = -1; double p2 = -1;
                assert(decode(v, w2, p2) && w2 == weather && p2 == p);
                assert(v >= 0 && v < 1 && std::abs(v - f) < 2.0 / 1048576.0);
            }
    int w2; double p2;
    for (double plain : {0.0, 0.1, 0.3333333333333333, 0.5, 0.75, 12345.0 / 36000.0}) assert(!decode(plain, w2, p2));
    assert(!decode(-0.5, w2, p2) && !decode(1.0, w2, p2) && !decode(NAN, w2, p2));
    double corrupt = encode(0.5, pack(kWeatherRain, 50) ^ 1);
    assert(!decode(corrupt, w2, p2));

    // Weather lock only changes the locked fields.
    WeatherData base{0.3, 0.4, 0.1, 12, 1, -2};
    WeatherData a = base; lock_weather(a, kWeatherNormal); assert(a.cloud == 0.3 && a.temperature == 12);
    a = base; lock_weather(a, kWeatherClear); assert(a.cloud == 0 && a.precipitation == 0 && a.temperature == 12);
    a = base; lock_weather(a, kWeatherStorm); assert(a.precipitation == 1 && a.wind_x == 2 && a.wind_y == -4 && a.temperature == 12);
    a = base; lock_weather(a, kWeatherFog); assert(a.fog == 1 && a.precipitation == 0);
    return 0;
}
