#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include "anyapi_equipment_v1.h"
#include "../balance_contract.h"
#include "../balance_assembly_contract.h"
static uintptr_t g_p34_frontend;namespace platform {static HWND game_window;}
static constexpr int ANY_LOG_INFO=0,ANY_LOG_WARN=1;
static void log_line(int,const char*,const char*){}
static bool safe_read_memory(uintptr_t p,void* out,size_t n){if(!p)return false;memcpy(out,(void*)p,n);return true;}
static bool p34_cell(uintptr_t,size_t,uintptr_t&){return false;}
static std::vector<uintptr_t> scan_exact(const unsigned char*,size_t){return {};}
static bool valid_state=true,valid_target=true,valid_transform=true;static unsigned original_calls,lookup_calls;
static unsigned char scene[13440]{},vehicle[2448]{},vehicle_b[2448]{},tool[80]{},state[16]{};
static void lookup_vehicle(uintptr_t* out,void* container,const int32_t* id){assert(container==scene+0xa0&&(*id==7||*id==8));++lookup_calls;*out=valid_target?uintptr_t(*id==7?vehicle:vehicle_b):0;}
static void get_transform(double* out,void* self){assert(self==vehicle||self==vehicle_b);double m[12]={1,0,0,0,1,0,0,0,1,self==vehicle?10.:14.,20,30};memcpy(out,m,sizeof(m));}
static void get_bounds(void* out,void* self){assert(self==vehicle||self==vehicle_b);double bounds[6]={-2,0,-3,2,4,3};memcpy(out,bounds,sizeof(bounds));}
static void overlay_original(void*,void*,void*,void*,const bool*,const int32_t*,void*){++original_calls;}
static void state_overlay(){}
static unsigned char server_component[600]{};
static bool p34_virtual(uintptr_t object,uintptr_t slot,uintptr_t& fn){
 if(object==uintptr_t(server_component)&&slot==78*8){fn=uintptr_t(balance_assembly_contract::LINK_2);return true;}
 if(object==uintptr_t(state)){fn=uintptr_t(state_overlay);return valid_state;}
 if(object==uintptr_t(scene)+0xa0){assert(slot==16);fn=uintptr_t(lookup_vehicle);return true;}
 if((object==uintptr_t(vehicle)||object==uintptr_t(vehicle_b))&&slot==104){fn=uintptr_t(get_transform);return valid_transform;}
 if((object==uintptr_t(vehicle)||object==uintptr_t(vehicle_b))&&slot==120){fn=uintptr_t(get_bounds);return true;}
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
static void mass(double* out,void* object){*out=object==vehicle+0x4d0?100.:25.;}
static void project(AnyBalanceScreenV1* out,void*,const AnyBalancePointV1* world,const double* aspect){assert(*aspect>0);*out={world->x*.01,world->y*.01,world->z*.01};}
static unsigned char actor[2048]{},klass[256]{};static uintptr_t table[20]{};
static void selected_lookup(uintptr_t* out,void* inventory,const int32_t* id){assert(inventory==actor+0x5c8&&*id==1);*out=uintptr_t(tool);}
int main(){using namespace balance_runtime;
 platform::game_window=CreateWindowExW(0,L"STATIC",L"Balance fixture",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(platform::game_window);
 uintptr_t state_ptr=uintptr_t(state);memcpy(tool+0x40,&state_ptr,8);int32_t id=7;memcpy(state+8,&id,4);memcpy(vehicle+8,&id,4);AnyBalancePointV1 centre{1,2,-1};memcpy(vehicle+0x408,&centre,sizeof(centre));
 vehicle_slot=16;transform_slot=104;state_slot=24;project_fn=uintptr_t(project);context=1;ready=true;
 assert(capture_inner(tool,nullptr,scene));AnyCreationBalanceSnapshotV1 out;assert(store.copy(&out,GetTickCount64()));assert(out.vehicle_id==7&&out.height_m==2&&out.offset_x_m==1&&out.centre_world.x==11&&out.centre_world.z==29&&!(out.valid_fields&ANY_BALANCE_BODY_MASS));
 valid_state=false;assert(!capture_inner(tool,nullptr,scene));valid_state=true;valid_target=false;assert(!capture_inner(tool,nullptr,scene));valid_target=true;valid_transform=false;assert(!capture_inner(tool,nullptr,scene));valid_transform=true;
 // Capture authoritative hinge links from native grid/component vectors on the
 // owning tick. Only copied IDs are retained; no server pointer reaches rendering.
 unsigned char server_vehicle[2928]{},server_other[2928]{},server_grid[192]{},other_component[600]{};
 memcpy(server_vehicle+64,&id,4);int32_t other_id=8;memcpy(server_other+64,&other_id,4);
 uintptr_t grid_refs[2]={0,uintptr_t(server_grid)},component_refs[2]={0,uintptr_t(server_component)};
 NativeVector grid_vector{uintptr_t(grid_refs),0,1,1,16,0,0},component_vector{uintptr_t(component_refs),0,1,1,16,0,0};
 memcpy(server_vehicle+96,&grid_vector,sizeof(grid_vector));memcpy(server_grid+96,&component_vector,sizeof(component_vector));
 uintptr_t other_component_ptr=uintptr_t(other_component),other_parent_ptr=uintptr_t(server_other);
 memcpy(server_component+528,&other_component_ptr,8);memcpy(other_component+320,&other_parent_ptr,8);
 allowed_item=1;sample_links(server_vehicle,scene);assert(links[7].ids==std::vector<int32_t>{8});
 links.clear();allowed_item=0;
 // A hover on a bare body has no component target, but still resolves the creation.
 unsigned char hovered[272]{};int32_t type=1;memcpy(hovered,&type,4);memcpy(hovered+8,&id,4);
 NativeVector hv{uintptr_t(hovered),0,1,1,272,0,0};memcpy(scene+10672+8,&hv,sizeof(hv));int32_t absent=-1;memcpy(state+8,&absent,4);
 assert(capture_inner(tool,nullptr,scene)&&store.copy(&out,GetTickCount64())&&out.vehicle_id==7);
 memcpy(state+8,&id,4);
 // Connected bodies use mass-weighted world positions and a deterministic chassis
 // frame, regardless of which door/body the Properties Tool currently hovers.
 int32_t id_b=8;memcpy(vehicle_b+8,&id_b,4);memcpy(vehicle_b+0x408,&centre,sizeof(centre));
 uintptr_t physics=1;memcpy(vehicle+0x4d8,&physics,8);memcpy(vehicle_b+0x4d8,&physics,8);mass_fn=uintptr_t(mass);
 unsigned char client[4000]{};client[3748]=1;
 links_scene=123;auto now=GetTickCount64();links[7]={now,{8}};links[8]={now,{}};
 assert(capture_inner(tool,nullptr,scene,client)&&store.copy(&out,GetTickCount64()));
 assert(out.body_count==2&&out.body_mass_kg==125&&std::abs(out.centre_world.x-11.8)<1e-9&&out.vehicle_id==7&&(out.valid_fields&ANY_BALANCE_CONNECTED_CREATION));
 assembly_ready=true;auto world=out.centre_world;memcpy(state+8,&id_b,4);
 assert(capture_inner(tool,nullptr,scene,client)&&store.copy(&out,GetTickCount64())&&out.vehicle_id==7&&out.centre_world.x==world.x&&out.centre_world.z==world.z);
 client[3748]=0;assert(capture_inner(tool,nullptr,scene,client)&&store.copy(&out,GetTickCount64())&&out.body_count==1&&!(out.valid_fields&ANY_BALANCE_CONNECTED_CREATION));
 links[8].tick=now-1000;client[3748]=1;assert(assembly_ids(7,true).empty());assembly_ready=false;links.clear();mass_fn=0;memcpy(state+8,&id,4);
 // A disconnected creation is not grouped just because its bounding box is nearby.
 assert(assembly_ids(7,true)==std::vector<int32_t>{7});
 // Native vectors can have a wrapped first element; reject malformed descriptors.
 uintptr_t items[3]={10,20,30};NativeVector wrapped{uintptr_t(items),2,2,3,8,0,0};std::vector<uintptr_t> values;
 assert(vector_items(uintptr_t(&wrapped),8,values)&&values==std::vector<uintptr_t>({30,10}));wrapped.size=16;values.clear();assert(!vector_items(uintptr_t(&wrapped),8,values));
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
