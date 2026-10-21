#pragma once
// EngineSound: saves that still play without the mod. A preset or Custom sound is a number the game itself does not
// know (enginesound_params.h), and the game has no fallback for one: get_selected_sound_effect names an effect only
// for 0, 1 and 2 and otherwise leaves its result unset, so an engine loaded without EngineSound (mods off, or a game
// update that turns them off) would play whatever value was left in memory. So a save only ever holds a stock sound:
//   - world saves are binary and read by position, so nothing can be added to them. The engine is saved with
//     kSavedStockSound and the real choice goes in a small file in the game's working folder, which the game copies
//     into the save slot when it saves and back out when it loads (the AnyEngineParts turbo wear travels the same
//     way, turbo_wear_store.h).
//   - saved vehicles are JSON and read by name. The engine is saved with kSavedStockSound and the real choice under
//     kSaveKey, which the game never reads.
// Without the mod an engine loads with the stock sound; with it, the choice comes back. An entry is used only while
// the engine still holds the stock sound it was saved with, so a sound changed while the mod was off is kept.
//
// World file entries are keyed by the vehicle's saved id and the engine's saved part id, both kept across loads.
// Pure: no game calls, tested in enginesound_params_test.cpp.
//
// File: {"version": 1, "sounds": {"12:34": [5, 0], "12:40": [-2147480000, 0]}}  ("vehicle:part": [choice, stock sound saved])
#include "enginesound_params.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace enginesound {

inline constexpr int32_t kSavedStockSound = kVanillaMin;
inline constexpr const char* kSaveKey = "enginesound_sound";

// A sound the stock game can't play, which a save must not hold.
inline bool needs_stock_save(int32_t value) { return value < kVanillaMin || value > kVanillaMax; }

struct SoundKey {
    int32_t vehicle = 0, component = 0;
    bool operator<(const SoundKey& o) const { return vehicle != o.vehicle ? vehicle < o.vehicle : component < o.component; }
};
struct SavedSound { int32_t choice = 0, stock = kSavedStockSound; };
using SoundMap = std::map<SoundKey, SavedSound>;

// An engine is being written to a world save with `value`: keep its choice, or forget it once it is a stock sound.
// True when the map changed.
inline bool note_saved(SoundMap& m, const SoundKey& k, int32_t value) {
    auto found = m.find(k);
    if (!needs_stock_save(value)) {
        if (found == m.end()) return false;
        m.erase(found);
        return true;
    }
    if (found != m.end() && found->second.choice == value && found->second.stock == kSavedStockSound) return false;
    m[k] = SavedSound{value, kSavedStockSound};
    return true;
}
// An engine was read from a world save holding `loaded`: the sound to give it.
inline int32_t restored(const SoundMap& m, const SoundKey& k, int32_t loaded) {
    auto found = m.find(k);
    if (found == m.end() || found->second.stock != loaded || !needs_stock_save(found->second.choice)) return loaded;
    return found->second.choice;
}
// An engine was read from a saved vehicle holding `loaded`, and kSaveKey held `kept` (`loaded` when missing).
inline int32_t restored_from_key(int32_t loaded, int32_t kept) {
    return loaded == kSavedStockSound && needs_stock_save(kept) ? kept : loaded;
}

inline std::string encode_saved(const SoundMap& m) {
    std::string out = "{\"version\": 1, \"sounds\": {";
    bool first = true;
    char line[96];
    for (auto& [k, v] : m) {
        std::snprintf(line, sizeof line, "%s\n  \"%d:%d\": [%d, %d]", first ? "" : ",", int(k.vehicle), int(k.component), int(v.choice), int(v.stock));
        out += line;
        first = false;
    }
    return out + "\n}}\n";
}

// Reads every "<vehicle>:<part>": [choice, stock] pair; anything else is skipped, and so are entries whose choice
// is a stock sound or whose stock sound is not one.
inline SoundMap decode_saved(const std::string& text) {
    SoundMap out;
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        size_t end = text.find('"', i + 1);
        if (end == std::string::npos) break;
        std::string key = text.substr(i + 1, end - i - 1);
        i = end + 1;
        char* p = nullptr;
        long long vehicle = std::strtoll(key.c_str(), &p, 10);
        if (p == key.c_str() || *p != ':') continue;
        const char* second = p + 1;
        long long component = std::strtoll(second, &p, 10);
        if (p == second || *p) continue;
        size_t at = text.find_first_not_of(" \t\r\n", i);
        if (at == std::string::npos || text[at] != ':') continue;
        at = text.find_first_not_of(" \t\r\n", at + 1);
        if (at == std::string::npos || text[at] != '[') continue;
        const char* start = text.c_str() + at + 1;
        char* stop = nullptr;
        long long choice = std::strtoll(start, &stop, 10);
        if (stop == start) continue;
        while (*stop == ' ' || *stop == '\t' || *stop == '\r' || *stop == '\n') ++stop;
        if (*stop != ',') continue;
        start = stop + 1;
        long long stock = std::strtoll(start, &stop, 10);
        auto fits = [](long long v) { return v >= INT32_MIN && v <= INT32_MAX; };
        if (stop == start || !fits(choice) || !fits(stock) || !fits(vehicle) || !fits(component)) continue;
        if (!needs_stock_save(int32_t(choice)) || needs_stock_save(int32_t(stock))) continue;
        out[SoundKey{int32_t(vehicle), int32_t(component)}] = SavedSound{int32_t(choice), int32_t(stock)};
    }
    return out;
}

}  // namespace enginesound
