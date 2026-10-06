// Phase27 retains the public API v16 layout, but the experimental collector,
// predicates and mutation event are quarantined. No dependency replacement.
using P26ServerTickFn=void (*)(void*,void*);
static P26ServerTickFn g_p26_server_tick_original=nullptr;
static void p26_server_tick(void* scene,void* server) {
    InterlockedIncrement64(&g_p27_hook_calls[22]); p29_note_hook(22);
    p27_validate_server_virtuals(scene);
    g_p26_server_tick_original(scene,server);
}
static bool p26_register_pulse(const char*,AnymakerInventoryPulseCallback,void*) { return false; }
static AnymakerInventoryResult p26_enumerate(uint64_t,AnymakerInventoryItemV1*,size_t,size_t* count) {
    if(count) *count=0;
    return ANY_INVENTORY_UNAVAILABLE;
}
static AnymakerInventoryResult p26_can_merge(uint64_t,uint64_t,uint64_t,bool* compatible) {
    if(compatible) *compatible=false;
    return ANY_INVENTORY_UNAVAILABLE;
}
static AnymakerInventoryResult p26_request_merge(uint64_t,uint64_t,uint64_t) { return ANY_INVENTORY_UNAVAILABLE; }
