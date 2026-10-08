// Example 2: read a transform and perform a supported mutation.
//
// Read: server_scene.entity.get_transform (virtual, returns mat34 through the hidden return pointer).
// Mutation: client-side camera FOV override. client_scene.m_is_fov_override / m_fov_override are plain
// client-local fields that the camera code reads each frame (static inference from the names and from
// client_scene.get_camera_fov reading them). They are not replicated, so changing them cannot desync
// a multiplayer session.
// Prerequisites: in a world. Read runs on the server thread (entity is server-side), the FOV write on the
// main thread (client_scene is main-thread owned) - both through example 4's queues.
// Failure handling: null checks; restore the previous FOV state on stop.
// Label: compile-tested, statically reviewed.
#include "example_common.hpp"

extern "C" void example_post_server(void (*fn)(void*), void* user);
extern "C" void example_post_main(void (*fn)(void*), void* user);

namespace {
using namespace example;
using get_entity_t = void (*)(void** ret, void* container, const int32_t* id);
using get_transform_t = void (*)(double* ret_mat34, const void* entity);

int32_t g_entity_id;
bool g_saved = false, g_prev_override = false;
double g_prev_fov = 0;

// client_scene field offsets are not in the curated symbol header; take them from the layout header.
constexpr size_t kFovOverride = offsetof(gc_client_scene, m_is_fov_override);
constexpr size_t kFov = offsetof(gc_client_scene, m_fov_override);

void read_transform(void*) {
    uint8_t* scene = server_scene_object();
    if (!scene) return;
    void* ents = scene + sym::off_server_scene__m_entities;
    void* e = nullptr;
    if (auto fn = vmethod<get_entity_t>(ents, sym::entity_container_get_entity_by_id_vslot)) fn(&e, ents, &g_entity_id);
    if (!e) { log("[ex02] entity %d not found", g_entity_id); return; }
    double m[12] = {};
    if (auto gt = vmethod<get_transform_t>(e, sym::entity_get_transform_vslot)) gt(m, e);
    log("[ex02] entity %d translation (%.2f, %.2f, %.2f)", g_entity_id, m[9], m[10], m[11]);
}

void set_fov(void* user) {
    uint8_t* cs = client_scene_object();
    if (!cs) return;
    double fov = *static_cast<double*>(user);
    delete static_cast<double*>(user);
    if (!g_saved) { g_prev_override = *(bool*)(cs + kFovOverride); g_prev_fov = *(double*)(cs + kFov); g_saved = true; }
    *(double*)(cs + kFov) = fov;
    *(bool*)(cs + kFovOverride) = true;
}

void restore_fov(void*) {
    uint8_t* cs = client_scene_object();
    if (!cs || !g_saved) return;
    *(double*)(cs + kFov) = g_prev_fov;
    *(bool*)(cs + kFovOverride) = g_prev_override;
    g_saved = false;
}
}  // namespace

extern "C" void example_read_entity_transform(int32_t entity_id) {
    g_entity_id = entity_id;
    example_post_server(&read_transform, nullptr);
}
// fov is in radians [runtime: default 1.5707963 = 90 degrees observed in a hosted world]
extern "C" void example_set_fov(double fov) { example_post_main(&set_fov, new double(fov)); }
extern "C" void example_restore_fov() { example_post_main(&restore_fov, nullptr); }
