#pragma once
// The game's sun path as a pure function of the cycle factor t (scene+0xba8),
// so the night sky, moon and moonlight can follow the real clock while the game
// parks scene+0xa78 at the noon direction for t<0.2 || t>0.8 (AAA F2, K16).
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace celestial {
constexpr double PI = 3.14159265358979323846;
constexpr double LATITUDE = 0.2 * PI;               // L: noon elevation 54 degrees
constexpr double NOON_Y = 0.80901699437494742;      // cos(L), the parked y
constexpr double CALIBRATE_DOT = .999;              // a sign pair must match the game's sun this well
constexpr double CHECK_DOT = .99;                   // calibrated day frames must keep matching this well
constexpr int64_t DAY_TICKS = 216000, START_OFFSET = 72000; // g_day_night_cycle_ticks, g_time_start_offset
constexpr int64_t LUNATION_DAYS = 8;

struct Calibration { int sx{0}, sz{0}; bool done{false}; };  // signs of x/z, fixed once from a day frame (0.2<=t<=0.8)
struct Result { bool valid; bool parked; double sun[3], moon[3], sun_y, night, star_angle; uint32_t day_index; float phase, lit_fraction; };

inline bool parked(double t) { return t < 0.2 || t > 0.8; }
// s(t) = (sx cosL sin2pi t, -cosL cos2pi t, -sz sinL), a unit vector toward the sun.
inline void path(double t, int sx, int sz, double out[3]) {
 double a = 2 * PI * t;
 out[0] = sx * std::cos(LATITUDE) * std::sin(a); out[1] = -std::cos(LATITUDE) * std::cos(a); out[2] = -sz * std::sin(LATITUDE);
}
inline double smoothstep(double e0, double e1, double x) { double k = (x - e0) / (e1 - e0); k = k < 0 ? 0 : k > 1 ? 1 : k; return k * k * (3 - 2 * k); }
inline double dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// The stored scene+0xa78 points away from the sun; this returns the unit vector toward it.
inline bool toward(const double native[3], double out[3]) {
 if (!native) return false;
 double m = dot(native, native);
 if (!std::isfinite(m) || m < .01) return false;
 double l = std::sqrt(m); for (int i = 0; i < 3; ++i) out[i] = -native[i] / l;
 return true;
}
inline void phase_of(int64_t tick, Result& r) {
 if (tick < 0) { r.day_index = 0; r.phase = 0; r.lit_fraction = 1; return; }
 r.day_index = uint32_t((tick + START_OFFSET) / DAY_TICKS);                  // increments at midnight (t = 0)
 int64_t night = (tick + START_OFFSET + DAY_TICKS / 2) / DAY_TICKS;           // increments at noon, while the moon is down
 r.phase = float(double(night % LUNATION_DAYS) / double(LUNATION_DAYS));
 r.lit_fraction = float((1 + std::cos(2 * PI * r.phase)) / 2);
}
// native_sun_dir: scene+0xa78 as stored. tick: the world_time tick, -1 unknown.
// Calibrates cal on the first day frame where exactly one of the 4 sign pairs
// matches; until then, or for t outside 0..1, or for a calibrated day frame
// that disagrees with the game's sun, valid is false.
inline Result evaluate(double t, const double native_sun_dir[3], int64_t tick, Calibration& cal) {
 Result r{};
 phase_of(tick, r);
 if (!std::isfinite(t) || t < 0 || t > 1) return r;
 r.parked = parked(t);
 double n[3]{}, s[3]{};
 bool have = toward(native_sun_dir, n);
 if (!cal.done && !r.parked && have) {
  int hits = 0, hx = 0, hz = 0;
  for (int sx : {1, -1}) for (int sz : {1, -1}) { path(t, sx, sz, s); if (dot(s, n) > CALIBRATE_DOT) { ++hits; hx = sx; hz = sz; } }
  if (hits == 1) cal = {hx, hz, true};
 }
 if (!cal.done) return r;
 path(t, cal.sx, cal.sz, r.sun);
 double tm = t + .5; if (tm >= 1) tm -= 1;
 path(tm, cal.sx, cal.sz, r.moon);
 r.sun_y = r.sun[1]; r.night = smoothstep(.05, -.12, r.sun_y); r.star_angle = -2 * PI * t;
 r.valid = r.parked || !have || dot(r.sun, n) >= CHECK_DOT;
 return r;
}
// Fallback without a usable clock: true while the game's sun is parked at the
// noon direction and its main light is dark (night). Callers hold the moon and stars.
inline bool parked_detector(const double native_sun_dir[3], const double light_main[3]) {
 double n[3]{};
 if (!toward(native_sun_dir, n) || !light_main) return false;
 double luma = .2126 * light_main[0] + .7152 * light_main[1] + .0722 * light_main[2];
 return std::isfinite(luma) && std::abs(n[1] - NOON_Y) < 1e-4 && luma < .01;
}
}  // namespace celestial
