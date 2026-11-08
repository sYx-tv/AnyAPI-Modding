#pragma once
// AnyLaser pure logic: the two part definitions, their data channels, the beam and sensor maths and the beam's
// screen geometry. No game or Windows dependencies, so tests/anylaser_logic_test.cpp runs it on any platform.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace anylaser {

constexpr const char* kLaserId = "anylaser_laser";
constexpr const char* kSensorId = "anylaser_sensor";
constexpr const char* kStockMeshPath = "meshes/components/compass_sensor_a.mesh";

// Geometry. A part cell is 0.08 m and cell 0 spans -0.04..0.04 (turbo_logic.h). Both parts are one cell and face
// +Z, the axis the GPS Sensor's arrow showed forward in game.
constexpr double kCell = 0.08;
constexpr double kHalf = kCell / 2;
constexpr double kStartOffset = kHalf + 0.004;   // the beam starts just past the lens, outside the laser's own cell
constexpr double kRange = 100.0;                 // metres
constexpr double kSensorRadius = 0.065;          // a beam end this close to a sensor's centre lands on it (cell half-diagonal 0.069)
constexpr double kLitHold = 0.15;                // seconds a sensor stays lit after the last beam on it (covers tick order)
constexpr double kBeamHold = 0.3;                // seconds a beam keeps drawing after its last server tick

enum class Kind { None, Laser, Sensor };

// Data channels. Outputs come first; an input is written by the game into the slot get_data_f64 hands out.
struct Channel { const char* name; bool input; const wchar_t* help; };
enum LaserSlot { kLaserHit, kLaserDistance, kLaserOff, kLaserSlots };
enum SensorSlot { kSensorLit, kSensorBroken, kSensorDistance, kSensorSlots };
inline constexpr Channel kLaserChannels[kLaserSlots] = {
    {"hit", false, L"1 when the beam lands on something, else 0"},
    {"distance", false, L"Metres from the lens to what the beam hits (0 when nothing is hit)"},
    {"off", true, L"Set to 1 to switch the laser off; unconnected = on"},
};
inline constexpr Channel kSensorChannels[kSensorSlots] = {
    {"lit", false, L"1 while a laser beam is on the sensor, else 0"},
    {"broken", false, L"1 when no beam reaches the sensor (the beam is blocked or off), else 0"},
    {"distance", false, L"Metres back to the laser lighting it (0 when not lit)"},
};

inline int channel_index(Kind kind, const char* name, size_t length) {
    const Channel* table = kind == Kind::Laser ? kLaserChannels : kSensorChannels;
    int count = kind == Kind::Laser ? int(kLaserSlots) : kind == Kind::Sensor ? int(kSensorSlots) : 0;
    for (int i = 0; i < count; ++i) {
        const char* n = table[i].name;
        size_t l = std::char_traits<char>::length(n);
        if (l == length && std::char_traits<char>::compare(n, name, l) == 0) return i;
    }
    return -1;
}

inline Kind kind_of(const char* id, size_t length) {
    auto is = [&](const char* s) { size_t l = std::char_traits<char>::length(s); return l == length && std::char_traits<char>::compare(s, id, l) == 0; };
    return is(kLaserId) ? Kind::Laser : is(kSensorId) ? Kind::Sensor : Kind::None;
}

// One part definition: a copy of the stock compass sensor's class (so it ticks and serves data channels) with our
// channels, one cell, data ports on every face but the lens (+Z, dir 5). dir = axis * 2 + (positive side): 0 -x,
// 1 +x, 2 -y, 3 +y, 4 -z, 5 +z (turbo_logic.h). tech_tier 3 and the sensor category, like the GPS Sensor.
inline std::string definition(Kind kind, const std::string& mesh_path) {
    bool laser = kind == Kind::Laser;
    const Channel* table = laser ? kLaserChannels : kSensorChannels;
    int count = laser ? int(kLaserSlots) : int(kSensorSlots);
    std::string descriptors = "{\"type\": \"type_f64_output\", \"name\": \"north_angle\"}";   // the class's own channel
    for (int i = 0; i < count; ++i)
        descriptors += std::string(",\n        {\"type\": \"") + (table[i].input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + table[i].name + "\"}";
    std::string id = laser ? kLaserId : kSensorId;
    std::string name = laser ? "Laser Block" : "Laser Sensor";
    std::string text = laser ? "Shoots a laser beam out of its lens. Outputs hit and distance; input off switches it off. Connect with data port."
                             : "Lights up when a Laser Block's beam lands on its lens. Outputs lit, broken and distance. Connect with data port.";
    return std::string(R"(    {
      "id": ")") + id + R"(",
      "name": ")" + name + R"(",
      "description": ")" + text + R"(",
      "class": "compass_sensor",
      "zones": [{"bounds_max": [0, 0, 0]}],
      "interaction_zones": [],
      "surfaces": [
        {"dir": 0, "gender": 2, "type": "port"},
        {"dir": 1, "gender": 2, "type": "port"},
        {"dir": 2, "gender": 2, "type": "port"},
        {"dir": 3, "gender": 2, "type": "port"},
        {"dir": 4, "gender": 2, "type": "port"}
      ],
      "logic_nodes": [],
      "mesh_static": {"mesh_path": ")" + mesh_path + R"("},
      "meshes_dynamic": [],
      "category": "sensor",
      "tags_generated": "port,",
      "inspection_geometry": [],
      "mateable_port_types": 2,
      "data_descriptors": [
        )" + descriptors + R"(
      ],
      "tech_tier": 3,
      "spawn_probability": 0.2,
      "loot_group": "mechanical",
      "constraint_orientations": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],
      "force_emitter_element_rotor": {"blades": []},
      "liquid_pump_wattage": 1000.0,
      "liquid_pump_max_output_rps": 50.0,
      "gas_pump_wattage": 1000.0,
      "gas_pump_max_output_rps": 50.0
    })";
}
inline std::string definitions_json(const std::string& laser_mesh = kStockMeshPath, const std::string& sensor_mesh = kStockMeshPath) {
    return "{\n  \"definitions\": [\n" + definition(Kind::Laser, laser_mesh) + ",\n" + definition(Kind::Sensor, sensor_mesh) + "\n  ]\n}\n";
}

// ------------------------------------------------------------------ maths
struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// mat34 as the game stores it: columns right, up, forward, translation (12 doubles).
struct Mat34 { double m[12]{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; };
inline Vec3 column(const Mat34& a, int c) { return {a.m[c * 3], a.m[c * 3 + 1], a.m[c * 3 + 2]}; }
inline Vec3 apply_rotation(const Mat34& a, Vec3 v) { return column(a, 0) * v.x + column(a, 1) * v.y + column(a, 2) * v.z; }
inline Mat34 compose(const Mat34& outer, const Mat34& inner) {   // outer * inner
    Mat34 r;
    for (int c = 0; c < 3; ++c) { Vec3 v = apply_rotation(outer, column(inner, c)); r.m[c * 3] = v.x; r.m[c * 3 + 1] = v.y; r.m[c * 3 + 2] = v.z; }
    Vec3 t = apply_rotation(outer, column(inner, 3)) + column(outer, 3);
    r.m[9] = t.x; r.m[10] = t.y; r.m[11] = t.z;
    return r;
}
inline bool finite(const Mat34& a) { for (double v : a.m) if (!std::isfinite(v)) return false; return true; }

// A part's centre and facing, from its world transform.
struct Pose { Vec3 centre, forward; };
inline bool pose_of(const Mat34& world, Pose& out) {
    Vec3 f = column(world, 2);
    double l = length(f);
    if (!finite(world) || !(l > 0.5 && l < 2)) return false;
    out.centre = column(world, 3);
    out.forward = f * (1 / l);
    return true;
}

// The beam of one laser this tick.
struct Beam { Vec3 start, end; bool hit = false; double distance = 0; };
inline Beam beam_from(const Pose& laser, double ray_distance) {
    Beam b;
    b.start = laser.centre + laser.forward * kStartOffset;
    double d = std::isfinite(ray_distance) ? std::clamp(ray_distance, 0.0, kRange) : kRange;
    b.hit = d < kRange - 1e-6;
    b.distance = b.hit ? d : 0;
    b.end = b.start + laser.forward * d;
    return b;
}

// Players block the beam too. A player is an upright capsule from its feet `position` (radius 0.3, 1.8 m tall);
// returns the beam distance shortened to the first player it passes through.
inline double block_by_capsule(const Vec3& start, const Vec3& dir, double distance, const Vec3& feet, double radius = 0.3, double height = 1.8) {
    // Sample the segment in 2 cm steps against the capsule's axis; cheap and exact enough for a tripwire.
    const Vec3 a = feet + Vec3{0, radius, 0}, b = feet + Vec3{0, height - radius, 0};
    for (double t = 0; t <= distance; t += 0.02) {
        Vec3 p = start + dir * t;
        double h = std::clamp(p.y - a.y, 0.0, b.y - a.y);
        Vec3 axis{a.x, a.y + h, a.z};
        if (length(p - axis) <= radius) return t;
    }
    return distance;
}

// True when the beam ends on the sensor: its end is within kSensorRadius of the sensor's centre and it comes in
// from the lens side (the sensor faces the laser at any angle up to 90 degrees off).
inline bool lands_on(const Beam& beam, const Pose& sensor) {
    if (!beam.hit) return false;
    Vec3 dir = beam.end - beam.start;
    double l = length(dir);
    if (l <= 0) return false;
    return length(beam.end - sensor.centre) <= kSensorRadius && dot(sensor.forward, dir * (1 / l)) < 0;
}

// Per-part state. Slots are what get_data_f64 points the game at, so a part's entry is never erased.
struct Part {
    Kind kind = Kind::None;
    Pose pose;
    double seen = -1e9;          // last server tick, seconds
    double lit_until = -1e9;     // sensors: lit while now < lit_until
    double lit_distance = 0;
    double slots[4]{};
};

inline void update_laser(Part& p, const Beam& b) {
    p.slots[kLaserHit] = b.hit ? 1 : 0;
    p.slots[kLaserDistance] = b.distance;
}
inline void light_sensor(Part& s, const Beam& b, double now) {
    double d = b.distance + kStartOffset;   // from the laser's centre: what a player measures block to block
    if (now >= s.lit_until || d < s.lit_distance) s.lit_distance = d;   // two lasers on one sensor: the nearer wins
    s.lit_until = now + kLitHold;
}
inline void update_sensor(Part& s, double now) {
    bool lit = now < s.lit_until;
    s.slots[kSensorLit] = lit ? 1 : 0;
    s.slots[kSensorBroken] = lit ? 0 : 1;
    s.slots[kSensorDistance] = lit ? s.lit_distance : 0;
}

// ------------------------------------------------------------------ beam looks
// The three looks shown to Nate (2026-11-08). kLook picks the shipped one.
struct Look { uint32_t core, glow; double core_m, glow_m; float min_core_px, min_glow_px; };
inline constexpr Look kLooks[3] = {
    {0xffff6a6a, 0x66ff1e1e, 0.003, 0.018, 1.2f, 4.f},   // 1 tripwire: thin red
    {0xffb4ffc0, 0x5a3cff5a, 0.005, 0.040, 1.6f, 7.f},   // 2 industrial: green glow
    {0xffd6f0ff, 0x6050aaff, 0.008, 0.060, 2.0f, 9.f},   // 3 sci-fi: thick blue
};

// ------------------------------------------------------------------ screen projection (AnyCombat's camera)
struct Camera {
    Vec3 position, forward, up, right;
    double tan_half_fov = 0, near_plane = 0.05;
    bool valid() const {
        return std::isfinite(tan_half_fov) && tan_half_fov > 0.01 && tan_half_fov < 20 &&
               std::fabs(dot(forward, forward) - 1) < 0.05 && std::fabs(dot(up, up) - 1) < 0.05 && std::fabs(dot(right, right) - 1) < 0.05;
    }
};
struct ScreenPoint { double x = 0, y = 0, depth = 0; };
// Clips the segment to the camera's near plane and projects both ends to pixels. False when it is all behind.
inline bool project_segment(const Camera& c, Vec3 a, Vec3 b, double width, double height, ScreenPoint& pa, ScreenPoint& pb) {
    if (!c.valid() || !(width > 0) || !(height > 0)) return false;
    double nearz = std::max(c.near_plane, 0.02) + 0.01;
    double za = dot(a - c.position, c.forward), zb = dot(b - c.position, c.forward);
    if (za < nearz && zb < nearz) return false;
    if (za < nearz) { a = a + (b - a) * ((nearz - za) / (zb - za)); za = nearz; }
    if (zb < nearz) { b = b + (a - b) * ((nearz - zb) / (za - zb)); zb = nearz; }
    double aspect = width / height;
    auto proj = [&](Vec3 p, double z, ScreenPoint& out) {
        Vec3 d = p - c.position;
        double nx = dot(d, c.right) / (z * c.tan_half_fov * aspect), ny = dot(d, c.up) / (z * c.tan_half_fov);
        out = {(nx + 1) * 0.5 * width, (1 - ny) * 0.5 * height, z};
        return std::isfinite(out.x) && std::isfinite(out.y);
    };
    return proj(a, za, pa) && proj(b, zb, pb);
}
// Pixels a world width covers at a depth.
inline double pixels(const Camera& c, double world, double depth, double height) { return world * height / (2 * depth * c.tan_half_fov); }

// A screen quad for a beam layer: half widths at each end, perpendicular to the beam on screen.
struct Quad { float x[4], y[4]; };
inline bool beam_quad(const ScreenPoint& a, const ScreenPoint& b, double ha, double hb, Quad& q) {
    double dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
    if (!(l > 0.5)) return false;
    double nx = -dy / l, ny = dx / l;
    q.x[0] = float(a.x + nx * ha); q.y[0] = float(a.y + ny * ha);
    q.x[1] = float(b.x + nx * hb); q.y[1] = float(b.y + ny * hb);
    q.x[2] = float(b.x - nx * hb); q.y[2] = float(b.y - ny * hb);
    q.x[3] = float(a.x - nx * ha); q.y[3] = float(a.y - ny * ha);
    return true;
}

}  // namespace anylaser
