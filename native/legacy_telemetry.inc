#include "legacy_contracts.h"
#include "legacy_api_audit_state.h"
static P29ApiAuditRow g_p29_api_audit[34];
static P29ThreadRecord g_p29_hook_threads[165];
static std::atomic<uint64_t> g_p29_generations{0};
static std::atomic<uint64_t> g_p29_world_revision{1};
static std::atomic<uint64_t> g_p29_stale_player_events{0};
static std::atomic<uint64_t> g_p29_reused_addresses{0}, g_p29_teardowns{0};
static std::atomic<uint64_t> g_p29_inventory_samples{0}, g_p29_inventory_failures{0};
static std::atomic<uint64_t> g_p29_ui_resource_changes{0};
static std::atomic<uint64_t> g_p29_transform_samples{0}, g_p29_transform_invalid{0};
static std::atomic<uint64_t> g_p31_hook_last_tick[165];
static void p29_note_hook(size_t slot) {
    g_p31_hook_last_tick[slot].store(GetTickCount64());
    g_p29_hook_threads[slot].observe(GetCurrentThreadId());
}
static uint64_t p29_next_generation() {
    // Generation zero is never accepted. Exhaustion fails closed rather than wrapping.
    uint64_t current = g_p29_generations.load();
    do {
        if (current == UINT64_MAX) return 0;
    } while (!g_p29_generations.compare_exchange_weak(current, current + 1));
    return current + 1;
}

static std::atomic<uint64_t> g_p29_component_metadata_resets{0};
