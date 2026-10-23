#pragma once
// AnyMoto: motorcycle parts for the stock Motorcycle Wheel item. Pure logic, no game calls (tested by
// tests/anymoto_logic_test.cpp).
//
// Game facts this rests on (Anymaker 0.1.27, game.gcl, static):
//   - The stock "wheel" part IS the suspension: the spring is a raycast inside vehicle_element.wheel, not a separate
//     body, and the tyre is an item that clips onto it. So a fork and a swingarm are wheel-class parts with their
//     own code-side tuning (vehicle_element_definition.wheel, 328 bytes): no joints, and drive from the torque face
//     stays on one body.
//   - The Motorcycle Wheel item (wheel_motorcycle) exists as loot but no stock part accepts it, and the tyre size
//     table (g_tyre_data_container) has no entry for it. The wheel uses max(tuning m_radius, table radius), so the
//     tuning's m_radius carries its size when the table has none.
//   - Surface dirs: 0 = -x, 1 = +x, 2 = -y, 3 = +y, 4 = -z, 5 = +z (from the stock seat). A part grows along +z from
//     the face it is placed on; y is up. Cells are 0.08 m, cell 0 centred on the origin.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <cstring>
#include <string>

namespace anymoto {

constexpr const char* kForkId = "moto_fork";
constexpr const char* kSwingarmId = "moto_swingarm";
constexpr const char* kLeanAssistId = "moto_lean_assist";
constexpr const char* kSeatId = "moto_seat";
constexpr const char* kHandlebarsId = "moto_handlebars";
constexpr const char* kMotorcycleTyre = "wheel_motorcycle";

enum class Kind { other, fork, swingarm, lean_assist, seat, handlebars };
inline Kind kind_of_id(const char* id, size_t n) {
    auto is = [&](const char* s) { return n == std::strlen(s) && std::memcmp(id, s, n) == 0; };
    if (!id || !n) return Kind::other;
    if (is(kForkId)) return Kind::fork;
    if (is(kSwingarmId)) return Kind::swingarm;
    if (is(kLeanAssistId)) return Kind::lean_assist;
    if (is(kSeatId)) return Kind::seat;
    if (is(kHandlebarsId)) return Kind::handlebars;
    return Kind::other;
}

// ------------------------------------------------------------------------------------------- vectors
struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a) { double l = length(a); return l > 1e-12 ? a * (1 / l) : Vec3{}; }

// mat34 as the game stores it: three rotation columns, then the translation (12 doubles, 96 bytes).
struct Mat34 { double m[12]{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; };
inline Vec3 column(const Mat34& a, int c) { return {a.m[c * 3], a.m[c * 3 + 1], a.m[c * 3 + 2]}; }
inline void set_column(Mat34& a, int c, Vec3 v) { a.m[c * 3] = v.x; a.m[c * 3 + 1] = v.y; a.m[c * 3 + 2] = v.z; }
inline Vec3 rotate(const Mat34& a, Vec3 p) { return column(a, 0) * p.x + column(a, 1) * p.y + column(a, 2) * p.z; }
inline Vec3 apply(const Mat34& a, Vec3 p) { return rotate(a, p) + column(a, 3); }
inline Mat34 compose(const Mat34& outer, const Mat34& inner) {   // outer * inner
    Mat34 r;
    for (int c = 0; c < 3; ++c) set_column(r, c, rotate(outer, column(inner, c)));
    set_column(r, 3, apply(outer, column(inner, 3)));
    return r;
}
inline Mat34 translation(Vec3 t) { Mat34 r; set_column(r, 3, t); return r; }
inline Mat34 rotation_x(double a) { Mat34 r; double c = std::cos(a), s = std::sin(a); set_column(r, 1, {0, c, s}); set_column(r, 2, {0, -s, c}); return r; }
inline Mat34 rotation_y(double a) { Mat34 r; double c = std::cos(a), s = std::sin(a); set_column(r, 0, {c, 0, -s}); set_column(r, 2, {s, 0, c}); return r; }
// A frame at `from` whose +z runs to `to` and is scaled to its length (x and y keep unit length).
inline Mat34 stretch_between(Vec3 from, Vec3 to, Vec3 up_hint = {1, 0, 0}) {
    Vec3 d = to - from;
    double len = length(d);
    Vec3 z = len > 1e-9 ? d * (1 / len) : Vec3{0, 0, 1};
    Vec3 x = normalized(cross(up_hint, z));
    if (length(x) < 0.5) x = normalized(cross(Vec3{0, 1, 0}, z));
    Vec3 y = cross(z, x);
    Mat34 r;
    set_column(r, 0, x); set_column(r, 1, y); set_column(r, 2, z * len); set_column(r, 3, from);
    return r;
}
inline bool finite(const Mat34& a) { for (double v : a.m) if (!std::isfinite(v) || std::abs(v) > 1e9) return false; return true; }
// The steering angle of a rotation_y matrix (client get_suspension_steering_transform).
inline double steering_angle_of(const Mat34& a) { return std::atan2(a.m[6], a.m[8]); }

// mat33_s32 (the tuning's m_rotation, 9 x s32, columns like mat34). Turns the tyre axle a quarter turn about y:
// the stock wheel's axle runs along z (out of the face it sits on); a motorcycle's runs across, along x.
inline void quarter_turn_y(const int32_t in[9], int32_t out[9]) {
    // Ry(90): x -> -z, y -> y, z -> x.
    static const int32_t r[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) {
            int64_t v = 0;
            for (int k = 0; k < 3; ++k) v += int64_t(r[k * 3 + row]) * in[c * 3 + k];
            out[c * 3 + row] = int32_t(v);
        }
}

// ------------------------------------------------------------------------------------------- wheel tuning
// Where each part holds the tyre, in the part's frame (metres). The fork is placed on the FRONT face of the bike's
// frame (it grows forward), the swingarm on the BACK face (it grows backward). The steering axis runs straight up
// through the tyre centre.
struct WheelTuning {
    Vec3 tyre;                   // m_position and m_steering_position
    double travel;               // m_suspension_length
    double steer_deg;            // +- m_steering_angle_min/max
    double radius;               // m_radius (Motorcycle Wheel: about 0.30 m)
};
constexpr double kMotorcycleTyreRadius = 0.30;
constexpr double kMotorcycleTyreWidth = 0.16;
inline WheelTuning fork_tuning() { return {{0, -0.40, 0.44}, 0.14, 30, kMotorcycleTyreRadius}; }
inline WheelTuning swingarm_tuning() { return {{0, -0.12, 0.52}, 0.12, 0, kMotorcycleTyreRadius}; }
constexpr double kPi = 3.14159265358979323846;
inline double rad(double deg) { return deg * kPi / 180; }

// Fork rake (drawing only). The game moves and steers a wheel along whole axes of the part (its tuning rotation is
// an integer matrix), so the tyre travels straight up and steers about a vertical axis. The fork is drawn raked:
// the legs run from the steering head, set back up the raked steering axis, to wherever the axle is, and the whole
// fork turns about the raked axis. At rest the axle sits on that axis, so nothing is off there; compressed and at
// full lock the fork's axle ends up within about 3 cm of the game's.
constexpr double kForkRakeDeg = 25;
constexpr double kForkLength = 0.75;                     // axle to steering head (top yoke) at rest, along the axis
inline Vec3 fork_head() {
    Vec3 t = fork_tuning().tyre;
    return {t.x, t.y + kForkLength * std::cos(rad(kForkRakeDeg)), t.z - kForkLength * std::sin(rad(kForkRakeDeg))};
}
struct ForkFrames { Mat34 upper, lower; };              // upper: origin at the head, legs down -y; lower: origin at the axle, legs up +y
// axle: the tyre centre in the part frame (the game's x and z stay put; y moves with the suspension).
// steer: the steering angle in radians, as the game turns the tyre about +y.
inline ForkFrames fork_frames(Vec3 axle, double steer) {
    Vec3 h = fork_head();
    Vec3 d = axle - h;
    double leg = length(d);
    double beta = leg > 1e-6 ? std::atan2(-d.z, -d.y) : -rad(kForkRakeDeg);
    double rake = rad(kForkRakeDeg);
    Mat34 turn = compose(compose(rotation_x(-rake), rotation_y(steer)), rotation_x(rake));   // about the raked axis
    Mat34 upper = compose(compose(translation(h), turn), rotation_x(beta));
    return {upper, compose(upper, translation({0, -leg, 0}))};
}

// Swingarm drawing: pivot at the frame face, shock from a bracket above the pivot to 55% along the arm.
constexpr Vec3 kSwingarmPivot{0, 0, 0.04};
constexpr Vec3 kShockTop{0, 0.24, 0.10};
constexpr double kShockAlongArm = 0.55;
inline double swingarm_angle(Vec3 tyre_centre) {   // rotation_x angle that turns the arm's +z onto the tyre
    Vec3 d = tyre_centre - kSwingarmPivot;
    return std::atan2(-d.y, d.z);
}

// Handlebars: the bars turn about a steering axis raked back toward the rider, at the top of a short riser that
// stands on the seat's front control face.
constexpr Vec3 kBarsPivot{0, 0.36, 0.02};
constexpr double kBarsRakeDeg = 25;
constexpr Vec3 kGripRight{0.28, 0.03, -0.07};          // in the bars' frame; the left grip mirrors x
constexpr double kBarsMaxTurnDeg = 35;                  // at full steering input
inline Mat34 bars_frame(double steering) {              // steering -1..1 -> part frame of the bars
    return compose(compose(translation(kBarsPivot), rotation_x(-rad(kBarsRakeDeg))), rotation_y(-steering * rad(kBarsMaxTurnDeg)));
}

// ------------------------------------------------------------------------------------------- lean assist
// Keeps the bike's up axis on the "balanced" direction: gravity plus the turn's centripetal acceleration, so it
// leans into corners like a ridden bike, and only ever along the part's forward axis (it never fights pitch or
// yaw). Works on angular velocity, so it does not depend on the bike's mass.
struct LeanInput {
    Vec3 forward, up;            // the part's +z and +y in the world
    Vec3 angular_velocity;       // rad/s, world
    Vec3 linear_velocity;        // m/s, world
    double strength;             // 0..1 (Properties: Power)
    double dt;                   // s
    double spin_sign = 1;        // +1 when a positive angular velocity about f turns u the way rotation_* does here
    // The roll rate seen from the bike's own motion (this file's convention, rad/s), smoothed; NaN = use the
    // physics' angular velocity instead. With a rider on, the physics' value bounces from tick to tick (the
    // rider is a separate body pulling back on the seat), and damping on it pumps that bounce up.
    double measured_rate = std::numeric_limits<double>::quiet_NaN();
    double gain_scale = 1;       // 0..1: backs off when the physics fights back
    double speed = 0;            // m/s: a moving bike is caught further over before the assist gives up
    // The turn's yaw rate (rad/s about world up), smoothed; NaN = the physics' own. The chassis weaves from tick to
    // tick, and at speed that noise alone swings the balanced lean from one side's limit to the other.
    double yaw_rate = std::numeric_limits<double>::quiet_NaN();
    double turn_lean = 1;        // 0..2 (Properties: Roll sensitivity): how far into a turn it leans, 1 = balanced
};
struct LeanOutput {
    Vec3 angular_velocity;       // what to set
    double lean_deg = 0;         // current lean, + = right side down
    double target_deg = 0;       // balanced lean for this turn
    bool active = false;
    bool settled = false;        // active, but nothing to correct: leave the physics alone
};
constexpr double kGravity = 9.81;
constexpr double kMaxLeanDeg = 45;
constexpr double kGiveUpDeg = 70;  // past this it has fallen over; let it lie
constexpr double kGiveUpMovingDeg = 85;
constexpr double kMaxRollAccel = 80;  // rad/s^2
// How fast the lean (radians, as lean_deg measures it) changes for a roll rate wf about the forward axis, in this
// file's rotation convention. The mod compares it with the lean change it sees to learn the physics' spin_sign.
inline double lean_rate_for(Vec3 forward, Vec3 up, double wf) {
    const Vec3 world_up{0, 1, 0};
    Vec3 f = normalized(forward), u = normalized(up);
    double l = dot(normalized(cross(u, f)), world_up);
    double c = std::sqrt(std::max(1e-6, 1 - l * l));
    return wf * dot(cross(cross(f, u), f), world_up) / c;
}
// A roll set about the centre of mass swings the tyres' contact patches sideways, and the tyres' grip throws it
// back (and scrubs them). Adding this to the linear velocity makes the same change in spin pivot about the contact
// line instead: the velocity of `pivot` stays as it was. The game's point velocity is spin x offset.
inline Vec3 pivot_velocity_change(Vec3 spin_change, Vec3 pivot, Vec3 centre_of_mass) {
    return cross(spin_change, pivot - centre_of_mass) * -1.0;
}
// The yaw rate whose balanced lean is the turn lean for this much steering: full steering leans 35 degrees, fading
// in from 2 to 10 m/s (no lean when crawling).
constexpr double kSteerLeanDeg = 35;
inline double steer_yaw_rate(double steer, double speed) {
    double fade = std::clamp((speed - 2) / 8, 0.0, 1.0);
    double lean = rad(kSteerLeanDeg) * std::clamp(steer, -1.0, 1.0) * fade;
    return kGravity * std::tan(lean) / std::max(speed, 1.0);
}
inline LeanOutput lean_assist(const LeanInput& in) {
    LeanOutput out;
    out.angular_velocity = in.angular_velocity;
    const Vec3 world_up{0, 1, 0};
    Vec3 f = normalized(in.forward), u = normalized(in.up);
    if (length(f) < 0.5 || length(u) < 0.5 || !(in.dt > 0) || !(in.strength > 0)) return out;
    Vec3 side = normalized(cross(u, f));                      // the part's -x: points to the rider's left
    out.lean_deg = std::asin(std::clamp(dot(side, world_up), -1.0, 1.0)) * 180 / kPi;
    // Balanced up: gravity plus the turn's acceleration (omega x v, yaw part only).
    Vec3 yaw = world_up * (std::isfinite(in.yaw_rate) ? in.yaw_rate : dot(in.angular_velocity, world_up));
    Vec3 centripetal = cross(yaw, in.linear_velocity);
    centripetal = (centripetal - world_up * dot(centripetal, world_up)) * std::clamp(in.turn_lean, 0.0, 2.0);
    double max_c = kGravity * std::tan(rad(kMaxLeanDeg));
    double c_len = length(centripetal);
    if (c_len > max_c) centripetal = centripetal * (max_c / c_len);
    Vec3 target_up = normalized(world_up * kGravity + centripetal);
    // The bike's lean measured from target_up is lean - target, so the target is the difference.
    out.target_deg = out.lean_deg - std::asin(std::clamp(dot(side, target_up), -1.0, 1.0)) * 180 / kPi;
    double give_up = in.speed > 3 ? kGiveUpMovingDeg : kGiveUpDeg;
    // On its side or upside down: leave it. Roll only, so a nose-down stop or a slope doesn't let go of the bike.
    if (std::abs(out.lean_deg) > give_up || dot(u, world_up) < 0) return out;
    // Roll error about the forward axis (rotate u onto target_up), and the current roll rate.
    double error = dot(cross(u, target_up), f);
    // The physics may spin the other way round for the same vector: spin_sign maps between them.
    double s = in.spin_sign < 0 ? -1.0 : 1.0;
    double rate = std::isfinite(in.measured_rate) ? in.measured_rate : s * dot(in.angular_velocity, f);
    // Stiff enough to beat gravity's tip-over (about g / height = 16 /s^2 for a bike's mass 0.6 m up) with room
    // to spare at the default power, critically damped at every strength.
    double k = 120 * in.strength * std::clamp(in.gain_scale, 0.0, 1.0), c = 2 * std::sqrt(k);
    double dt = std::min(in.dt, 0.05);
    // Parked and settled: leave it be. Nudging an upright bike every tick scrubs its tyres sideways (dust).
    if (in.speed < 0.5 && std::abs(error) < rad(1.0) && std::abs(rate) < 0.15) { out.active = true; out.settled = true; return out; }
    double delta = (k * error - c * rate) * dt;
    delta = std::clamp(delta, -kMaxRollAccel * dt, kMaxRollAccel * dt);
    out.angular_velocity = in.angular_velocity + f * (s * delta);
    out.active = true;
    return out;
}

// ------------------------------------------------------------------------------------------- definitions
struct MeshPaths {
    std::string fork_base, fork_steer, fork_lower, swingarm_base, swingarm_arm, swingarm_shock, hidden;
    std::string lean_assist, seat, handlebars_base, handlebars_bars;
};

inline std::string dyn(const std::string& path, Vec3 pos, const Mat34* rot = nullptr) {
    char b[64];
    std::snprintf(b, sizeof b, "%.3f, %.3f, %.3f", pos.x, pos.y, pos.z);
    std::string s = "{\"path\": \"" + path + "\", \"pos\": [" + b + "]";
    if (rot) {
        s += ", \"preview_rot\": [";
        for (int i = 0; i < 9; ++i) { std::snprintf(b, sizeof b, "%s%.6f", i ? ", " : "", rot->m[i]); s += b; }
        s += "]";
    }
    return s + "}";
}
// Twelve moving meshes, like the stock wheel: other game code may ask a wheel for any of them. Ours use the first
// three; the rest are a hidden speck inside the head.
inline std::string wheel_meshes(const std::string& a, Vec3 pa, const std::string& b, Vec3 pb, const std::string& c, Vec3 pc, const std::string& hidden) {
    std::string s = dyn(a, pa) + ", " + dyn(b, pb) + ", " + dyn(c, pc);
    for (int i = 3; i < 12; ++i) s += ", " + dyn(hidden, {0, 0, 0});
    return s;
}

inline std::string definitions_json(const MeshPaths& m) {
    WheelTuning f = fork_tuning();
    ForkFrames rest = fork_frames(f.tyre, 0);
    std::string fork_meshes = dyn(m.fork_steer, fork_head(), &rest.upper) + ", " + dyn(m.fork_lower, f.tyre, &rest.lower);
    for (int i = 2; i < 12; ++i) fork_meshes += ", " + dyn(m.hidden, {0, 0, 0});
    std::string wheel_common = R"("class": "wheel", "category": "wheel", "tags_generated": "interactable,mechanical,port,torque,", "inspection_geometry": [], "mateable_port_types": 2, "data_descriptors": [{"type": "type_f64_output", "name": "brake_factor"}, {"type": "type_f64_output", "name": "rps"}], "tech_tier": 2, "spawn_probability": 0.3, "loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []})";
    std::string fork = std::string(R"({"id": ")") + kForkId + R"(", "name": "Motorcycle Fork", "description": "Fits the Motorcycle Wheel. Place it on the front face of the bike's frame. Links on the neck behind the steering head: a steering link on each side (one turns it left, the other right) and the brake link above the left one. 14 cm of travel.", )" + wheel_common +
        R"(, "zones": [{"bounds_min": [-1, -1, 0], "bounds_max": [1, 1, 1]}, {"bounds_min": [-1, 1, 1], "bounds_max": [1, 3, 2]}, {"bounds_min": [-2, 2, 2], "bounds_max": [2, 3, 2]}, {"bounds_min": [-2, -1, 5], "bounds_max": [2, 0, 6]}, {"bounds_min": [-2, -4, 5], "bounds_max": [-2, -3, 5]}, {"bounds_min": [-2, -2, 4], "bounds_max": [-2, -1, 4]}, {"bounds_min": [-2, 0, 3], "bounds_max": [-2, 1, 3]}, {"bounds_min": [2, -4, 5], "bounds_max": [2, -3, 5]}, {"bounds_min": [2, -2, 4], "bounds_max": [2, -1, 4]}, {"bounds_min": [2, 0, 3], "bounds_max": [2, 1, 3]}], )"
        R"("interaction_zones": [{"bounds_min": [-0.1, -0.5, 0.34], "bounds_max": [0.1, -0.3, 0.54]}], )"
        R"("surfaces": [{"id": "torque", "dir": 4, "type": "torque"}, {"pos": [0, 2, 1], "gender": 2, "type": "mechanical"}, {"pos": [0, 2, 1], "dir": 1, "gender": 2, "type": "mechanical"}, {"pos": [0, 3, 1], "gender": 2, "type": "mechanical"}, {"pos": [0, -1, 0], "dir": 4, "gender": 2, "type": "port"}], )"
        R"("logic_nodes": [{"id": "steering_pos", "pos": [0, 2, 1], "direction": 0, "type": "mechanical_in"}, {"id": "steering_neg", "pos": [0, 2, 1], "direction": 1, "type": "mechanical_in"}, {"id": "brake", "pos": [0, 3, 1], "direction": 0, "type": "mechanical_in"}], )"
        R"("mesh_static": {"mesh_path": ")" + m.fork_base + R"("}, "meshes_dynamic": [)" +
        fork_meshes +
        R"(], "bounds_override_min": [-3, -10, 0], "bounds_override_max": [3, 5, 10]})";
    std::string swing = std::string(R"({"id": ")") + kSwingarmId + R"(", "name": "Motorcycle Swingarm", "description": "Fits the Motorcycle Wheel. Place it on the back face of the bike's frame. Drive comes in through the torque face (the hex nut) on the left end of the pivot block, seen from the seat: put a pulley wheel there and belt it to the engine. Brake link on the right side of the shock bracket. 12 cm of travel.", )" + wheel_common +
        R"(, "zones": [{"bounds_min": [-1, -1, 0], "bounds_max": [1, 1, 1]}, {"bounds_min": [0, 2, 0], "bounds_max": [0, 3, 1]}, {"bounds_min": [-2, -1, 2], "bounds_max": [-2, 0, 5]}, {"bounds_min": [2, -1, 2], "bounds_max": [2, 0, 5]}], )"
        R"("interaction_zones": [{"bounds_min": [-0.1, -0.22, 0.42], "bounds_max": [0.1, -0.02, 0.62]}], )"
        R"("surfaces": [{"id": "torque", "pos": [1, 0, 0], "dir": 1, "type": "torque"}, {"pos": [0, 2, 0], "gender": 2, "type": "mechanical"}, {"pos": [-1, 0, 0], "dir": 4, "gender": 2, "type": "mechanical"}, {"pos": [1, 0, 0], "dir": 4, "gender": 2, "type": "mechanical"}, {"pos": [0, -1, 0], "dir": 4, "gender": 2, "type": "port"}], )"
        // The game reads a wheel's links by position in this list: steering_pos, steering_neg, brake. The swingarm
        // does not steer, so its two steering links sit unused on the mounting face, against the frame.
        R"("logic_nodes": [{"id": "steering_pos", "pos": [-1, 0, 0], "direction": 4, "type": "mechanical_in"}, {"id": "steering_neg", "pos": [1, 0, 0], "direction": 4, "type": "mechanical_in"}, {"id": "brake", "pos": [0, 2, 0], "direction": 0, "type": "mechanical_in"}], )"
        R"("mesh_static": {"mesh_path": ")" + m.swingarm_base + R"("}, "meshes_dynamic": [)" +
        wheel_meshes(m.swingarm_arm, kSwingarmPivot, m.swingarm_shock, kShockTop, m.hidden, {0, 0, 0}, m.hidden) +
        R"(], "bounds_override_min": [-3, -7, 0], "bounds_override_max": [3, 4, 11]})";
    std::string lean = std::string(R"({"id": ")") + kLeanAssistId + R"(", "name": "Lean Assist", "description": "Keeps a two-wheeler upright and leans it into turns. Place it with its arrow pointing forward. Power sets how hard it works (Properties tool). Data input assist: 0 switches it off, anything else (or nothing connected) keeps it on. Outputs lean and target_lean in degrees.", "class": "gyro_rotation_sensor", "zones": [{"bounds_max": [0, 1, 0]}], "interaction_zones": [], "surfaces": [{"dir": 2, "gender": 2, "type": "port"}, {"gender": 2, "type": "port"}, {"dir": 1, "gender": 2, "type": "port"}, {"dir": 4, "gender": 2, "type": "port"}, {"dir": 5, "gender": 2, "type": "port"}], "logic_nodes": [], "mesh_static": {"mesh_path": ")" + m.lean_assist +
        R"("}, "meshes_dynamic": [], "category": "sensor", "tags_generated": "port,", "inspection_geometry": [], "mateable_port_types": 2, "data_descriptors": [{"type": "type_f64_output", "name": "rps"}, {"type": "type_f64_input", "name": "assist"}, {"type": "type_f64_output", "name": "lean"}, {"type": "type_f64_output", "name": "target_lean"}], "tech_tier": 2, "spawn_probability": 0.2, "loot_group": "mechanical", "constraint_orientations": [[1, 0, 0, 0, 1, 0, 0, 0, 1]], "force_emitter_element_rotor": {"blades": []}})";
    // Seat: the stock seat's faces on a narrower saddle (3 cells wide, 7 long). Control main face at the front,
    // a control face on each side for throttles or shifters.
    std::string seat = std::string(R"({"id": ")") + kSeatId + R"(", "name": "Motorcycle Seat", "description": "A saddle. Takes handlebars (or a steering wheel) on its main control face at the front and a throttle or shifter on each side.", "class": "seat", "zones": [{"bounds_min": [-1, -1, -3], "bounds_max": [1, 0, 3]}], "interaction_zones": [{"bounds_min": [-0.2, -0.12, -0.28], "bounds_max": [0.2, 0.5, 0.28]}], )"
        R"("surfaces": [{"pos": [-1, -1, -3], "size": [3, 7], "dir": 2, "gender": 2, "type": "port"}, {"pos": [0, -1, 3], "dir": 5, "rot_mode": 1, "gender": 2, "type": "control_main"}, {"pos": [-1, -1, 1], "rot_mode": 1, "gender": 2, "type": "control"}, {"pos": [1, -1, 1], "dir": 1, "rot_mode": 1, "gender": 2, "type": "control"}, {"pos": [-1, -1, -3], "size": [3, 1], "dir": 4, "gender": 2, "type": "port"}], )"
        R"("logic_nodes": [], "mesh_static": {"mesh_path": ")" + m.seat + R"("}, "meshes_dynamic": [], "category": "control", "tags_generated": "interactable,port,control_main,control,", "inspection_geometry": [], "mateable_port_types": 2, "data_descriptors": [], "tech_tier": 1, "spawn_probability": 0.3, "loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []}})";
    // Handlebars: the steering wheel's class and links (steering on top, pedals as the two front links), a short
    // riser from the seat's control face.
    std::string bars = std::string(R"({"id": ")") + kHandlebarsId + R"(", "name": "Handlebars", "description": "Mount on a seat main control face. Mechanical links on the front of the riser: steering -1 to 1 (top), left and right pedals 0 to 1 (the two below).", "class": "steering_wheel", "zones": [{"bounds_min": [0, -1, 0], "bounds_max": [0, 4, 0]}], "interaction_zones": [], )"
        R"("surfaces": [{"pos": [0, -1, 0], "dir": 4, "rot_mode": 1, "gender": 1, "type": "control_main"}, {"pos": [0, 2, 0], "dir": 5, "gender": 1, "type": "mechanical"}, {"pos": [0, 0, 0], "dir": 5, "gender": 1, "type": "mechanical"}, {"pos": [0, 1, 0], "dir": 5, "gender": 1, "type": "mechanical"}], )"
        R"("logic_nodes": [{"id": "wheel", "pos": [0, 2, 0], "direction": 5}, {"id": "pedal_l", "pos": [0, 0, 0], "direction": 5}, {"id": "pedal_r", "pos": [0, 1, 0], "direction": 5}], )"
        R"("mesh_static": {"mesh_path": ")" + m.handlebars_base + R"("}, "meshes_dynamic": [)" + dyn(m.handlebars_bars, kBarsPivot) + ", " + dyn(m.hidden, {0, 0, 0}) + ", " + dyn(m.hidden, {0, 0, 0}) +
        R"(], "category": "control", "tags_generated": "mechanical,port,control_main,", "inspection_geometry": [], "mateable_port_types": 2, "data_descriptors": [{"type": "type_f64_output", "name": "left_pedal"}, {"type": "type_f64_output", "name": "right_pedal"}, {"type": "type_f64_output", "name": "steering"}], "tech_tier": 1, "spawn_probability": 0.3, "loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []}})";
    return "{\n  \"definitions\": [\n    " + fork + ",\n    " + swing + ",\n    " + lean + ",\n    " + seat + ",\n    " + bars + "\n  ]\n}\n";
}

}  // namespace anymoto
