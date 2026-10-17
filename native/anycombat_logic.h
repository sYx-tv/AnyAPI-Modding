#pragma once
// AnyCombat pure logic: projecting hit points onto the screen, merging rapid hits on one target into one number,
// watching hit targets for a kill, and the timing and formatting of hit markers and damage numbers. No Windows or game dependencies, so
// tests/anycombat_logic_test.cpp covers it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace anycombat {

struct Vec3 { double x{}, y{}, z{}; };
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The game's camera as client_scene.get_camera returns it (application.graphics.camera): position plus the
// forward, up and right directions in world space, and tan(vertical fov / 2).
struct Camera {
    Vec3 position, forward, up, right;
    double tan_half_fov{}, near_plane{0.05};
    bool valid() const {
        return std::isfinite(tan_half_fov) && tan_half_fov > 0.01 && tan_half_fov < 20 &&
               std::fabs(dot(forward, forward) - 1) < 0.05 && std::fabs(dot(up, up) - 1) < 0.05 && std::fabs(dot(right, right) - 1) < 0.05;
    }
};

// World point to screen pixels. False when the point is behind the camera or the camera is unusable.
inline bool project(const Camera& c, Vec3 world, double width, double height, double* sx, double* sy) {
    if (!c.valid() || !(width > 0) || !(height > 0)) return false;
    Vec3 d = sub(world, c.position);
    double z = dot(d, c.forward);
    if (!(z > std::max(c.near_plane, 0.01))) return false;
    double aspect = width / height;
    double nx = dot(d, c.right) / (z * c.tan_half_fov * aspect), ny = dot(d, c.up) / (z * c.tan_half_fov);
    if (!std::isfinite(nx) || !std::isfinite(ny)) return false;
    *sx = (nx + 1) * 0.5 * width;
    *sy = (1 - ny) * 0.5 * height;
    return true;
}

// Damage types (e_actor_damage_type, 0.1.25 reference order).
enum DamageType : int32_t { kMelee = 0, kGun = 1, kImpact = 2, kGrenade = 3, kExplosion = 4, kInjury = 5, kAcid = 6 };

constexpr double kMarkerSeconds = 0.28;   // hit marker on screen
constexpr double kNumberSeconds = 1.1;    // damage number rise and fade
constexpr double kMergeSeconds = 0.35;    // hits on the same target this close together add up (shotgun pellets, bursts)
constexpr double kRiseFraction = 0.06;    // of screen height, over a number's life
constexpr int kMaxNumbers = 24;
constexpr double kKillWindow = 1.5;       // a target that goes down this soon after your hit counts as your kill
constexpr int kMaxWatched = 16;

struct Number {
    int32_t actor{-1};
    double damage{}, born{}, last_hit{};
    Vec3 position;   // world position the number is drawn at
    Vec3 offset;     // the hit point relative to the target, so the number can follow it
    int32_t type{};
    bool used{};
    bool kill{};
};

// Fixed pool of floating numbers. A hit on the same actor within kMergeSeconds of its last hit adds to that number
// (and restarts its life at the newest hit point); otherwise a new number replaces the oldest when the pool is full.
struct Numbers {
    Number items[kMaxNumbers];
    void add(int32_t actor, double damage, Vec3 position, Vec3 offset, int32_t type, double now) {
        Number* best = nullptr;
        for (auto& n : items)
            if (n.used && n.actor == actor && actor >= 0 && now - n.last_hit <= kMergeSeconds) { best = &n; break; }
        if (best) {
            best->damage += damage; best->last_hit = now; best->born = now; best->position = position; best->offset = offset;
            if (type != best->type && (type == kGun || type == kMelee)) best->type = type;
            return;
        }
        Number* slot = nullptr;
        for (auto& n : items) if (!n.used) { slot = &n; break; }
        if (!slot) { slot = &items[0]; for (auto& n : items) if (n.born < slot->born) slot = &n; }
        *slot = Number{actor, damage, now, now, position, offset, type, true, false};
    }
    // The newest number for this actor turns into a kill (red, and it restarts its life so the kill is seen).
    void mark_kill(int32_t actor, double now) {
        Number* best = nullptr;
        for (auto& n : items) if (n.used && n.actor == actor && (!best || n.born > best->born)) best = &n;
        if (best) { best->kill = true; best->born = now; }
    }
    void expire(double now) { for (auto& n : items) if (n.used && now - n.born > kNumberSeconds) n.used = false; }
    int count() const { int c = 0; for (auto& n : items) c += n.used; return c; }
};

// 0..1 through a number's life: alpha fades out over the last 40%, and it rises with an ease-out.
inline double number_alpha(double age) { if (age < 0 || age > kNumberSeconds) return 0; double t = age / kNumberSeconds; return t < 0.6 ? 1.0 : std::max(0.0, 1 - (t - 0.6) / 0.4); }
inline double number_rise(double age) { double t = std::clamp(age / kNumberSeconds, 0.0, 1.0); return 1 - (1 - t) * (1 - t); }
// Pop: numbers start 35% larger and settle within 0.15 s.
inline double number_scale(double age) { return 1.0 + 0.35 * std::max(0.0, 1 - age / 0.15); }
// Hit marker: grows out from the crosshair, then fades.
inline double marker_alpha(double age) { if (age < 0 || age > kMarkerSeconds) return 0; double t = age / kMarkerSeconds; return t < 0.5 ? 1.0 : 1 - (t - 0.5) / 0.5; }
inline double marker_spread(double age) { double t = std::clamp(age / 0.08, 0.0, 1.0); return 0.7 + 0.3 * t; }

// Whole numbers from 1 up, one decimal below 10 when there is a fraction, two significant digits under 1.
inline int format_damage(double damage, wchar_t* out, int cap) {
    if (!std::isfinite(damage) || damage < 0) damage = 0;
    int n;
    if (damage >= 10 || std::fabs(damage - std::round(damage)) < 0.05) n = std::swprintf(out, cap, L"%.0f", std::round(damage));
    else if (damage >= 1) n = std::swprintf(out, cap, L"%.1f", damage);
    else n = std::swprintf(out, cap, L"%.2g", damage);
    return std::max(n, 0);
}

// White for a hit, red for the hit that killed.
constexpr uint32_t kHitColor = 0xfff5f7fa, kKillColor = 0xffff3b30;
inline uint32_t damage_color(bool kill) { return kill ? kKillColor : kHitColor; }

// Targets you hit while they were still up, watched for kKillWindow seconds. The caller polls each one's
// "incapacitated" flag and reports it through check(); a target that goes down inside the window is your kill.
// A target that was already down when hit (a corpse) is never watched.
struct KillWatch {
    struct Entry { int32_t actor{-1}; double hit{}; bool used{}; };
    Entry items[kMaxWatched];
    void hit(int32_t actor, double now) {
        if (actor < 0) return;
        Entry* slot = nullptr;
        for (auto& e : items) if (e.used && e.actor == actor) { slot = &e; break; }
        if (!slot) for (auto& e : items) if (!e.used) { slot = &e; break; }
        if (!slot) { slot = &items[0]; for (auto& e : items) if (e.hit < slot->hit) slot = &e; }
        *slot = Entry{actor, now, true};
    }
    // down: whether the actor is incapacitated now (false when it can't be found). Returns true once, on the kill.
    bool check(int32_t actor, bool down, double now) {
        for (auto& e : items) {
            if (!e.used || e.actor != actor) continue;
            if (down) { e.used = false; return now - e.hit <= kKillWindow; }
            if (now - e.hit > kKillWindow) e.used = false;
            return false;
        }
        return false;
    }
    void expire(double now) { for (auto& e : items) if (e.used && now - e.hit > kKillWindow) e.used = false; }
    int count() const { int c = 0; for (auto& e : items) c += e.used; return c; }
};

}  // namespace anycombat
