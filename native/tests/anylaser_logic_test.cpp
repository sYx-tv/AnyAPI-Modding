#include "../anylaser_logic.h"
#include "../anylaser_mesh.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
using namespace anylaser;
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e; }
// A pose at p facing along f (unit).
static Pose facing(Vec3 p, Vec3 f) { Pose o; o.centre = p; o.forward = f; return o; }

// A fake stock mesh with the compass header layout: 4-char magic, version 5, (1), name, 88 header bytes,
// bounds, one triangle, trailer.
static std::vector<uint8_t> fake_stock() {
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); b.insert(b.end(), c, c + n); };
    uint32_t u;
    put("mesh", 4); u = 5; put(&u, 4); u = 1; put(&u, 4);
    const char* name = "compass_sensor_a"; u = uint32_t(std::strlen(name)); put(&u, 4); put(name, u);
    std::vector<uint8_t> header(88, 0); put(header.data(), header.size());
    double bounds[6]{}; put(bounds, sizeof bounds);
    MeshVertex v[3]{}; u = sizeof v; put(&u, 4); put(v, sizeof v);
    uint32_t idx[3] = {0, 1, 2}; u = sizeof idx; put(&u, 4); put(idx, sizeof idx);
    uint8_t trailer[8]{}; put(trailer, 8);
    return b;
}

int main() {
    // Channels: lookups by kind and length; the laser's only input is off.
    assert(channel_index(Kind::Laser, "hit", 3) == kLaserHit && channel_index(Kind::Laser, "distance", 8) == kLaserDistance && channel_index(Kind::Laser, "off", 3) == kLaserOff);
    assert(channel_index(Kind::Sensor, "lit", 3) == kSensorLit && channel_index(Kind::Sensor, "broken", 6) == kSensorBroken && channel_index(Kind::Sensor, "distance", 8) == kSensorDistance);
    assert(channel_index(Kind::Sensor, "hit", 3) < 0 && channel_index(Kind::Laser, "north_angle", 11) < 0 && channel_index(Kind::None, "hit", 3) < 0);
    assert(kLaserChannels[kLaserOff].input && !kLaserChannels[kLaserHit].input);
    for (auto& c : kSensorChannels) assert(!c.input);
    assert(kind_of("anylaser_laser", 14) == Kind::Laser && kind_of("anylaser_sensor", 15) == Kind::Sensor && kind_of("compass_sensor", 14) == Kind::None && kind_of("anylaser_laser", 8) == Kind::None);

    // Definitions: two compass-class parts, one cell, no port on the lens face (dir 5), inputs typed as inputs.
    std::string json = definitions_json("meshes/components/anylaser_laser_a.mesh", "meshes/components/anylaser_sensor_a.mesh");
    assert(json.find("\"id\": \"anylaser_laser\"") != std::string::npos && json.find("\"id\": \"anylaser_sensor\"") != std::string::npos);
    size_t first = json.find("\"class\": \"compass_sensor\""); assert(first != std::string::npos && json.find("\"class\": \"compass_sensor\"", first + 1) != std::string::npos);
    assert(json.find("\"dir\": 5") == std::string::npos && json.find("\"bounds_max\": [0, 0, 0]") != std::string::npos);
    assert(json.find("{\"type\": \"type_f64_input\", \"name\": \"off\"}") != std::string::npos && json.find("{\"type\": \"type_f64_output\", \"name\": \"broken\"}") != std::string::npos);
    assert(json.find("anylaser_laser_a.mesh") != std::string::npos && json.find("anylaser_sensor_a.mesh") != std::string::npos);
    assert(json.find('\t') == std::string::npos);

    // Pose from a mat34 (columns right, up, forward, translation).
    Mat34 m; m.m[6] = 0; m.m[7] = 0; m.m[8] = 1; m.m[9] = 1; m.m[10] = 2; m.m[11] = 3;
    Pose pose; assert(pose_of(m, pose) && near(pose.centre.x, 1) && near(pose.forward.z, 1));
    Mat34 bad = m; bad.m[8] = NAN; assert(!pose_of(bad, pose));
    // compose: a part one cell forward on a vehicle turned 90 degrees about +Y faces +X.
    Mat34 v; v.m[0] = 0; v.m[2] = -1; v.m[6] = 1; v.m[8] = 0; Mat34 c; c.m[11] = 0.08;
    assert(pose_of(compose(v, c), pose) && near(pose.forward.x, 1) && near(pose.centre.x, 0.08));

    // Beam: starts just past the lens; a miss runs the full range and reports no hit.
    Pose laser = facing({0, 1, 0}, {0, 0, 1});
    Beam miss = beam_from(laser, kRange);
    assert(!miss.hit && miss.distance == 0 && near(miss.start.z, kStartOffset) && near(miss.end.z, kStartOffset + kRange));
    Beam hit = beam_from(laser, 2.0);
    assert(hit.hit && near(hit.distance, 2.0) && near(hit.end.z, kStartOffset + 2.0));
    assert(!beam_from(laser, NAN).hit && beam_from(laser, -1).distance == 0);

    // A sensor 2.088 m away facing back: its lens face (0.04 in front of its centre) is where the ray stops.
    Pose sensor = facing({0, 1, kStartOffset + 2.0 + kHalf}, {0, 0, -1});
    assert(lands_on(hit, sensor));
    Pose turned = facing(sensor.centre, {0, 0, 1});                  // lens facing away: the back is hit, not the lens
    assert(!lands_on(hit, turned));
    Pose side = facing({0.2, 1, sensor.centre.z}, {0, 0, -1});       // a sensor beside the beam
    assert(!lands_on(hit, side));
    Pose angled = facing(sensor.centre, {0.7071, 0, -0.7071});       // 45 degrees off still counts
    assert(lands_on(hit, angled));
    Pose edge = facing({0.035, 1.035, sensor.centre.z}, {0, 0, -1});  // the beam on the lens face's corner
    assert(lands_on(hit, edge));
    assert(!lands_on(miss, sensor));

    // Sensor state: lit while beams keep arriving, broken shortly after they stop.
    Part s; s.kind = Kind::Sensor;
    update_sensor(s, 0); assert(s.slots[kSensorLit] == 0 && s.slots[kSensorBroken] == 1 && s.slots[kSensorDistance] == 0);
    light_sensor(s, hit, 1.0); update_sensor(s, 1.0);
    assert(s.slots[kSensorLit] == 1 && s.slots[kSensorBroken] == 0 && near(s.slots[kSensorDistance], 2.0 + kStartOffset));
    update_sensor(s, 1.0 + kLitHold * 0.5); assert(s.slots[kSensorLit] == 1);
    update_sensor(s, 1.0 + kLitHold + 0.01); assert(s.slots[kSensorLit] == 0 && s.slots[kSensorBroken] == 1 && s.slots[kSensorDistance] == 0);
    // Two lasers on one sensor: the nearer distance wins while both are on.
    Part two; light_sensor(two, beam_from(laser, 5.0), 3.0); light_sensor(two, beam_from(laser, 1.0), 3.0); light_sensor(two, beam_from(laser, 5.0), 3.01);
    update_sensor(two, 3.02); assert(near(two.slots[kSensorDistance], 1.0 + kStartOffset));

    // Laser outputs.
    Part l; update_laser(l, hit); assert(l.slots[kLaserHit] == 1 && near(l.slots[kLaserDistance], 2.0));
    update_laser(l, miss); assert(l.slots[kLaserHit] == 0 && l.slots[kLaserDistance] == 0);

    // A player standing across the beam cuts it at their capsule; one beside it does not.
    double cut = block_by_capsule({0, 1, 0}, {0, 0, 1}, 10, {0, 0, 4});
    assert(cut > 3.6 && cut < 3.75);
    assert(block_by_capsule({0, 1, 0}, {0, 0, 1}, 10, {1, 0, 4}) == 10);
    assert(block_by_capsule({0, 1, 0}, {0, 0, 1}, 10, {0, 3, 4}) == 10);     // above the beam
    assert(block_by_capsule({0, 1, 0}, {0, 0, 1}, 3, {0, 0, 4}) == 3);       // beyond what the ray hit

    // Projection: a camera at the origin looking +Z; a point straight ahead lands in the middle of the screen.
    Camera cam; cam.forward = {0, 0, 1}; cam.up = {0, 1, 0}; cam.right = {1, 0, 0}; cam.tan_half_fov = 1;
    ScreenPoint a, b;
    assert(project_segment(cam, {0, 0, 5}, {1, 0, 5}, 1000, 500, a, b) && near(a.x, 500) && near(a.y, 250) && near(b.x, 550) && near(a.depth, 5));
    assert(!project_segment(cam, {0, 0, -5}, {1, 0, -1}, 1000, 500, a, b));          // all behind
    assert(project_segment(cam, {0, 0, -5}, {0, 1, 5}, 1000, 500, a, b) && a.depth > 0.05 && a.depth < 0.1 && near(b.depth, 5));   // clipped
    assert(!Camera{}.valid());
    assert(near(pixels(cam, 0.02, 10, 500), 0.5));
    Quad q; assert(beam_quad({0, 0, 1}, {100, 0, 1}, 2, 1, q) && near(q.y[0], 2) && near(q.y[1], 1) && near(q.y[2], -1) && near(q.y[3], -2));
    assert(!beam_quad({0, 0, 1}, {0.1, 0, 1}, 2, 1, q));

    // Meshes: every look builds both parts inside the one cell, with valid indices and a recognised header.
    std::vector<uint8_t> stock = fake_stock();
    MeshLayout layout; assert(parse_mesh(stock, layout));
    for (int look = 0; look < 3; ++look)
        for (int part = 0; part < 2; ++part) {
            MeshBuilder mb; build_part(mb, look, part == 0);
            assert(!mb.verts.empty() && mb.idx.size() % 3 == 0);
            for (uint32_t i : mb.idx) assert(i < mb.verts.size());
            for (auto& vx : mb.verts) for (float p : vx.pos) assert(std::fabs(p) <= float(kHalf) + 1e-6f);
            std::vector<uint8_t> out = build_part_mesh(stock, look, part == 0, "anylaser_test");
            MeshLayout ol; assert(parse_mesh(out, ol) && ol.vertices == mb.verts.size() && ol.indices == mb.idx.size());
        }
    assert(build_part_mesh({}, 0, true, "x").empty());
    return 0;
}
