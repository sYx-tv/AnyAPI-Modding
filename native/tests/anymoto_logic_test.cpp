#include "../anymoto_logic.h"
#include "../anymoto_mesh.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
using namespace anymoto;
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e; }
static bool near(Vec3 a, Vec3 b, double e = 1e-6) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.z, b.z, e); }

// A mesh is well formed: indices in range, whole triangles, normals unit length.
static void check_mesh(const Mesh& m) {
    assert(!m.v.empty() && m.i.size() % 3 == 0);
    for (uint32_t i : m.i) assert(i < m.v.size());
    for (auto& v : m.v) {
        double n = std::sqrt(double(v.normal[0]) * v.normal[0] + double(v.normal[1]) * v.normal[1] + double(v.normal[2]) * v.normal[2]);
        assert(near(n, 1, 1e-3));
    }
    std::vector<uint8_t> file = encode_mesh(m, "anymoto_test");
    assert(file.size() == 16 + 12 + 88 + 48 + 4 + m.v.size() * 36 + 4 + m.i.size() * 4 + 8 && std::memcmp(file.data(), "mesh", 4) == 0);
}

// Outward faces: for a box, every triangle's stored winding (a, c, b) must face inward from the viewer's side, i.e.
// the stored order is clockwise seen from outside.
static void check_box_winding() {
    Mesh m;
    m.box({-1, -1, -1}, {1, 1, 1}, kIron);
    for (size_t t = 0; t < m.i.size(); t += 3) {
        auto p = [&](int k) { auto& v = m.v[m.i[t + k]]; return Vec3{v.pos[0], v.pos[1], v.pos[2]}; };
        Vec3 n = cross(p(1) - p(0), p(2) - p(0));
        Vec3 centre = (p(0) + p(1) + p(2)) * (1.0 / 3);
        assert(dot(n, centre) < 0);   // stored clockwise from outside
    }
}

int main() {
    // Ids.
    assert(kind_of_id("moto_fork", 9) == Kind::fork && kind_of_id("moto_swingarm", 13) == Kind::swingarm);
    assert(kind_of_id("moto_lean_assist", 16) == Kind::lean_assist && kind_of_id("moto_seat", 9) == Kind::seat);
    assert(kind_of_id("moto_handlebars", 15) == Kind::handlebars && kind_of_id("wheel", 5) == Kind::other && kind_of_id("moto_for", 8) == Kind::other);

    // Matrices.
    Mat34 a = compose(translation({1, 2, 3}), rotation_y(kPi / 2));
    assert(near(apply(a, {0, 0, 1}), {2, 2, 3}));
    assert(near(steering_angle_of(rotation_y(0.3)), 0.3));
    Mat34 s = stretch_between({0, 1, 0}, {0, 1, 2});
    assert(near(apply(s, {0, 0, 1}), {0, 1, 2}) && near(apply(s, {0, 0, 0.5}), {0, 1, 1}));
    Mat34 s2 = stretch_between({0, 0, 0}, {0, -1, 1});
    assert(near(apply(s2, {0, 0, 1}), {0, -1, 1}) && near(length(column(s2, 0)), 1) && near(length(column(s2, 1)), 1));

    // A quarter turn about y moves an axle on z to x, and is its own integer matrix.
    int32_t id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, out[9];
    quarter_turn_y(id, out);
    // Column 2 (where local z goes) is now +x.
    assert(out[6] == 1 && out[7] == 0 && out[8] == 0 && out[4] == 1 && out[2] == -1);

    // Swingarm angle: tyre straight behind the pivot = 0, below = positive (rotation_x turns +z down).
    assert(near(swingarm_angle(kSwingarmPivot + Vec3{0, 0, 0.5}), 0));
    double down = swingarm_angle(kSwingarmPivot + Vec3{0, -0.2, 0.5});
    assert(down > 0 && near(apply(rotation_x(down), {0, 0, 1}).y < 0 ? 1 : 0, 1));
    Vec3 dir = normalized(apply(rotation_x(down), {0, 0, 1}));
    assert(near(dir, normalized(Vec3{0, -0.2, 0.5}), 1e-9));

    // Handlebars: centred bars run across x; full lock swings a grip well forward or back (the sense is checked in game).
    Vec3 grip0 = apply(bars_frame(0), kGripRight), grip1 = apply(bars_frame(1), kGripRight);
    assert(grip0.x > 0.25 && std::abs(grip1.z - grip0.z) > 0.1);

    // Fork rake: at rest the legs run from the head down the raked axis to the axle; compressed, the lower frame
    // still lands on the axle; steering turns about the raked axis, so the head stays put.
    {
        Vec3 tyre = fork_tuning().tyre, head = fork_head();
        assert(head.y > tyre.y + 0.6 && head.z < tyre.z - 0.25);
        ForkFrames rest = fork_frames(tyre, 0);
        assert(near(column(rest.upper, 3), head) && near(column(rest.lower, 3), tyre, 1e-9));
        Vec3 up_leg = column(rest.upper, 1);   // +y of the upper frame points up the legs, tipped back by the rake
        assert(near(std::atan2(-up_leg.z, up_leg.y), rad(kForkRakeDeg), 1e-9));
        Vec3 squashed = tyre + Vec3{0, 0.14, 0};
        assert(near(column(fork_frames(squashed, 0).lower, 3), squashed, 1e-9));
        ForkFrames lock = fork_frames(tyre, rad(30));
        assert(near(column(lock.upper, 3), head, 1e-9) && near(column(lock.lower, 3), tyre, 1e-9));
        Vec3 axle_dir = column(lock.lower, 0);   // the axle swings toward the steering, mostly level
        assert(std::abs(axle_dir.z) > 0.4 && std::abs(axle_dir.y) < 0.25);
        Vec3 off = column(fork_frames(squashed, rad(30)).lower, 3) - squashed;
        assert(length(off) < 0.035);
    }

    // Lean assist: upright and going straight = no correction; tipped right = pushed back left; strength 0 = off.
    LeanInput in;
    in.forward = {0, 0, 1}; in.up = {0, 1, 0}; in.strength = 1; in.dt = 1.0 / 60;
    LeanOutput o = lean_assist(in);
    assert(o.active && near(o.angular_velocity, {0, 0, 0}) && near(o.lean_deg, 0) && near(o.target_deg, 0));
    double tilt = rad(20);
    in.up = {std::sin(tilt), std::cos(tilt), 0};   // top leaning to +x
    o = lean_assist(in);
    assert(o.active && std::abs(o.lean_deg) > 19 && std::abs(o.lean_deg) < 21);
    // Rolling back means rotating the up axis from +x toward +y: angular velocity about +z must be positive.
    assert(dot(o.angular_velocity, {0, 0, 1}) > 0);
    in.up = {-std::sin(tilt), std::cos(tilt), 0};
    assert(dot(lean_assist(in).angular_velocity, {0, 0, 1}) < 0);
    // Already rolling back fast: damping holds it back (less correction than from rest).
    in.up = {std::sin(tilt), std::cos(tilt), 0};
    double from_rest = dot(lean_assist(in).angular_velocity, {0, 0, 1});
    in.angular_velocity = {0, 0, 3};
    assert(dot(lean_assist(in).angular_velocity, {0, 0, 1}) - 3 < from_rest);
    in.angular_velocity = {};
    in.strength = 0;
    assert(!lean_assist(in).active);
    // Fallen over (80 degrees): leave it.
    in.strength = 1; in.up = {std::sin(rad(80)), std::cos(rad(80)), 0};
    assert(!lean_assist(in).active);
    // Turning left at speed: the target leans into the turn, the same way as yaw x velocity.
    in.up = {0, 1, 0}; in.linear_velocity = {0, 0, 15}; in.angular_velocity = {0, 0.6, 0};
    o = lean_assist(in);
    assert(std::abs(o.target_deg) > 30 && std::abs(o.target_deg) < 46);
    Vec3 centripetal = cross(Vec3{0, 0.6, 0}, Vec3{0, 0, 15});   // +x: the turn's centre
    // The correction rolls the top toward the centre: about +z, toward +x means negative angular velocity.
    assert(centripetal.x > 0 && dot(o.angular_velocity, {0, 0, 1}) < 0);
    // Simulate with gravity tipping it over (a bike's mass 0.6 m up): tipped 15 degrees, it settles upright within
    // 2 s at power 0.6, at 60 and at 30 ticks a second, whichever way the physics spins (spin_sign).
    for (double hz : {60.0, 30.0})
        for (double spin : {1.0, -1.0}) {
            double phi = rad(15), w = 0;   // our convention: +w about +z lowers phi
            for (int k = 0; k < int(2 * hz); ++k) {
                LeanInput sim;
                sim.forward = {0, 0, 1}; sim.up = {std::sin(phi), std::cos(phi), 0};
                sim.angular_velocity = {0, 0, spin * w}; sim.strength = 0.6; sim.dt = 1 / hz; sim.spin_sign = spin;
                w = spin * dot(lean_assist(sim).angular_velocity, {0, 0, 1});
                w -= 9.81 / 0.6 * std::sin(phi) * sim.dt;   // gravity tips it further
                phi -= w * sim.dt;
            }
            assert(std::abs(phi) < rad(2));
        }
    // With a rider the physics' roll rate bounces +-2 rad/s from tick to tick: damping on the smoothed rate seen from
    // the lean (measured_rate) still settles it, and the bounce is passed through, not pumped.
    {
        double phi = rad(15), w = 0;
        for (int k = 0; k < 120; ++k) {
            double bounce = (k % 2 ? 2.0 : -2.0);
            LeanInput sim;
            sim.forward = {0, 0, 1}; sim.up = {std::sin(phi), std::cos(phi), 0};
            sim.angular_velocity = {0, 0, w + bounce}; sim.measured_rate = w; sim.strength = 1; sim.dt = 1.0 / 60;
            w = dot(lean_assist(sim).angular_velocity, {0, 0, 1}) - bounce;
            w -= 9.81 / 1.0 * std::sin(phi) / 60;
            phi -= w / 60;
        }
        assert(std::abs(phi) < rad(2));
    }
    // Pivoting about the contact line: the contact point's velocity (v + w x r) is unchanged by the spin change.
    {
        Vec3 com{0, 0.6, 0}, contact{0, 0, 0.1}, dw{0, 0, 1.5};
        Vec3 dv = pivot_velocity_change(dw, contact, com);
        assert(near(dv + cross(dw, contact - com), {0, 0, 0}, 1e-12) && std::abs(dv.x) > 0.8);
    }
    // Parked and upright: no nudging (that scrubs the tyres).
    {
        LeanInput still;
        still.forward = {0, 0, 1}; still.up = {std::sin(rad(0.3)), std::cos(rad(0.3)), 0}; still.strength = 1; still.dt = 1.0 / 60;
        still.measured_rate = 0.01;
        LeanOutput o2 = lean_assist(still);
        assert(o2.active && o2.settled);
    }
    // Without the assist the same bike falls past 60 degrees within 2 s (the sim above is a real test).
    {
        double phi = rad(15), w = 0;
        for (int k = 0; k < 120; ++k) { w -= 9.81 / 0.6 * std::sin(phi) / 60; phi -= w / 60; if (phi > rad(60)) break; }
        assert(phi > rad(60));
    }
    // lean_rate_for: +w about forward lowers the lean of a bike leaning right in this convention.
    {
        double l0 = lean_assist(LeanInput{{0, 0, 1}, {std::sin(rad(20)), std::cos(rad(20)), 0}, {}, {}, 1, 1, 1}).lean_deg;
        double a = 0.01;
        Vec3 up1 = Vec3{std::sin(rad(20) - a), std::cos(rad(20) - a), 0};   // turned by +a about +z
        double l1 = lean_assist(LeanInput{{0, 0, 1}, up1, {}, {}, 1, 1, 1}).lean_deg;
        double predicted = lean_rate_for({0, 0, 1}, {std::sin(rad(20)), std::cos(rad(20)), 0}, 1) * a * 180 / kPi;
        assert(near(l1 - l0, predicted, 0.01));
    }

    // Definitions: five parts, the wheel parts with 12 moving meshes, names the game shows.
    MeshPaths paths{"a.mesh", "b.mesh", "c.mesh", "d.mesh", "e.mesh", "f.mesh", "h.mesh", "l.mesh", "s.mesh", "hb.mesh", "hd.mesh"};
    std::string json = definitions_json(paths);
    for (const char* id : {kForkId, kSwingarmId, kLeanAssistId, kSeatId, kHandlebarsId}) assert(json.find(std::string("\"id\": \"") + id + "\"") != std::string::npos);
    assert(json.find("\"name\": \"Motorcycle Fork\"") != std::string::npos && json.find("\"class\": \"steering_wheel\"") != std::string::npos);
    size_t fork = json.find(kForkId), swing = json.find(kSwingarmId);
    size_t count = 0;
    for (size_t p = json.find("\"path\"", fork); p != std::string::npos && p < swing; p = json.find("\"path\"", p + 1)) ++count;
    assert(json.find("preview_rot", fork) < swing);
    // The game reads a wheel's links by index (steering_pos, steering_neg, brake): the swingarm needs all three.
    for (size_t at : {fork, swing}) {
        size_t nodes = json.find("\"logic_nodes\"", at);
        size_t p0 = json.find("steering_pos", nodes), p1 = json.find("steering_neg", nodes), p2 = json.find("\"brake\"", nodes);
        assert(p0 < p1 && p1 < p2 && p2 < json.find("mesh_static", nodes));
    }
    assert(count == 12);
    assert(json.find("type_f64_input\", \"name\": \"assist\"") != std::string::npos);
    int depth = 0;
    for (char c : json) { depth += c == '{' || c == '['; depth -= c == '}' || c == ']'; assert(depth >= 0); }
    assert(depth == 0);

    // Meshes.
    check_box_winding();
    for (const Mesh& m : {fork_base(), fork_steer(), fork_lower(), swingarm_base(), swingarm_arm(), swingarm_shock(), hidden_speck(), lean_assist(), seat(), handlebars_base(), handlebars_bars()}) check_mesh(m);
    return 0;
}
