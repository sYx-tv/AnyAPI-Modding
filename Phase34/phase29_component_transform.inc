#include "phase29_transform_patterns.h"
using P29TransformFn=void (*)(double*,void*);
static P29TransformFn g_p29_component_world[2]{},g_p29_component_grid[2]{};
struct P29ComponentTransform {
    uintptr_t component{};
    double world[3]{},grid[3]{};
    uint32_t thread{};
    bool server{},world_ok{},grid_ok{};
    uint64_t revision{};
};
static P29ComponentTransform g_p29_component_transform{};
static SRWLOCK g_p29_component_transform_lock=SRWLOCK_INIT;
static std::atomic<uint64_t> g_p29_component_transform_attempts{0},g_p29_component_transform_ok{0};
static void p29_resolve_transforms() {
    struct Candidate {const char* name;const unsigned char* bytes;size_t size;P29TransformFn* out;};
    Candidate candidates[]={
        {"p29_client_component_world",P29_CLIENT_COMPONENT_WORLD,sizeof(P29_CLIENT_COMPONENT_WORLD),&g_p29_component_world[0]},
        {"p29_client_component_grid",P29_CLIENT_COMPONENT_GRID,sizeof(P29_CLIENT_COMPONENT_GRID),&g_p29_component_grid[0]},
        {"p29_server_component_world",P29_SERVER_COMPONENT_WORLD,sizeof(P29_SERVER_COMPONENT_WORLD),&g_p29_component_world[1]},
        {"p29_server_component_grid",P29_SERVER_COMPONENT_GRID,sizeof(P29_SERVER_COMPONENT_GRID),&g_p29_component_grid[1]}};
    for(auto candidate:candidates) {
        if(InterlockedCompareExchangePointer((void* volatile*)candidate.out,nullptr,nullptr)) continue;
        TargetResolution resolution{candidate.name,candidate.bytes,candidate.size,0,0};
        if(resolve_target(resolution)) InterlockedCompareExchangePointer((void* volatile*)candidate.out,(void*)resolution.address,nullptr);
    }
}
// POD-only SEH leaf. Called only after the original component creation returns,
// never from the worker; no transform pointer or native resource escapes.
// Windows runtime acceptance of these additional const getter calls is pending.
static bool p29_call_transform(P29TransformFn getter,uintptr_t component,double out[3]) {
    if(!getter || !component) return false;
    double matrix[12]{};
    __try { getter(matrix,(void*)component); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return p29_translation(matrix,out);
}
static void p29_capture_component_transform(uintptr_t component,bool server) {
    P29ComponentTransform snapshot{};snapshot.component=component;snapshot.server=server;
    snapshot.thread=GetCurrentThreadId();snapshot.revision=g_p29_world_revision.load();
    size_t side=server?1:0;
    auto world=(P29TransformFn)InterlockedCompareExchangePointer((void* volatile*)&g_p29_component_world[side],nullptr,nullptr);
    auto grid=(P29TransformFn)InterlockedCompareExchangePointer((void* volatile*)&g_p29_component_grid[side],nullptr,nullptr);
    g_p29_component_transform_attempts.fetch_add(1);
    snapshot.world_ok=p29_call_transform(world,component,snapshot.world);
    snapshot.grid_ok=p29_call_transform(grid,component,snapshot.grid);
    if(snapshot.world_ok && snapshot.grid_ok) g_p29_component_transform_ok.fetch_add(1);
    AcquireSRWLockExclusive(&g_p29_component_transform_lock);g_p29_component_transform=snapshot;ReleaseSRWLockExclusive(&g_p29_component_transform_lock);
}
