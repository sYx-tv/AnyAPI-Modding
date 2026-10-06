#include "legacy_inventory_probe.h"
struct P29InventorySample {
    P29InventoryProbe probe{};
    uint32_t thread{};
    bool server{};
    uint64_t revision{};
};
static P29InventorySample g_p29_inventory_sample{};
static SRWLOCK g_p29_inventory_sample_lock=SRWLOCK_INIT;
static void p29_capture_inventory(uintptr_t inventory,bool server) {
    auto probe=p29_probe_handheld(inventory,server,safe_read_memory);
    g_p29_inventory_samples.fetch_add(1);
    if(!probe.header_readable || (!probe.empty && !probe.definition_readable)) g_p29_inventory_failures.fetch_add(1);
    AcquireSRWLockExclusive(&g_p29_inventory_sample_lock);
    g_p29_inventory_sample={probe,GetCurrentThreadId(),server,g_p29_world_revision.load()};
    ReleaseSRWLockExclusive(&g_p29_inventory_sample_lock);
}
struct P29UiSnapshot {
    uintptr_t renderer{};
    double origin[2]{}, viewport[2]{};
    uint64_t revision{};
    uint32_t thread{};
};
static P29UiSnapshot g_p29_ui_snapshot{};
static SRWLOCK g_p29_ui_snapshot_lock=SRWLOCK_INIT;
