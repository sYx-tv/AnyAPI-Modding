#include "../runtime_task_queue.h"
#include <cassert>
#include <thread>
static void callback() {}
int main() {
    anyapi_runtime::TaskQueue<void(*)()> queue;
    assert(!queue.post(0,0,callback,nullptr));
    auto ticket=queue.post(1,7,callback,nullptr);
    assert(!queue.cancel(2,ticket)); assert(queue.cancel(1,ticket)); assert(queue.take().empty());
    for(int i=0;i<128;++i) assert(queue.post(1,9,callback,nullptr));
    assert(!queue.post(1,9,callback,nullptr));
    auto jobs=queue.take(); assert(jobs.size()==32 && jobs[0].epoch==9);
    assert(!queue.cancel(1,jobs[0].ticket));
    for(int i=0;i<3;++i) assert(queue.take().size()==32);
    assert(queue.take().empty());
    std::thread writer([&]{for(int i=0;i<64;++i) assert(queue.post(1,0,callback,nullptr));});
    writer.join(); assert(queue.take().size()==32); assert(queue.take().size()==32);
}
