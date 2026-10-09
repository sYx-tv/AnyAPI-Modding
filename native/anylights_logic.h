#pragma once
// AnyLights colours, patterns, the name tag that saves them and the 32-bit word that syncs them.
// Pure and platform-free so it is unit tested without the game.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace anylights {
enum Pattern : uint8_t { kSteady, kBlink, kStrobe, kDoubleFlash, kPulse, kAlternateA, kAlternateB, kColourCycle, kPatternCount };
inline const char* const kPatternNames[kPatternCount] = {"Steady", "Blink", "Strobe", "Double flash", "Pulse", "Alternate A", "Alternate B", "Colour cycle"};
constexpr int kSpeedCount = 8;
inline const double kSpeedHz[kSpeedCount] = {0.25, 0.5, 1, 1.5, 2, 3, 4, 6};
constexpr uint8_t kDefaultSpeed = 2;   // 1 Hz

struct Colour { uint8_t r = 0, g = 0, b = 0; };
inline bool operator==(const Colour& a, const Colour& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// What the player chose in the Properties tool. has_colour false keeps the light's stock colour.
struct Config {
    bool has_colour = false;
    Colour colour;
    uint8_t pattern = kSteady;
    uint8_t speed = kDefaultSpeed;
    bool modded() const { return has_colour || pattern != kSteady; }
};
inline bool operator==(const Config& a, const Config& b) {
    return a.has_colour == b.has_colour && (!a.has_colour || a.colour == b.colour) && a.pattern == b.pattern && (a.pattern == kSteady || a.speed == b.speed);
}

// ------------------------------------------------------------------ name tag
// Saved as the last word of the light's name (its Properties alias): "AL" + RRGGBB + pattern + speed, all hex,
// e.g. "Left beacon ALff220032" (red-orange, strobe, 1.5 Hz). Letters and digits only, so the game's name field accepts it. RRGGBB 000000
// means the stock colour. Vanilla players just see the word in the name.
constexpr size_t kTagLength = 10;

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool parse_tag(const char* word, size_t length, Config& out) {
    if (length != kTagLength || word[0] != 'A' || word[1] != 'L') return false;
    int d[8];
    for (int i = 0; i < 8; ++i) if ((d[i] = hex_value(word[2 + i])) < 0) return false;
    Config c;
    c.colour = {uint8_t(d[0] * 16 + d[1]), uint8_t(d[2] * 16 + d[3]), uint8_t(d[4] * 16 + d[5])};
    c.has_colour = !(c.colour == Colour{});
    c.pattern = uint8_t(d[6] < kPatternCount ? d[6] : kSteady);
    c.speed = uint8_t(d[7] < kSpeedCount ? d[7] : kDefaultSpeed);
    out = c;
    return true;
}
// Splits a name into the player's own text and the AnyLights tag (if its last word is one).
inline bool split_name(const std::string& name, std::string& base, Config& config) {
    size_t end = name.find_last_not_of(' ');
    if (end == std::string::npos) { base.clear(); config = {}; return false; }
    size_t start = name.find_last_of(' ', end);
    start = start == std::string::npos ? 0 : start + 1;
    Config parsed;
    if (!parse_tag(name.data() + start, end + 1 - start, parsed)) { base = name.substr(0, end + 1); config = {}; return false; }
    size_t base_end = start ? name.find_last_not_of(' ', start - 1) : std::string::npos;
    base = base_end == std::string::npos ? std::string() : name.substr(0, base_end + 1);
    config = parsed;
    return true;
}
inline std::string tag(const Config& c) {
    static const char* digits = "0123456789abcdef";
    Colour col = c.has_colour ? c.colour : Colour{};
    uint8_t v[3] = {col.r, col.g, col.b};
    std::string out = "AL";
    for (uint8_t x : v) { out += digits[x >> 4]; out += digits[x & 15]; }
    out += digits[c.pattern < kPatternCount ? c.pattern : 0];
    out += digits[c.speed < kSpeedCount ? c.speed : kDefaultSpeed];
    return out;
}
// The name to save: the player's text plus the tag, or just their text when the light is back to stock.
inline std::string join_name(const std::string& base, const Config& c) {
    if (!c.modded()) return base;
    return base.empty() ? tag(c) : base + " " + tag(c);
}

// ------------------------------------------------------------------ sync word
// Sent by the host to every player as the light's component ability event. Bit 31 marks AnyLights (vanilla
// abilities are small positive numbers); 30-28 pattern; 27-25 speed; 24 custom colour; 23-0 RGB.
// 0x80000000 means "back to stock".
constexpr uint32_t kMarker = 0x80000000u;
inline int32_t pack(const Config& c) {
    uint32_t w = kMarker | (uint32_t(c.pattern & 7) << 28) | (uint32_t(c.speed & 7) << 25);
    if (c.has_colour) w |= (1u << 24) | (uint32_t(c.colour.r) << 16) | (uint32_t(c.colour.g) << 8) | c.colour.b;
    int32_t out; std::memcpy(&out, &w, 4);
    return out;
}
inline bool is_word(int32_t value) { return value < 0; }
inline Config unpack(int32_t value) {
    uint32_t w; std::memcpy(&w, &value, 4);
    Config c;
    c.pattern = uint8_t((w >> 28) & 7);
    c.speed = uint8_t((w >> 25) & 7);
    c.has_colour = (w >> 24) & 1;
    if (c.has_colour) c.colour = {uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w)};
    return c;
}

// ------------------------------------------------------------------ data inputs
// Values written by data links or a microcontroller. NaN means "not connected": the Properties choice stays.
struct Inputs { double r = NAN, g = NAN, b = NAN, pattern = NAN, speed = NAN; };
inline uint8_t channel(double v) {
    if (!std::isfinite(v) || v <= 0) return 0;
    if (v <= 1) return uint8_t(std::lround(v * 255));
    return v >= 255 ? 255 : uint8_t(std::lround(v));   // 0-255 also accepted
}
inline uint8_t nearest_speed(double hz) {
    uint8_t best = kDefaultSpeed; double best_err = 1e9;
    for (uint8_t i = 0; i < kSpeedCount; ++i) {
        double err = std::fabs(std::log(kSpeedHz[i]) - std::log(hz));
        if (err < best_err) { best_err = err; best = i; }
    }
    return best;
}
inline Config effective(const Config& chosen, const Inputs& in) {
    Config c = chosen;
    if (std::isfinite(in.r) || std::isfinite(in.g) || std::isfinite(in.b)) {
        c.colour = {channel(in.r), channel(in.g), channel(in.b)};
        c.has_colour = !(c.colour == Colour{});
    }
    if (std::isfinite(in.pattern)) {
        long p = std::lround(in.pattern);
        c.pattern = uint8_t(p >= 0 && p < kPatternCount ? p : kSteady);
    }
    if (std::isfinite(in.speed) && in.speed > 0) c.speed = nearest_speed(in.speed);
    return c;
}

// ------------------------------------------------------------------ animation
// Brightness factor 0-1 for a pattern at `seconds` (any shared clock).
inline double level(uint8_t pattern, uint8_t speed, double seconds) {
    double hz = kSpeedHz[speed < kSpeedCount ? speed : kDefaultSpeed];
    double phase = seconds * hz; phase -= std::floor(phase);
    switch (pattern) {
    case kBlink: case kAlternateA: return phase < 0.5 ? 1 : 0;
    case kAlternateB: return phase < 0.5 ? 0 : 1;
    case kStrobe: return phase < 0.12 ? 1 : 0;
    case kDoubleFlash: return (phase < 0.08 || (phase >= 0.18 && phase < 0.26)) ? 1 : 0;
    case kPulse: return 0.15 + 0.85 * (0.5 - 0.5 * std::cos(phase * 6.283185307179586));
    default: return 1;
    }
}
inline bool lit(uint8_t pattern, uint8_t speed, double seconds) { return level(pattern, speed, seconds) >= 0.5; }

inline Colour hsv(double hue, double sat, double val) {
    hue -= std::floor(hue); hue *= 6;
    int i = int(hue); double f = hue - i, p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
    double r, g, b;
    switch (i % 6) { case 0: r = val; g = t; b = p; break; case 1: r = q; g = val; b = p; break; case 2: r = p; g = val; b = t; break;
                     case 3: r = p; g = q; b = val; break; case 4: r = t; g = p; b = val; break; default: r = val; g = p; b = q; }
    return {uint8_t(std::lround(r * 255)), uint8_t(std::lround(g * 255)), uint8_t(std::lround(b * 255))};
}
// Colour to show at `seconds`: the chosen colour, or a hue sweep for Colour cycle.
inline bool colour_at(const Config& c, double seconds, Colour& out) {
    if (c.pattern == kColourCycle) { out = hsv(seconds * kSpeedHz[c.speed < kSpeedCount ? c.speed : kDefaultSpeed] * 0.25, 1, 1); return true; }
    if (!c.has_colour) return false;
    out = c.colour;
    return true;
}
}  // namespace anylights
