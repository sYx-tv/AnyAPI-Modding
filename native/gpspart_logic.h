#pragma once
// GpsPart pure logic: data channel table, the GPS sensor definition and the per-sensor maths.
// No game or Windows dependencies, so tests/gpspart_logic_test.cpp runs it on any platform.
#include <cmath>
#include <cstdint>
#include <string>

namespace gpspart {

constexpr const char* kDefinitionId = "gps_sensor";
constexpr const char* kStockMeshPath = "meshes/components/compass_sensor_a.mesh";
// The marked mesh (gpspart_mesh.h) is written beside the stock one under rom/.
constexpr const char* kGpsMeshPath = "meshes/components/gpspart_gps_sensor_a.mesh";
constexpr double kPi = 3.14159265358979323846;
constexpr double kMsToKmh = 3.6;

// Data channels. Outputs are written by the host every tick; inputs are written by data links and
// microcontrollers into the same slots (the game reads and writes through the pointer get_data_f64 returns).
enum Channel {
    kX, kY, kZ, kAltitude,
    kHeading, kPitch, kRoll,
    kSpeedMs, kSpeedKmh, kGroundSpeedMs, kGroundSpeedKmh, kVerticalSpeedMs, kVerticalSpeedKmh,
    kAccelMs2, kAccelKmhS,
    kWaypointDistance, kWaypointHorizontalDistance, kWaypointBearing, kWaypointRelativeBearing, kWaypointHeightDifference,
    kWaypointX, kWaypointY, kWaypointZ,
    kChannelCount
};
constexpr int kFirstInput = kWaypointX;
struct ChannelInfo { const char* name; bool input; const wchar_t* help; };
inline constexpr ChannelInfo kChannels[kChannelCount] = {
    {"x", false, L"World position of the sensor along the map's x axis, in metres"},
    {"y", false, L"Height of the sensor, in metres (same as altitude)"},
    {"z", false, L"World position of the sensor along the map's z axis, in metres"},
    {"altitude", false, L"Height of the sensor, in metres"},
    {"heading", false, L"Compass direction the yellow arrow points, 0-360\u00b0"},
    {"pitch", false, L"Nose up (+) or down (-) along the arrow, -90 to 90\u00b0"},
    {"roll", false, L"Bank to the right (+) or left (-), -90 to 90\u00b0"},
    {"speed_ms", false, L"Speed in any direction, in m/s"},
    {"speed_kmh", false, L"Speed in any direction, in km/h"},
    {"ground_speed_ms", false, L"Speed along the ground, ignoring climb, in m/s"},
    {"ground_speed_kmh", false, L"Speed along the ground, ignoring climb, in km/h"},
    {"vertical_speed_ms", false, L"Climbing (+) or falling (-) speed, in m/s"},
    {"vertical_speed_kmh", false, L"Climbing (+) or falling (-) speed, in km/h"},
    {"acceleration_ms2", false, L"Speeding up (+) or slowing down (-), in m/s per second"},
    {"acceleration_kmh_s", false, L"Speeding up (+) or slowing down (-), in km/h per second"},
    {"waypoint_distance", false, L"Straight-line distance to the waypoint, in metres"},
    {"waypoint_horizontal_distance", false, L"Distance to the waypoint ignoring height, in metres"},
    {"waypoint_bearing", false, L"Compass direction to the waypoint, 0-360\u00b0"},
    {"waypoint_relative_bearing", false, L"Turn needed to face the waypoint, -180 to 180\u00b0 (+ is the way heading counts up)"},
    {"waypoint_height_difference", false, L"How far the waypoint is above (+) or below (-) you, in metres"},
    {"waypoint_x", true, L"Input: set the waypoint's x (copy x from a GPS at the spot)"},
    {"waypoint_y", true, L"Input: set the waypoint's height"},
    {"waypoint_z", true, L"Input: set the waypoint's z"},
};
constexpr const wchar_t* kNorthAngleHelp = L"Stock compass output: angle to north";
inline int channel_index(const char* name, size_t length) {
    for (int i = 0; i < kChannelCount; ++i) {
        const char* n = kChannels[i].name;
        if (std::char_traits<char>::length(n) == length && std::char_traits<char>::compare(n, name, length) == 0) return i;
    }
    return -1;
}

// The new part: a copy of the stock compass sensor (same class, mesh, ports and size) with the GPS channels.
// north_angle stays first so the stock compass behaviour keeps working on the same class.
// mesh_path: the marked GPS mesh when it was built, else the stock compass mesh.
// tech_tier 4: world loot from the moment the 3rd bunker's end sequence starts (loot level 1 + 3 bunkers). category "sensor": the sandbox/creative sensor
// container. Edited by hand only together with the in-game test, see docs/mods/gps.md.
inline std::string definition_json(const std::string& mesh_path = kStockMeshPath) {
    std::string descriptors = "{\"type\": \"type_f64_output\", \"name\": \"north_angle\"}";
    for (const auto& c : kChannels)
        descriptors += std::string(",\n        {\"type\": \"") + (c.input ? "type_f64_input" : "type_f64_output") + "\", \"name\": \"" + c.name + "\"}";
    return std::string(R"({
  "definitions": [
    {
      "id": "gps_sensor",
      "name": "GPS Sensor",
      "description": "Outputs position, heading, pitch, roll, speed, acceleration and waypoint distance and bearing. Connect with data port.",
      "class": "compass_sensor",
      "zones": [{"bounds_max": [0, 1, 0]}],
      "interaction_zones": [],
      "surfaces": [
        {"dir": 2, "gender": 2, "type": "port"},
        {"gender": 2, "type": "port"},
        {"dir": 1, "gender": 2, "type": "port"},
        {"dir": 4, "gender": 2, "type": "port"},
        {"dir": 5, "gender": 2, "type": "port"}
      ],
      "logic_nodes": [],
      "mesh_static": {"mesh_path": ")" + mesh_path + R"("},
      "meshes_dynamic": [],
      "category": "sensor",
      "tags_generated": "port,",
      "inspection_geometry": [],
      "mateable_port_types": 2,
      "data_descriptors": [
        )") + descriptors + R"(
      ],
      "tech_tier": 4,
      "spawn_probability": 0.2,
      "loot_group": "mechanical",
      "constraint_orientations": [[1, 0, 0, 0, 1, 0, 0, 0, 1]],
      "force_emitter_element_rotor": {"blades": []},
      "liquid_pump_wattage": 1000.0,
      "liquid_pump_max_output_rps": 50.0,
      "gas_pump_wattage": 1000.0,
      "gas_pump_max_output_rps": 50.0
    }
  ]
}
)";
}

struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline double deg(double r) { return r * 180.0 / kPi; }
inline double wrap360(double d) { d = std::fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }
inline double wrap180(double d) { d = wrap360(d); return d > 180.0 ? d - 360.0 : d; }

// Native mat34: three basis columns then the translation, 12 doubles (see balance_math.h).
struct Mat34 { double m[12]{}; };
inline Vec3 column(const Mat34& a, int c) { return {a.m[c * 3], a.m[c * 3 + 1], a.m[c * 3 + 2]}; }
inline Vec3 apply_rotation(const Mat34& a, Vec3 p) { return {a.m[0] * p.x + a.m[3] * p.y + a.m[6] * p.z, a.m[1] * p.x + a.m[4] * p.y + a.m[7] * p.z, a.m[2] * p.x + a.m[5] * p.y + a.m[8] * p.z}; }
inline Mat34 compose(const Mat34& outer, const Mat34& inner) {   // outer * inner
    Mat34 r;
    for (int c = 0; c < 3; ++c) { Vec3 v = apply_rotation(outer, column(inner, c)); r.m[c * 3] = v.x; r.m[c * 3 + 1] = v.y; r.m[c * 3 + 2] = v.z; }
    Vec3 t = apply_rotation(outer, column(inner, 3)) + column(outer, 3);
    r.m[9] = t.x; r.m[10] = t.y; r.m[11] = t.z;
    return r;
}
inline bool finite(const Mat34& a) { for (double v : a.m) if (!std::isfinite(v) || std::abs(v) > 1e9) return false; return true; }

// Y is up and +Z is the part's forward axis (same convention as the player heading in anyapi_player_heading.h).
inline double yaw_of(Vec3 v) { return deg(std::atan2(v.x, v.z)); }

struct Sensor {
    double slots[kChannelCount]{};
    bool started = false;
    Vec3 last_position, velocity;
    double last_time = 0, last_speed = 0;
    // Direction convention of the game's north_angle against yaw_of(): heading = sign * yaw + offset.
    double sign = 1, last_yaw = 0, last_north = 0;
    bool have_last_angles = false, sign_known = false;
};

// One host tick. world: the part's world transform. north: the game's own north_angle for this part.
// now: seconds. Velocity is a smoothed finite difference of the part's position.
inline void tick(Sensor& s, const Mat34& world, double north, double now) {
    Vec3 pos = column(world, 3), fwd = column(world, 2), right = column(world, 0);
    if (!s.started) {
        s.started = true; s.last_position = pos; s.last_time = now;
    }
    // Raw world coordinates of the part itself (Nate, 2026-10-10). Waypoints use the same coordinates.
    s.slots[kX] = pos.x; s.slots[kY] = pos.y; s.slots[kZ] = pos.z;
    s.slots[kAltitude] = pos.y;

    double heading = wrap360(north);
    double yaw = yaw_of(fwd);
    if (s.have_last_angles && !s.sign_known) {
        double dy = wrap180(yaw - s.last_yaw), dh = wrap180(heading - s.last_north);
        if (std::abs(dy) > 2 && std::abs(dh) > 2) { s.sign = (dy > 0) == (dh > 0) ? 1 : -1; s.sign_known = true; }
    }
    s.have_last_angles = true; s.last_yaw = yaw; s.last_north = heading;
    s.slots[kHeading] = heading;
    double fl = length(fwd), rl = length(right);
    s.slots[kPitch] = fl > 1e-9 ? deg(std::asin(std::fmax(-1.0, std::fmin(1.0, fwd.y / fl)))) : 0;
    s.slots[kRoll] = rl > 1e-9 ? deg(std::asin(std::fmax(-1.0, std::fmin(1.0, right.y / rl)))) : 0;

    double dt = now - s.last_time;
    if (dt > 1e-4) {
        Vec3 v = (pos - s.last_position) * (1.0 / dt);
        if (dt > 1.0) v = {};                                // a pause or teleport: start over
        double a = std::fmin(1.0, dt / 0.25);                // ~0.25 s smoothing
        s.velocity = s.velocity * (1 - a) + v * a;
        double speed = length(s.velocity);
        double accel = dt > 1.0 ? 0 : (speed - s.last_speed) / dt;
        s.slots[kAccelMs2] = s.slots[kAccelMs2] * (1 - a) + accel * a;
        s.last_speed = speed;
        s.last_position = pos; s.last_time = now;
    }
    double speed = length(s.velocity), ground = std::hypot(s.velocity.x, s.velocity.z);
    s.slots[kSpeedMs] = speed; s.slots[kSpeedKmh] = speed * kMsToKmh;
    s.slots[kGroundSpeedMs] = ground; s.slots[kGroundSpeedKmh] = ground * kMsToKmh;
    s.slots[kVerticalSpeedMs] = s.velocity.y; s.slots[kVerticalSpeedKmh] = s.velocity.y * kMsToKmh;
    s.slots[kAccelKmhS] = s.slots[kAccelMs2] * kMsToKmh;

    Vec3 target{s.slots[kWaypointX], s.slots[kWaypointY], s.slots[kWaypointZ]};
    Vec3 to = target - pos;
    double horizontal = std::hypot(to.x, to.z);
    s.slots[kWaypointDistance] = length(to);
    s.slots[kWaypointHorizontalDistance] = horizontal;
    s.slots[kWaypointHeightDifference] = to.y;
    if (horizontal > 1e-6) {
        double relative = s.sign * wrap180(yaw_of(to) - yaw);
        s.slots[kWaypointRelativeBearing] = wrap180(relative);
        s.slots[kWaypointBearing] = wrap360(heading + relative);
    } else {
        s.slots[kWaypointRelativeBearing] = 0; s.slots[kWaypointBearing] = heading;
    }
}

}  // namespace gpspart
