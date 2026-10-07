#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include "anyapi_equipment_v1.h"
#include "../balance_contract.h"
static uintptr_t g_p34_frontend;namespace platform {static HWND game_window;}
static constexpr int ANY_LOG_INFO=0,ANY_LOG_WARN=1;
static void log_line(int,const char*,const char*){}
static bool safe_read_memory(uintptr_t p,void* out,size_t n){if(!p)return false;memcpy(out,(void*)p,n);return true;}
static bool p34_cell(uintptr_t,size_t,uintptr_t&){return false;}
static std::vector<uintptr_t> scan_exact(const unsigned char*,size_t){return {};}
static bool valid_state=true,valid_target=true,valid_transform=true;static unsigned original_calls,lookup_calls;
static unsigned char scene[512]{},vehicle[2448]{},tool[80]{},state[16]{};
static void lookup_vehicle(uintptr_t* out,void* container,const int32_t* id){assert(container==scene+0xa0&&*id==7);++lookup_calls;*out=valid_target?uintptr_t(vehicle):0;}
static void get_transform(double* out,void* self){assert(self==vehicle);double m[12]={1,0,0,0,1,0,0,0,1,10,20,30};memcpy(out,m,sizeof(m));}
static void get_bounds(void* out,void* self){assert(self==vehicle);double bounds[6]={-2,0,-3,2,4,3};memcpy(out,bounds,sizeof(bounds));}
static void overlay_original(void*,void*,void*,void*,const bool*,const int32_t*,void*){++original_calls;}
static void state_overlay(){}
static bool p34_virtual(uintptr_t object,uintptr_t slot,uintptr_t& fn){
 if(object==uintptr_t(state)){fn=uintptr_t(state_overlay);return valid_state;}
 if(object==uintptr_t(scene)+0xa0){assert(slot==16);fn=uintptr_t(lookup_vehicle);return true;}
 if(object==uintptr_t(vehicle)&&slot==104){fn=uintptr_t(get_transform);return valid_transform;}
 if(object==uintptr_t(vehicle)&&slot==120){fn=uintptr_t(get_bounds);return true;}
 return false;
}
template<size_t N>static bool p34_body(uintptr_t fn,const unsigned char(&expected)[N]){using namespace balance_contract;
 return (fn==uintptr_t(state_overlay)&&+expected==+HOVER_OVERLAY)||(fn==uintptr_t(lookup_vehicle)&&+expected==+VEHICLE_LOOKUP)||(fn==uintptr_t(get_transform)&&+expected==+TRANSFORM)||(fn==uintptr_t(get_bounds)&&+expected==+BOUNDS)||(fn==uintptr_t(overlay_original)&&+expected==+PROPERTY_OVERLAY);
}
namespace equipment_runtime {
 using Lookup=void(*)(uintptr_t*,void*,const int32_t*);static uintptr_t lookup;
 static AnyEquipmentSnapshotV1 equipped;static bool copy(AnyEquipmentSnapshotV1* out){*out=equipped;return true;}
 static const AnyEquipmentV1 api{sizeof(AnyEquipmentV1),1,copy,nullptr,nullptr};
}
#include "../anyapi_creation_balance.inc"
static void project(AnyBalanceScreenV1* out,void*,const AnyBalancePointV1* world,const double* aspect){assert(*aspect>0);*out={world->x*.01,world->y*.01,world->z*.01};}
static unsigned char actor[2048]{},klass[256]{};static uintptr_t table[20]{};
static void selected_lookup(uintptr_t* out,void* inventory,const int32_t* id){assert(inventory==actor+0x5c8&&*id==1);*out=uintptr_t(tool);}
int main(){using namespace balance_runtime;
 platform::game_window=CreateWindowExW(0,L"STATIC",L"Balance fixture",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(platform::game_window);
 uintptr_t state_ptr=uintptr_t(state);memcpy(tool+0x40,&state_ptr,8);int32_t id=7;memcpy(state+8,&id,4);memcpy(vehicle+8,&id,4);AnyBalancePointV1 centre{1,2,-1};memcpy(vehicle+0x408,&centre,sizeof(centre));
 vehicle_slot=16;transform_slot=104;state_slot=24;project_fn=uintptr_t(project);context=1;ready=true;
 assert(capture_inner(tool,nullptr,scene));AnyCreationBalanceSnapshotV1 out;assert(store.copy(&out,GetTickCount64()));assert(out.vehicle_id==7&&out.height_m==2&&out.offset_x_m==1&&out.centre_world.x==11&&out.centre_world.z==29&&!(out.valid_fields&ANY_BALANCE_BODY_MASS));
 valid_state=false;assert(!capture_inner(tool,nullptr,scene));valid_state=true;valid_target=false;assert(!capture_inner(tool,nullptr,scene));valid_target=true;valid_transform=false;assert(!capture_inner(tool,nullptr,scene));valid_transform=true;
 // A real writable shared call cell verifies forwarding and authority rejection.
 auto cell=(void**)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(cell);*cell=(void*)overlay_original;assert(overlay_hook.install(cell,(void*)observe));allowed_item=uintptr_t(tool);allowed_scene=uintptr_t(scene);allowed_id=1;
 bool authority=false;int32_t active=1;observe(tool,nullptr,nullptr,scene,&authority,&active,nullptr);assert(original_calls==1&&!store.copy(&out,GetTickCount64()));
 authority=true;observe(tool,(void*)1,nullptr,scene,&authority,&active,nullptr);assert(original_calls==2&&store.copy(&out,GetTickCount64()));
 valid_target=false;observe(tool,(void*)1,nullptr,scene,&authority,&active,nullptr);assert(original_calls==3&&!store.copy(&out,GetTickCount64()));valid_target=true;
 active=2;observe(tool,(void*)1,nullptr,scene,&authority,&active,nullptr);assert(original_calls==4&&!store.copy(&out,GetTickCount64()));active=1;
 // Equipping a different tool clears a previously visible marker immediately
 // on the local actor's tick. A remote actor cannot replace local selection.
 uintptr_t local=uintptr_t(actor),class_ptr=uintptr_t(klass),table_ptr=uintptr_t(table);memcpy(scene+0x188,&local,8);memcpy(tool,&class_ptr,8);memcpy(klass+0x80,&table_ptr,8);table[12]=uintptr_t(cell);
 equipment_runtime::lookup=uintptr_t(selected_lookup);auto& equipped=equipment_runtime::equipped;equipped.count=1;equipped.selected_slot=0;equipped.slots[0].slot_index=0;equipped.slots[0].item_id=1;strcpy_s(equipped.slots[0].definition_id,"vehicle_editor_properties");
 tick_inner(scene,actor);assert(allowed_item==uintptr_t(tool)&&allowed_id==1);observe(tool,(void*)1,nullptr,scene,&authority,&active,nullptr);assert(store.copy(&out,GetTickCount64()));
 strcpy_s(equipped.slots[0].definition_id,"torch");tick_inner(scene,vehicle);assert(allowed_item==uintptr_t(tool));tick_inner(scene,actor);assert(!allowed_item&&!store.copy(&out,GetTickCount64()));
 equipped.count=0;tick_inner(scene,actor);assert(!allowed_item);
 overlay_hook.remove();VirtualFree(cell,0,MEM_RELEASE);DestroyWindow(platform::game_window);
}
