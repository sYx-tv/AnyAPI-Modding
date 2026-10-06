#include "../anymaker_mod_api.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include <cassert>
#include <iostream>
static void p29_capture_inventory(uintptr_t,bool){}
using LONG64=int64_t;using SRWLOCK=int;
static LONG64 InterlockedIncrement64(volatile LONG64* p) {auto value=*p+1;*p=value;return value;}
static void AcquireSRWLockShared(SRWLOCK*){}static void ReleaseSRWLockShared(SRWLOCK*){}
static AnymakerLocalPlayerStateV2 g_latest_player_state{};static bool g_have_latest_player_state=false;static SRWLOCK g_latest_player_lock=0;
struct DefinitionSnapshot {bool readable=false;char id[128]{},name[192]{},cls[128]{};};
static DefinitionSnapshot capture_definition_snapshot(uintptr_t) {DefinitionSnapshot s{};s.readable=true;std::strcpy(s.id,"fixture");return s;}
static std::string hexptr(uintptr_t p) {return std::to_string(p);}static void log_line(AnymakerLogLevel,const char*,const char*){}
struct Region {uintptr_t base;size_t size;};static std::vector<Region> regions;
static bool safe_read_memory(uintptr_t addr,void* out,size_t size) {
    for(auto region:regions) if(addr>=region.base && addr+size<=region.base+region.size) {std::memcpy(out,(void*)addr,size);return true;}
    return false;
}
static bool safe_read_native(uintptr_t addr,void* out,size_t size) {return safe_read_memory(addr,out,size);}
using InventoryFn=void(*)(uintptr_t*,void*);using ItemFn=void(*)(uintptr_t*,void*,const int32_t*,uintptr_t*,uintptr_t*);using ActorFn=void(*)(uintptr_t*,void*,const int32_t*);
static uintptr_t actor_ptr=0,inventory_ptr=0,ref_ptr=0,scene_ptr=0;static int fallback_calls=0,inventory_calls=0;static bool allow_fallback=true;
static void inventory_get(uintptr_t* out,void* actor) {assert((uintptr_t)actor==actor_ptr);++inventory_calls;*out=inventory_ptr;}
static void item_get(uintptr_t* out,void* inventory,const int32_t* id,uintptr_t*,uintptr_t*) {assert((uintptr_t)inventory==inventory_ptr && *id==7);*out=ref_ptr;}
static void actor_get(uintptr_t* out,void* container,const int32_t* id) {assert((uintptr_t)container==scene_ptr+0x250 && *id==42);++fallback_calls;*out=allow_fallback?actor_ptr:0;}
static InventoryFn g_server_actor_get_inventory=inventory_get;static ItemFn g_server_inventory_get_item=item_get;static ActorFn g_server_actor_get_by_id=actor_get;
static bool seh_server_actor_get_inventory(InventoryFn f,uintptr_t* o,void* a) {f(o,a);return true;}
static bool seh_server_inventory_get_item(ItemFn f,uintptr_t* o,void* i,const int32_t* id,uintptr_t* c,uintptr_t* e) {f(o,i,id,c,e);return true;}
static bool seh_server_actor_get_by_id(ActorFn f,uintptr_t* o,void* c,const int32_t* id) {f(o,c,id);return true;}
static volatile LONG64 g_server_use_actor_chain_ok=0;
static volatile LONG64 g_server_use_actor_fallback_attempts=0;
static volatile LONG64 g_server_use_actor_fallback_ok=0;
static volatile LONG64 g_server_use_actor_id_match=0;
static volatile LONG64 g_server_use_actor_resolved=0;
static volatile LONG64 g_server_use_definition_ok=0;
static volatile LONG64 g_server_use_inventory_getter_ok=0;
static volatile LONG64 g_server_use_inventory_offset_match=0;
static volatile LONG64 g_server_use_item_ref_direct_ok=0;
static volatile LONG64 g_server_use_item_resolved=0;
static volatile LONG64 g_server_use_local_active_match=0;
static volatile LONG64 g_server_use_local_handheld_match=0;
static volatile LONG64 g_server_use_lookup_call_ok=0;
static volatile LONG64 g_server_use_peer_actor_native_ok=0;
static volatile LONG64 g_server_use_peer_actor_nonzero=0;
static volatile LONG64 g_server_use_peer_actor_rpm_ok=0;
static volatile LONG64 g_server_use_peer_state_native_ok=0;
static volatile LONG64 g_server_use_peer_state_nonzero=0;
static volatile LONG64 g_server_use_peer_state_rpm_ok=0;
static volatile LONG64 g_server_use_probe_logs=0;
static volatile LONG64 g_server_use_resolve_attempts=0;
#include "../phase27_server_use_resolution.inc"
template<size_t N> static void register_region(unsigned char (&b)[N]) {regions.push_back({(uintptr_t)b,N});}
static void pointer_at(unsigned char* b,size_t off,uintptr_t value) {std::memcpy(b+off,&value,8);}
int main() {
    unsigned char server[512]{},peer[2048]{},state[8192]{},actor[4096]{},decoy[256]{},ref[128]{},item[256]{},scene[1024]{};
    register_region(server);register_region(peer);register_region(state);register_region(actor);register_region(decoy);register_region(ref);register_region(item);register_region(scene);
    actor_ptr=(uintptr_t)actor;inventory_ptr=actor_ptr+0x780;ref_ptr=(uintptr_t)ref;scene_ptr=(uintptr_t)scene;
    int32_t id=42,bad_id=100;std::memcpy(actor+0x40,&id,4);std::memcpy(decoy+0x40,&bad_id,4);
    pointer_at(peer,0x698,(uintptr_t)state);pointer_at(state,0x1998,actor_ptr);pointer_at(state,0x19B8,(uintptr_t)decoy);
    pointer_at(server,0xA0,scene_ptr);pointer_at(ref,0x20,(uintptr_t)item);pointer_at(item,0x88,1);
    auto first=resolve_server_use_item(server,peer,42,7);assert(first.resolved && first.server_actor==actor_ptr && first.server_item==(uintptr_t)item);assert(fallback_calls==0 && g_server_use_inventory_offset_match==1);
    pointer_at(state,0x1998,(uintptr_t)decoy);auto second=resolve_server_use_item(server,peer,42,7);assert(second.resolved && fallback_calls==1);
    allow_fallback=false;int previous_calls=inventory_calls;auto third=resolve_server_use_item(server,peer,42,7);assert(!third.resolved && inventory_calls==previous_calls);
    std::cout<<"PASS: current peer offset resolves; wrong readable actor falls back; rejected ID never reaches inventory getter\n";
}
