#include "../runtime_call_cell.h"
#include <cassert>
#include <thread>
static volatile int marker;
static void original() { marker=1; }
static void replacement() { marker=2; }
static void second() { marker=3; }
int main() {
    using anyapi_runtime::CallCellHook;
    alignas(void*) void* cell = (void*)original;
    CallCellHook first, top;
    assert(!first.install(nullptr, (void*)replacement));
    assert(first.install(&cell, (void*)replacement));
    assert(first.original() == (void*)original);
    assert(top.install(&cell, (void*)second));
    assert(!first.remove()); // must not erase another mod's hook
    assert(cell == (void*)second);
    assert(top.remove()); assert(first.remove()); assert(cell == (void*)original);
    assert(first.original() == (void*)original); // late detours can still call through
    std::atomic<bool> stop{}, bad{};
    std::thread reader([&] {
        while (!stop.load()) {
            auto value = InterlockedCompareExchangePointer(&cell, nullptr, nullptr);
            if (value == (void*)replacement && first.original() != (void*)original) bad = true;
        }
    });
    for (int i=0; i<10000; ++i) { assert(first.install(&cell, (void*)replacement)); assert(first.remove()); }
    stop = true; reader.join(); assert(!bad);
    void* page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(page); *(void**)page = (void*)original;
    DWORD previous{}; assert(VirtualProtect(page, 4096, PAGE_READONLY, &previous));
    CallCellHook readonly; assert(!readonly.install((void**)page, (void*)replacement));
    VirtualFree(page, 0, MEM_RELEASE);
}
