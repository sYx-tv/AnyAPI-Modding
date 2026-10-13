#pragma once
// CustomGame: the settings table, the per-world config file format, the day-cycle maths and the payload the host
// hides in the replicated time of day. No Windows or game calls, so tests/customgame_logic_test.cpp can check it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace customgame {

enum Category { kGeneral, kSurvival, kDanger, kWorld, kCategoryCount };
inline const char* const kCategoryNames[kCategoryCount] = {"General", "Survival", "Danger", "World"};

// How a value is shown. Percent-style settings are multipliers applied to the game's own default.
enum Format {
    kFmtMode,        // 0 Career, 1 Creative, 2 Sandbox (the game's e_gamemode: survival, creative, sandbox)
    kFmtDaySpeed,    // -1 normal, else a multiplier (0 = frozen)
    kFmtHour,        // -1 game default, else 0..23
    kFmtInt,         // plain number
    kFmtPercent,     // 0 shows as "Off" when off_at_zero
    kFmtOnOff,       // 1 on, 0 off
    kFmtDegrees,     // offset in degrees C from the game's value
    kFmtMinutes,     // multiplier of a game duration in seconds
    kFmtWeather,     // Weather enum
    kFmtJoints,      // percent, kUnbreakable shows as "Unbreakable"
};

enum Weather { kWeatherNormal, kWeatherClear, kWeatherCloudy, kWeatherRain, kWeatherStorm, kWeatherFog, kWeatherCount };
inline const char* const kWeatherNames[kWeatherCount] = {"Normal (changes)", "Always clear", "Always cloudy",
                                                         "Always rain", "Always stormy", "Always foggy"};

constexpr double kUnbreakable = 1000000;

enum Id {
    kMode, kDaySpeed, kStartHour, kStartTier, kMaxPlayers,
    kHunger, kThirst, kStaminaDrain, kStaminaRecover, kBreath, kSoak, kDry, kCorpseTime, kLostBodyTime, kColdest, kHottest,
    kZombies, kCreatures, kHordeSize, kMaxEnemies, kSight, kHearing, kSafeRadius,
    kLoot, kWeather, kPrecipitation, kJoints,
    kCount
};

struct Setting {
    const char* key;      // saved identity: never rename
    const char* label;
    const char* help;
    Category category;
    Format format;
    const double* values; // the stepper walks this list
    int count;
    double def;           // default value (must be in values)
    bool off_at_zero;
};

namespace values {
inline constexpr double mode[] = {0, 1, 2};
inline constexpr double day_speed[] = {-1, 0, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8};
inline constexpr double hour[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
inline constexpr double tier[] = {1, 2, 3, 4, 5};
inline constexpr double players[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
inline constexpr double drain[] = {0, 25, 50, 75, 100, 125, 150, 200, 300, 400};
inline constexpr double rate[] = {25, 50, 75, 100, 125, 150, 200, 300, 400};
inline constexpr double longer[] = {25, 50, 75, 100, 150, 200, 300, 500};
inline constexpr double duration[] = {25, 50, 100, 200, 400, 800};
inline constexpr double degrees[] = {-30, -25, -20, -15, -10, -5, 0, 5, 10, 15, 20, 25, 30};
inline constexpr double on_off[] = {1, 0};
inline constexpr double amount[] = {0, 25, 50, 75, 100, 150, 200, 300};
inline constexpr double enemies[] = {25, 50, 75, 100, 150, 200, 300};
inline constexpr double senses[] = {25, 50, 75, 100, 125, 150, 200, 300};
inline constexpr double radius[] = {0, 50, 100, 150, 200, 300};
inline constexpr double loot[] = {25, 50, 75, 100, 150, 200, 300, 500};
inline constexpr double weather[] = {0, 1, 2, 3, 4, 5};
inline constexpr double precipitation[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
inline constexpr double joints[] = {25, 50, 75, 100, 150, 200, 300, 500, kUnbreakable};
}  // namespace values

#define CG_LIST(a) values::a, int(sizeof(values::a) / sizeof(values::a[0]))
inline const Setting kSettings[kCount] = {
    {"mode", "Game mode", "Career, Creative (the dome) or Sandbox. The world starts exactly like that mode.", kGeneral, kFmtMode, CG_LIST(mode), 0, false},
    {"day_speed", "Day cycle speed", "How fast days pass. Normal keeps the game's rules (Sandbox time stands still).", kGeneral, kFmtDaySpeed, CG_LIST(day_speed), -1, false},
    {"start_hour", "Start time", "The time of day when the world begins.", kGeneral, kFmtHour, CG_LIST(hour), -1, false},
    {"start_tier", "Starting loot tier", "Tier 1 is normal. Each bunker you finish still raises it by one.", kGeneral, kFmtInt, CG_LIST(tier), 1, false},
    {"max_players", "Max players", "How many players can join (the game's limit is 6). Above 6 is untested.", kGeneral, kFmtInt, CG_LIST(players), 6, false},
    {"hunger", "Hunger speed", "How fast you get hungry.", kSurvival, kFmtPercent, CG_LIST(drain), 100, true},
    {"thirst", "Thirst speed", "How fast you get thirsty.", kSurvival, kFmtPercent, CG_LIST(drain), 100, true},
    {"stamina_drain", "Stamina drain", "How fast sprinting uses stamina.", kSurvival, kFmtPercent, CG_LIST(drain), 100, true},
    {"stamina_recover", "Stamina recovery", "How fast stamina comes back.", kSurvival, kFmtPercent, CG_LIST(rate), 100, false},
    {"breath", "Breath underwater", "How long your air lasts, and how long you last once it runs out.", kSurvival, kFmtPercent, CG_LIST(longer), 100, false},
    {"soak", "Clothes soak speed", "How fast rain and swimming soak your clothes.", kSurvival, kFmtPercent, CG_LIST(drain), 100, true},
    {"dry", "Clothes dry speed", "How fast wet clothes dry.", kSurvival, kFmtPercent, CG_LIST(rate), 100, false},
    {"corpse_time", "Bodies stay for", "How long dead zombies, creatures and survivors stay before they vanish.", kSurvival, kFmtMinutes, CG_LIST(duration), 100, false},
    {"lost_body_time", "Your lost body stays for", "How long your own body (and its gear) waits for you after you die.", kSurvival, kFmtMinutes, CG_LIST(duration), 100, false},
    {"coldest", "Coldest temperature", "Lower makes cold places and nights colder.", kSurvival, kFmtDegrees, CG_LIST(degrees), 0, false},
    {"hottest", "Hottest temperature", "Higher makes hot places and days hotter.", kSurvival, kFmtDegrees, CG_LIST(degrees), 0, false},
    {"zombies", "Zombies", "Turn zombie spawning off for a peaceful world.", kDanger, kFmtOnOff, CG_LIST(on_off), 1, false},
    {"creatures", "Creatures", "Turn creature spawning off.", kDanger, kFmtOnOff, CG_LIST(on_off), 1, false},
    {"horde_size", "Zombie horde size", "How many zombies come in a horde.", kDanger, kFmtPercent, CG_LIST(amount), 100, true},
    {"max_enemies", "Max enemies around", "The cap on spawned zombies, creatures and survivors at once.", kDanger, kFmtPercent, CG_LIST(enemies), 100, false},
    {"sight", "Enemy sight range", "How far zombies, creatures and survivors can see you.", kDanger, kFmtPercent, CG_LIST(senses), 100, false},
    {"hearing", "Enemy hearing range", "How far zombies, creatures and survivors can hear you.", kDanger, kFmtPercent, CG_LIST(senses), 100, false},
    {"safe_radius", "No-spawn zone", "How close to players new enemies may appear. Bigger keeps them further away.", kDanger, kFmtPercent, CG_LIST(radius), 100, false},
    {"loot", "Loot amount", "How much loot is in buildings, bunkers and wrecks.", kWorld, kFmtPercent, CG_LIST(loot), 100, false},
    {"weather", "Weather", "Lock the weather, or leave it changing as normal.", kWorld, kFmtWeather, CG_LIST(weather), 0, false},
    {"precipitation", "Rain and snow density", "How many rain drops and snowflakes you see. Looks only.", kWorld, kFmtPercent, CG_LIST(precipitation), 100, true},
    {"joints", "Joint strength", "How much force it takes to break hinges, latches, pins and rails.", kWorld, kFmtJoints, CG_LIST(joints), 100, false},
};
#undef CG_LIST

struct Config {
    double v[kCount];
    Config() { reset(); }
    void reset() { for (int i = 0; i < kCount; ++i) v[i] = kSettings[i].def; }
    double operator[](int i) const { return v[i]; }
    bool is_default(int i) const { return v[i] == kSettings[i].def; }
    bool all_default() const { for (int i = 1; i < kCount; ++i) if (!is_default(i)) return false; return true; }
    int mode() const { return int(v[kMode]); }
};

// Index of the closest listed value, so a hand-edited or older file still lands on a valid step.
inline int value_index(int id, double value) {
    const Setting& s = kSettings[id];
    int best = 0;
    for (int i = 1; i < s.count; ++i)
        if (std::fabs(s.values[i] - value) < std::fabs(s.values[best] - value)) best = i;
    return best;
}
inline void step(Config& c, int id, int delta) {
    const Setting& s = kSettings[id];
    int i = std::clamp(value_index(id, c.v[id]) + delta, 0, s.count - 1);
    c.v[id] = s.values[i];
}
inline void snap(Config& c) { for (int i = 0; i < kCount; ++i) c.v[i] = kSettings[i].values[value_index(i, c.v[i])]; }

// ------------------------------------------------------------------ file format: "key=value" lines
// Extra keys: "seed", "tier_applied" (world files only). Unknown keys are ignored.
struct WorldFile {
    Config config;
    int32_t seed = 0;
    bool tier_applied = false;
};
inline std::string serialize(const WorldFile& w, bool world) {
    std::string out = "# CustomGame settings. Edit with care: values snap to the nearest menu step.\n";
    char line[96];
    if (world) {
        snprintf(line, sizeof line, "seed=%d\ntier_applied=%d\n", int(w.seed), w.tier_applied ? 1 : 0);
        out += line;
    }
    for (int i = 0; i < kCount; ++i) {
        snprintf(line, sizeof line, "%s=%.17g\n", kSettings[i].key, w.config.v[i]);
        out += line;
    }
    return out;
}
inline bool parse(const std::string& text, WorldFile& w) {
    w = WorldFile{};
    size_t pos = 0;
    int known = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        char* stop = nullptr;
        double number = std::strtod(value.c_str(), &stop);
        if (stop == value.c_str() || !std::isfinite(number)) continue;
        if (key == "seed") { w.seed = int32_t(number); continue; }
        if (key == "tier_applied") { w.tier_applied = number != 0; continue; }
        for (int i = 0; i < kCount; ++i)
            if (key == kSettings[i].key) { w.config.v[i] = number; ++known; break; }
    }
    snap(w.config);
    return known > 0;
}

// ------------------------------------------------------------------ labels
// Game defaults are read from the running game; pass NaN when unknown.
struct GameDefaults {
    double corpse_seconds = NAN, lost_body_seconds = NAN, temperature_min = NAN, temperature_max = NAN;
    double day_minutes = NAN;
};
inline std::string value_text(int id, double value, const GameDefaults& d) {
    const Setting& s = kSettings[id];
    char b[96];
    switch (s.format) {
    case kFmtMode: return value == 1 ? "Creative" : value == 2 ? "Sandbox" : "Career";
    case kFmtDaySpeed:
        if (value < 0) return "Normal";
        if (value == 0) return "Frozen (time stands still)";
        if (std::isfinite(d.day_minutes) && d.day_minutes > 0) snprintf(b, sizeof b, "%gx  (%.0f min days)", value, d.day_minutes / value);
        else snprintf(b, sizeof b, "%gx", value);
        return b;
    case kFmtHour:
        if (value < 0) return "Game default";
        snprintf(b, sizeof b, "%02d:00", int(value));
        return b;
    case kFmtInt: snprintf(b, sizeof b, "%d", int(value)); return b;
    case kFmtPercent:
        if (value == 0 && s.off_at_zero) return "Off";
        snprintf(b, sizeof b, "%d%%", int(value));
        return b;
    case kFmtOnOff: return value != 0 ? "On" : "Off";
    case kFmtDegrees: {
        double base = id == kColdest ? d.temperature_min : d.temperature_max;
        if (std::isfinite(base)) snprintf(b, sizeof b, "%.0f \xC2\xB0" "C%s", base + value, value == 0 ? "  (normal)" : "");
        else if (value == 0) snprintf(b, sizeof b, "Normal");
        else snprintf(b, sizeof b, "%+.0f \xC2\xB0" "C", value);
        return b;
    }
    case kFmtMinutes: {
        double base = id == kCorpseTime ? d.corpse_seconds : d.lost_body_seconds;
        if (std::isfinite(base) && base > 0) {
            double minutes = base * value / 100 / 60;
            if (minutes < 10) snprintf(b, sizeof b, "%.1f min", minutes);
            else snprintf(b, sizeof b, "%.0f min", minutes);
        } else snprintf(b, sizeof b, "%d%%", int(value));
        return b;
    }
    case kFmtWeather: return kWeatherNames[std::clamp(int(value), 0, kWeatherCount - 1)];
    case kFmtJoints:
        if (value >= kUnbreakable) return "Unbreakable";
        snprintf(b, sizeof b, "%d%%", int(value));
        return b;
    }
    return "";
}
// One line for the presets page.
inline std::string summary(const Config& c) {
    std::string s = value_text(kMode, c[kMode], {});
    int changed = 0;
    for (int i = 1; i < kCount; ++i) changed += !c.is_default(i);
    char b[48];
    snprintf(b, sizeof b, changed == 1 ? ", 1 setting changed" : ", %d settings changed", changed);
    return s + b;
}

// ------------------------------------------------------------------ game values
// Seconds-style values: a rate of p% divides the game's duration, 0% means it never happens.
inline double scaled_duration(double base, double percent) { return percent <= 0 ? 1e15 : base * 100.0 / percent; }
inline double scaled(double base, double percent) { return base * percent / 100.0; }
inline int32_t scaled_count(int32_t base, double percent, int32_t minimum) {
    return std::max(minimum, int32_t(std::lround(double(base) * percent / 100.0)));
}

// Whether the host needs to drive the replicated time-of-day override: any time setting, or a setting that rides
// along in the payload for joining players.
inline bool needs_time_override(const Config& c) {
    return !c.is_default(kDaySpeed) || !c.is_default(kStartHour) || !c.is_default(kWeather) || !c.is_default(kPrecipitation);
}

// The game: cycle = ((tick + start_offset) % day_ticks) / day_ticks, except Sandbox, which holds sandbox_factor.
inline double day_factor(const Config& c, int32_t tick, int32_t day_ticks, int32_t start_offset, double sandbox_factor) {
    if (day_ticks <= 0) day_ticks = 1;
    bool sandbox = c.mode() == 2;
    double start = c[kStartHour] >= 0 ? c[kStartHour] / 24.0
                 : sandbox ? sandbox_factor
                 : double(start_offset % day_ticks) / double(day_ticks);
    double speed = c[kDaySpeed] >= 0 ? c[kDaySpeed] : (sandbox ? 0.0 : 1.0);
    double f = start + double(tick) * speed / double(day_ticks);
    f -= std::floor(f);
    return f >= 0 && f < 1 ? f : 0.0;
}

// ------------------------------------------------------------------ payload in the time of day
// The host writes the time of day rounded to 2^-20 of a day (under 0.1 s of a 24 min day) and puts 20 payload bits
// at 2^-21..2^-40. The value replicates as a full f64, so a joining player's mod reads the bits back while the
// game (and vanilla players) just see the time.
constexpr uint32_t kMagic = 0xC6;
inline uint32_t checksum(uint32_t weather, uint32_t precipitation) { return (weather * 7 + precipitation * 13 + 11) & 31; }
inline uint32_t pack(int weather, double precipitation_percent) {
    uint32_t w = uint32_t(std::clamp(weather, 0, 7));
    uint32_t p = uint32_t(std::clamp(int(std::lround(precipitation_percent / 10)), 0, 10));
    return (kMagic << 12) | (w << 9) | (p << 5) | checksum(w, p);
}
inline double encode(double factor, uint32_t payload) {
    const double unit = 1048576.0;   // 2^20
    double q = std::floor(std::clamp(factor, 0.0, 1.0) * unit) / unit;
    if (q >= 1) q = 0;
    return q + double(payload & 0xFFFFF) / 1099511627776.0;   // 2^40
}
inline bool decode(double value, int& weather, double& precipitation_percent) {
    if (!(value >= 0 && value < 1)) return false;
    double scaled_value = value * 1099511627776.0;   // exact: the low 40 bits are the payload position
    if (scaled_value != std::floor(scaled_value)) return false;
    uint32_t p = uint32_t(uint64_t(scaled_value) & 0xFFFFF);
    uint32_t w = (p >> 9) & 7, d = (p >> 5) & 15;
    if ((p >> 12) != kMagic || d > 10 || w >= kWeatherCount || (p & 31) != checksum(w, d)) return false;
    weather = int(w);
    precipitation_percent = d * 10.0;
    return true;
}

// ------------------------------------------------------------------ weather lock
struct WeatherData { double cloud, precipitation, fog, temperature, wind_x, wind_y; };   // weather_data layout
inline void lock_weather(WeatherData& w, int weather) {
    switch (weather) {
    case kWeatherClear: w.cloud = 0; w.precipitation = 0; w.fog = 0; break;
    case kWeatherCloudy: w.cloud = 0.85; w.precipitation = 0; w.fog = std::min(w.fog, 0.15); break;
    case kWeatherRain: w.cloud = 1; w.precipitation = 0.7; w.fog = std::max(w.fog, 0.2); break;
    case kWeatherStorm: w.cloud = 1; w.precipitation = 1; w.fog = std::max(w.fog, 0.3); w.wind_x *= 2; w.wind_y *= 2; break;
    case kWeatherFog: w.cloud = std::max(w.cloud, 0.5); w.precipitation = 0; w.fog = 1; break;
    default: break;
    }
}

}  // namespace customgame
