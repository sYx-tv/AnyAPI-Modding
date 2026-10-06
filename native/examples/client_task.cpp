#include "anyapi_services_v1.h"
#include "anyapi_client_tasks_v1.h"
static uint64_t owner;
static void work(const AnyClientTaskFrameV1* frame, void*) {
    // Copy observations or use documented client-thread services here.
    // No borrowed game pointers are exposed by this service.
    (void)frame;
}
bool register_client_tasks() {
    auto services=AnyAPI_Services();
    auto tasks=services ? (const AnyClientTasksV1*)services->query("anyapi.client_tasks",1) : nullptr;
    owner=tasks ? tasks->register_owner() : 0;
    return owner!=0;
}
uint64_t schedule_for_world(uint64_t epoch) {
    auto services=AnyAPI_Services();
    auto tasks=services ? (const AnyClientTasksV1*)services->query("anyapi.client_tasks",1) : nullptr;
    return tasks && owner ? tasks->post(owner,epoch,work,nullptr) : 0;
}
