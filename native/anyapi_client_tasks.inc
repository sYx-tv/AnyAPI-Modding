#include "anyapi_client_tasks_v1.h"
#include "runtime_task_queue.h"
namespace client_tasks {
using Callback=void(*)(const AnyClientTaskFrameV1*,void*);
static anyapi_runtime::TaskQueue<Callback> queue;
static bool available(int id) {
    // ModInit runs before its slot is published to other threads. The owned
    // initializing callback may register/post; background callers wait for publication.
    return anyapi_owner_active(id) || (id >= 0 && id < 32
        && platform::current_plugin == id && uint32_t(id) == platform::plugin_count.load()
        && !platform::plugins[id].faulted);
}
static uint64_t owner() {
    auto id=platform::current_plugin;
    return available(id) ? uint64_t(id)+1 : 0;
}
static uint64_t post(uint64_t token,uint64_t epoch,Callback callback,void* user) {
    if (!token || token>32 || !available(int(token)-1)) return 0;
    return queue.post(token,epoch,callback,user);
}
static bool cancel(uint64_t token,uint64_t ticket) { return queue.cancel(token,ticket); }
static bool invoke(const anyapi_runtime::TaskQueue<Callback>::Job* job,const AnyClientTaskFrameV1* frame) {
    __try { job->callback(frame,job->user); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static const AnyClientTasksV1 api{sizeof(AnyClientTasksV1),1,owner,post,cancel};
}
static const AnyClientTasksV1* anyapi_client_tasks_service() { return &client_tasks::api; }
static void anyapi_client_tasks_tick(uint64_t epoch) {
    AnyClientTaskFrameV1 frame; frame.world_epoch=epoch; frame.tick=GetTickCount64();
    for (auto& job:client_tasks::queue.take()) {
        int id=int(job.owner)-1;
        if (!anyapi_owner_active(id) || (job.epoch && job.epoch!=epoch)) continue;
        int previous=platform::current_plugin; platform::current_plugin=id;
        bool ok=client_tasks::invoke(&job,&frame); platform::current_plugin=previous;
        if (!ok) { anyapi_owner_fault(id); log_line(ANY_LOG_ERROR,"framework","Client task provider disabled after exception"); }
    }
}
