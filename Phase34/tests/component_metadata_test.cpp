#include "../anymaker_mod_api.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
#include <iostream>
using LONG64=int64_t;
struct GeoString16{uintptr_t data;uint32_t length,capacity;};
static LONG64 InterlockedExchange64(volatile LONG64* p,LONG64 v){auto old=*p;*p=v;return old;}
static LONG64 InterlockedIncrement64(volatile LONG64* p){auto v=*p+1;*p=v;return v;}
struct Region{uintptr_t base;size_t size;};static std::vector<Region> regions;
static bool safe_read_memory(uintptr_t p,void* out,size_t n){for(auto r:regions)if(p>=r.base && n<=r.size && p-r.base<=r.size-n){std::memcpy(out,(void*)p,n);return true;}return false;}
#include "../phase273_component_metadata.inc"
template<size_t N> void region(unsigned char (&a)[N]){regions.push_back({(uintptr_t)a,N});}
int main(){
 assert(!std::strcmp(P28_REASON_NAMES[P28_EMPTY],"EMPTY_CLASS"));
 unsigned char server[512]{},client[256]{},definition[128]{},name[]={'w','h','e','e','l'};region(server);region(client);region(definition);region(name);
 uintptr_t d=(uintptr_t)definition;std::memcpy(server+0x138,&d,8);std::memcpy(client+0xB0,&d,8);GeoString16 text{(uintptr_t)name,5,5};std::memcpy(definition+0x30,&text,sizeof(text));
 char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{},existing[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
 assert(p273_read_component_class((uintptr_t)server,ANY_COMPONENT_SIDE_SERVER,cls) && !std::strcmp(cls,"wheel"));
 p273_merge_class(ANY_COMPONENT_SIDE_SERVER,existing,cls);assert(!std::strcmp(existing,"wheel") && g_p273_metadata_filled[1]==1);
 p273_merge_class(ANY_COMPONENT_SIDE_SERVER,existing,cls);assert(g_p273_metadata_match[1]==1);
 p273_merge_class(ANY_COMPONENT_SIDE_SERVER,existing,"seat");assert(!std::strcmp(existing,"wheel") && g_p273_metadata_mismatch[1]==1);
 assert(p273_read_component_class((uintptr_t)client,ANY_COMPONENT_SIDE_CLIENT,cls));
 name[2]=0;assert(!p273_read_component_class((uintptr_t)server,ANY_COMPONENT_SIDE_SERVER,cls) && !cls[0]);name[2]='e';
 text.length=0;text.capacity=0;text.data=0;std::memcpy(definition+0x30,&text,sizeof(text));
 assert(p273_read_component_class((uintptr_t)server,ANY_COMPONENT_SIDE_SERVER,cls) && !cls[0] && g_p28_reasons[1][P28_EMPTY]==1);
 assert(g_p273_definition_missing[1]==1); // previous embedded NUL failed; valid empty adds no failure
 text.length=ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX;std::memcpy(definition+0x30,&text,sizeof(text));assert(!p273_read_component_class((uintptr_t)server,ANY_COMPONENT_SIDE_SERVER,cls));
 d=0;std::memcpy(server+0x138,&d,8);assert(!p273_read_component_class((uintptr_t)server,ANY_COMPONENT_SIDE_SERVER,cls));
 assert(!p273_read_component_class(1,ANY_COMPONENT_SIDE_CLIENT,cls));
 assert(g_p28_samples[0].reason==P28_NAME_CONFLICT && !std::strcmp(g_p28_samples[0].existing,"wheel") && !std::strcmp(g_p28_samples[0].native_name,"seat"));
 assert(g_p28_sample_ready[0]==1);
 std::cout<<"PASS: production metadata backfill reads both audited offsets, compares factory names, preserves disagreements, and rejects invalid/partial identities\n";
}
