#pragma once
// AnyVehicleOverhaul trailing link gear (Nate 2026-11-25, look 3 of the three shown): a steerable suspension leg with
// its own built-in wheel, in three sizes. Pure logic, no game calls (tested by tests/avo_gear_logic_test.cpp).
//
// How it rests on the game (the same facts as AnyMoto, anymoto_logic.h, proven there in game):
//   - A wheel-class part IS the suspension: the spring is a raycast inside vehicle_element.wheel, run along the part's
//     own down axis (y), so travel is straight up and down when the part sits upright, and steering turns the tyre
//     about the part's vertical axis. Each part gets its own copy of the stock wheel's code-side tuning
//     (vehicle_element_definition.wheel, 328 bytes), set by the mod on the server and the client.
//   - m_is_tread_integral (tuning +320) is the game's own "this wheel has its tyre built in" flag: full grip with no
//     tyre item fitted. The copies accept no tyre items at all and carry the wheel's radius in m_radius.
//   - Surface dirs: 0 = -x, 1 = +x, 2 = -y, 3 = +y, 4 = -z, 5 = +z. A part grows along +z from the face it is placed
//     on; y is up. Cells are 0.08 m, cell 0 centred on the origin.
//
// Layout (part frame, metres): a top mount (Nate 2026-11-28). The mount plate (3 x 3 cells, one cell thick, centred
// on the origin) bolts to the underside of a hull or wing by its top face (dir 3, at y = 0.04), the way a seat
// stands on a floor by its bottom face, so the part's y stays up. The leg drops from the plate's centre to the pivot;
// the trailing arm runs out (+z) from the pivot to the axle, and two coilovers, one each side of the wheel, run from
// a cross bar on the leg to lugs on the arm's side plates. The links sit on the plate's sides and back.
#include "anymoto_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace avogear {
using anymoto::Mat34;
using anymoto::Vec3;
using anymoto::rad;
using anymoto::kPi;

constexpr double kCell = 0.08;

struct Size {
    const char* id;        // never rename (saves keep parts by id)
    const char* name;
    const char* key;       // mesh names
    double scale;          // of the leg and arm layout
    double radius, width;  // wheel
    double travel;         // m_suspension_length
    double steer_deg;
};
inline constexpr Size kSizes[] = {
    {"avo_gear_trailing_s", "Trailing Link Gear (Small)", "s", 0.70, 0.17, 0.12, 0.10, 40},
    {"avo_gear_trailing_m", "Trailing Link Gear (Medium)", "m", 1.00, 0.25, 0.16, 0.15, 40},
    {"avo_gear_trailing_l", "Trailing Link Gear (Large)", "l", 1.40, 0.36, 0.22, 0.22, 35},
};
constexpr int kSizeCount = int(sizeof kSizes / sizeof kSizes[0]);
inline int size_of_id(const char* id, size_t n) {
    if (!id) return -1;
    for (int i = 0; i < kSizeCount; ++i)
        if (n == std::strlen(kSizes[i].id) && std::memcmp(id, kSizes[i].id, n) == 0) return i;
    return -1;
}

// ------------------------------------------------------------------------------------------- geometry
// Everything below is in the part frame. The steering axis is the leg (x = 0, z = leg_z).
struct Layout {
    double leg_z, leg_r;           // leg axis and radius
    double leg_top;                // where the leg meets the mount bracket
    Vec3 pivot;                    // arm pivot at the foot of the leg
    double arm;                    // pivot to axle
    double fork_x;                 // the arm's two side plates, either side of the tyre
    double mid_angle;              // arm angle below horizontal at mid travel (rad)
    Vec3 shock_top;                // on the leg's bracket
    double shock_along;            // lug position along the arm (0..1)
    double shock_lift;             // lug above the arm's centre line
    Vec3 tyre_top;                 // tuning m_position: the axle with the suspension fully compressed
};
inline Layout layout(const Size& s) {
    Layout l;
    double k = s.scale;
    l.leg_r = 0.035 * k;
    l.leg_z = 0;
    l.leg_top = -0.04;
    l.arm = std::max(s.radius + l.leg_r + 0.06, 1.75 * s.travel);
    l.fork_x = s.width / 2 + 0.03 * std::max(k, 0.8);
    l.mid_angle = rad(6);
    l.pivot = {0, -0.06 - 0.30 * k, l.leg_z};
    l.shock_top = {0, l.pivot.y + 0.22 * k, l.leg_z + l.leg_r + 0.035 * k};
    l.shock_along = 0.55;
    l.shock_lift = 0.04 * k;
    // The game moves the axle straight up and down; the arm swings on an arc about the pivot. Mid travel sits on
    // the arm at mid_angle, so the arc's end is within a couple of cm of the game's axle all through the travel.
    Vec3 mid = l.pivot + Vec3{0, -std::sin(l.mid_angle), std::cos(l.mid_angle)} * l.arm;
    l.tyre_top = mid + Vec3{0, s.travel / 2, 0};
    return l;
}
// The arm angle (rotation_x, as swingarm_angle in AnyMoto: turns the arm's +z toward the axle) for an axle height.
inline double arm_angle(const Layout& l, double axle_y) {
    double s = std::clamp((l.pivot.y - axle_y) / l.arm, -0.98, 0.98);
    return std::asin(s);
}
// Where the arm puts the axle (drawn tyre centre) for an angle.
inline Vec3 arm_end(const Layout& l, double angle) { return l.pivot + Vec3{0, -std::sin(angle), std::cos(angle)} * l.arm; }
inline Vec3 shock_lug(const Layout& l, double angle) {
    Vec3 along{0, -std::sin(angle), std::cos(angle)}, up{0, std::cos(angle), std::sin(angle)};
    return l.pivot + along * (l.arm * l.shock_along) + up * l.shock_lift;
}
// Lowest point of the wheel at full droop (for bounds).
inline double lowest_y(const Size& s) { Layout l = layout(s); return l.tyre_top.y - s.travel - s.radius; }

// ------------------------------------------------------------------------------------------- drawing frames
struct Frames { Mat34 steer, arm, shock, tyre; Vec3 axle; };
// axle_y: the tyre centre's height from the game (accessory transform); steer: the steering angle (rad, about +y);
// spin: the wheel's rotation (rad, about the axle). The assembly below the mount turns about the leg.
inline Frames frames(const Size& s, double axle_y, double steer, double spin) {
    using namespace anymoto;
    Layout l = layout(s);
    Mat34 turn = compose(compose(translation({0, 0, l.leg_z}), rotation_y(steer)), translation({0, 0, -l.leg_z}));
    double a = arm_angle(l, axle_y);
    Frames f;
    f.steer = turn;
    f.arm = compose(turn, compose(translation(l.pivot), rotation_x(a)));
    f.shock = compose(turn, stretch_between(l.shock_top, shock_lug(l, a)));
    f.axle = arm_end(l, a);
    f.tyre = compose(turn, compose(translation(f.axle), rotation_x(spin)));
    return f;
}

// ------------------------------------------------------------------------------------------- definitions
struct MeshPaths { std::string base[kSizeCount], steer[kSizeCount], arm[kSizeCount], shock[kSizeCount], tyre[kSizeCount], hidden; };
inline int cell(double metres) { return int(std::lround(metres / kCell)); }

inline std::string description(const Size& s) {
    char b[600];
    std::snprintf(b, sizeof b,
                  "Trailing link landing gear with its own %.0f cm wheel built in (no tyre to fit). Bolt the top of the mount plate to the "
                  "underside of a hull or wing: the leg drops from it and the wheel trails out. Links on the mount plate: steering in on each "
                  "side (one turns it left, the other right) and the brake on the back. Drive in through the torque face on the front. "
                  "%.0f cm of travel, steers %.0f degrees each way. Spring and damping in the Properties tool.",
                  s.radius * 200, s.travel * 100, s.steer_deg);
    return b;
}

inline std::string definition_json(int i, const MeshPaths& m) {
    const Size& s = kSizes[i];
    Layout l = layout(s);
    char b[2400];
    // Rest pose for the previews: the arm at mid travel.
    Frames rest = frames(s, l.tyre_top.y - s.travel / 2, 0, 0);
    auto dyn = [](const std::string& path, const Mat34& f) {
        char t[400];
        std::snprintf(t, sizeof t, "{\"path\": \"%s\", \"pos\": [%.4f, %.4f, %.4f], \"preview_rot\": [%.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f]}",
                      path.c_str(), f.m[9], f.m[10], f.m[11], f.m[0], f.m[1], f.m[2], f.m[3], f.m[4], f.m[5], f.m[6], f.m[7], f.m[8]);
        return std::string(t);
    };
    std::string meshes = dyn(m.steer[i], rest.steer) + ", " + dyn(m.arm[i], rest.arm) + ", " + dyn(m.shock[i], rest.shock) + ", " + dyn(m.tyre[i], rest.tyre);
    for (int k = 4; k < 12; ++k) meshes += ", " + anymoto::dyn(m.hidden, {0, 0, 0});
    int leg_cell_z = cell(l.leg_z), pivot_cell = cell(l.pivot.y);
    int low = cell(lowest_y(s)) - 1, reach = cell(l.tyre_top.z + s.radius) + 1, side = cell(std::max(l.fork_x + 0.03, s.radius)) + 1;
    std::string out = std::string("{\"id\": \"") + s.id + "\", \"name\": \"" + s.name + "\", \"description\": \"" + description(s) + "\", ";
    out += R"("class": "wheel", "category": "wheel", "tags_generated": "interactable,mechanical,port,torque,", "inspection_geometry": [], "mateable_port_types": 2, )"
           R"("data_descriptors": [{"type": "type_f64_output", "name": "brake_factor"}, {"type": "type_f64_output", "name": "rps"}], "tech_tier": 3, "spawn_probability": 0.1, )"
           R"("loot_group": "mechanical", "constraint_orientations": [], "force_emitter_element_rotor": {"blades": []}, )";
    // Zones: the mount plate (3 x 3 cells) and the leg column down to the pivot. The arm and wheel move, so they
    // are left out of the zones (as the stock wheel's tyre and AnyMoto's swingarm).
    std::snprintf(b, sizeof b,
                  R"("zones": [{"bounds_min": [-1, 0, -1], "bounds_max": [1, 0, 1]}, {"bounds_min": [0, %d, %d], "bounds_max": [0, -1, %d]}], )"
                  R"("interaction_zones": [{"bounds_min": [%.3f, %.3f, %.3f], "bounds_max": [%.3f, %.3f, %.3f]}], )",
                  std::min(pivot_cell, -1), leg_cell_z, leg_cell_z,
                  -s.width / 2, rest.axle.y - s.radius, rest.axle.z - s.radius, s.width / 2, rest.axle.y + s.radius, rest.axle.z + s.radius);
    out += b;
    // Surfaces: the mount face on top (a port face, as the seat's bottom), the links on the plate's sides and back,
    // the torque face on the front.
    out += R"("surfaces": [{"pos": [-1, 0, -1], "size": [3, 3], "dir": 3, "gender": 2, "type": "port"}, )"
           R"({"pos": [-1, 0, 0], "gender": 2, "type": "mechanical"}, {"pos": [1, 0, 0], "dir": 1, "gender": 2, "type": "mechanical"}, )"
           R"({"pos": [0, 0, -1], "dir": 4, "gender": 2, "type": "mechanical"}, {"id": "torque", "pos": [0, 0, 1], "dir": 5, "type": "torque"}], )";
    // The game reads a wheel's links by position in this list: steering_pos, steering_neg, brake.
    out += R"("logic_nodes": [{"id": "steering_pos", "pos": [-1, 0, 0], "direction": 0, "type": "mechanical_in"}, {"id": "steering_neg", "pos": [1, 0, 0], "direction": 1, "type": "mechanical_in"}, )"
           R"({"id": "brake", "pos": [0, 0, -1], "direction": 4, "type": "mechanical_in"}], )";
    out += "\"mesh_static\": {\"mesh_path\": \"" + m.base[i] + "\"}, \"meshes_dynamic\": [" + meshes + "], ";
    std::snprintf(b, sizeof b, R"("bounds_override_min": [%d, %d, -2], "bounds_override_max": [%d, 1, %d]})", -side, low, side, reach);
    return out + b;
}
inline std::string definitions_json(const MeshPaths& m) {
    std::string out = "{\n  \"definitions\": [\n";
    for (int i = 0; i < kSizeCount; ++i) out += "    " + definition_json(i, m) + (i + 1 < kSizeCount ? ",\n" : "\n");
    return out + "  ]\n}\n";
}

}  // namespace avogear
