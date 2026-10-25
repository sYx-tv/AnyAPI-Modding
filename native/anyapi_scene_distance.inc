#include "runtime_call_cell.h"
// Draw distance: scales the distance thresholds the game passes to its two
// distance-cull helpers (actors 256 m, props 1024 m, items 128 m, projectiles
// 256 m, vehicle components 128 m). Terrain and vegetation keep the game's own
// render distance. 1 leaves every call untouched.
namespace scene_effects {
static anyapi_runtime::CallCellHook cull_cell,cull_radius_cell;static std::atomic<uint64_t> distance_calls{};
// K36: the hooks run per object; a thread_local count folded in every 4096 calls replaces a seq_cst add per object.
static thread_local uint32_t distance_pending{};
static void count_distance(){if(++distance_pending>=4096){distance_calls.fetch_add(distance_pending,std::memory_order_relaxed);distance_pending=0;}}
static const unsigned char ITEM_RENDER_CULL[]={0x53,0x55,0x56,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x28,0x4c,0x89,0x44,0x24,0x30,0x4c,0x89,0x4c,0x24,0x38,0x48,0x8d,0x44,0x24,0x40,0x49};
static const unsigned char VEHICLE_RENDER[]={0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa8,0x13,0x00,0x00,0x4c,0x8b,0x9c,0x24,0x10,0x14,0x00,0x00,0x48,0x89,0x8c,0x24,0x08};
static const unsigned char CULL_BY_DISTANCE[]={0x53,0x55,0x56,0x57,0x48,0x81,0xec,0xa8,0x00,0x00,0x00,0x48,0x89,0x4c,0x24,0x50,0x48,0x89,0x54,0x24,0x58,0x4c,0x89,0x44,0x24,0x60,0x4c,0x89,0x4c,0x24,0x68,0x48};
static const unsigned char CULL_BY_DISTANCE_RADIUS[]={0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x81,0xec,0xb0,0x00,0x00,0x00,0x48,0x8b,0xb4,0x24,0x00,0x01,0x00,0x00,0x48,0x89,0x4c,0x24,0x50,0x48,0x89,0x54,0x24,0x58,0x4c};
// const f64 and vec3 arguments arrive by pointer; the bool result goes through a hidden pointer.
using CullFn=uintptr_t(*)(void*,void*,const double*,const double*);
using CullRadiusFn=uintptr_t(*)(void*,void*,const double*,const double*,const double*);
// distance_used is refreshed once a frame from the policy, so the hooks
// (called per object) stay lock-free.
static float distance_factor(){return distance_used.load(std::memory_order_relaxed);}
// SC-16: needs only an active owner (copy_owned), not the lighting cell.
static void update_distance(){auto p=copy_owned();float f=p.enabled&&distance_available?p.draw_distance:1.f;if(f!=distance_used.load(std::memory_order_relaxed)){distance_used.store(f,std::memory_order_relaxed);log_line(ANY_LOG_INFO,"framework",("SCENE_DISTANCE scale="+std::to_string(f)).c_str());}
 static uint64_t frames{};if(f!=1&&++frames%3600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_DISTANCE scale="+std::to_string(f)+" scaled_calls="+std::to_string(distance_calls.load(std::memory_order_relaxed))).c_str());}
static uintptr_t cull_hook(void* result,void* renderer,const double* threshold,const double* position){float f=distance_factor();
 if(f==1||!threshold)return ((CullFn)cull_cell.original())(result,renderer,threshold,position);count_distance();double scaled=*threshold*f;return ((CullFn)cull_cell.original())(result,renderer,&scaled,position);}
static uintptr_t cull_radius_hook(void* result,void* renderer,const double* threshold,const double* position,const double* radius){float f=distance_factor();
 if(f==1||!threshold)return ((CullRadiusFn)cull_radius_cell.original())(result,renderer,threshold,position,radius);count_distance();double scaled=*threshold*f;return ((CullRadiusFn)cull_radius_cell.original())(result,renderer,&scaled,position,radius);}
// Call cells are per callee, shared by every caller of the helper (actors,
// props, items, projectiles, vehicles); one anchor caller each finds them.
static void install_distance(void** (*resolve)(uintptr_t,size_t,size_t,const unsigned char*,size_t),std::vector<uintptr_t> (*scan)(const unsigned char*,size_t)){
 void** cull{};void** radius{};for(auto caller:scan(ITEM_RENDER_CULL,sizeof(ITEM_RENDER_CULL))){auto c=resolve(caller,272,0,CULL_BY_DISTANCE,sizeof(CULL_BY_DISTANCE));if(!c)continue;if(cull&&cull!=c){cull=nullptr;break;}cull=c;}
 for(auto caller:scan(VEHICLE_RENDER,sizeof(VEHICLE_RENDER))){auto c=resolve(caller,23512,22,CULL_BY_DISTANCE_RADIUS,sizeof(CULL_BY_DISTANCE_RADIUS));if(!c)continue;if(radius&&radius!=c){radius=nullptr;break;}radius=c;}
 bool a=cull&&cull_cell.install(cull,(void*)cull_hook),b=radius&&cull_radius_cell.install(radius,(void*)cull_radius_hook);distance_available=a||b;
 log_line(distance_available?ANY_LOG_INFO:ANY_LOG_WARN,"framework",("SCENE_DISTANCE resolved="+std::to_string(int(distance_available.load()))+" objects="+std::to_string(int(a))+" vehicle_parts="+std::to_string(int(b))).c_str());}
}
