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
#include "../balance_fluid_contract.h"
static uintptr_t g_p34_frontend;static unsigned memory_reads;namespace platform {static HWND game_window;}
static constexpr int ANY_LOG_INFO=0,ANY_LOG_WARN=1;
static void log_line(int,const char*,const char*){}
static bool safe_read_memory(uintptr_t p,void* out,size_t n){++memory_reads;if(!p)return false;memcpy(out,(void*)p,n);return true;}
static bool safe_read_native(uintptr_t p,void* out,size_t n){return safe_read_memory(p,out,n);}
static void tank_tick(){}static void tank_in_scene(bool* out,void* physics);
static bool p34_cell(uintptr_t owner,size_t offset,uintptr_t& target){if(owner==uintptr_t(tank_tick)&&offset==balance_fluid_contract::LIQUID_TANK_ADDED_CELL){target=uintptr_t(tank_in_scene);return true;}return false;}
static std::vector<uintptr_t> scan_exact(const unsigned char*,size_t){return {};}
static bool valid_state=true,valid_target=true,valid_transform=true;static unsigned original_calls,lookup_calls,bounds_calls,mass_calls;
static unsigned char scene[13440]{},vehicle[2448]{},vehicle_b[2448]{},tool[80]{},state[16]{};
static void lookup_vehicle(uintptr_t* out,void* container,const int32_t* id){assert(container==scene+0xa0&&(*id==7||*id==8));++lookup_calls;*out=valid_target?uintptr_t(*id==7?vehicle:vehicle_b):0;}
static void get_transform(double* out,void* self){assert(self==vehicle||self==vehicle_b);double m[12]={1,0,0,0,1,0,0,0,1,self==vehicle?10.:14.,20,30};memcpy(out,m,sizeof(m));}
static void get_bounds(void* out,void* self){++bounds_calls;assert(self==vehicle||self==vehicle_b);double bounds[6]={-2,0,-3,2,4,3};memcpy(out,bounds,sizeof(bounds));}
static void overlay_original(void*,void*,void*,void*,const bool*,const int32_t*,void*){++original_calls;}
static void state_overlay(){}
static unsigned char server_component[600]{},tank_component[900]{};static bool tank_added_to_scene=true;
static void tank_in_scene(bool* out,void* physics){assert(physics==tank_component+balance_fluid_contract::FLUID_PHYSICS);*out=tank_added_to_scene;}
static bool p34_virtual(uintptr_t object,uintptr_t slot,uintptr_t& fn){
 if(object==uintptr_t(tank_component)&&slot==balance_fluid_contract::LIQUID_TANK_TICK_SLOT*8){fn=uintptr_t(tank_tick);return true;}
 if(object==uintptr_t(server_component)&&slot==78*8){fn=uintptr_t(balance_assembly_contract::LINK_2);return true;}
 if(object==uintptr_t(state)){fn=uintptr_t(state_overlay);return valid_state;}
 if(object==uintptr_t(scene)+0xa0){assert(slot==16);fn=uintptr_t(lookup_vehicle);return true;}
 if((object==uintptr_t(vehicle)||object==uintptr_t(vehicle_b))&&slot==104){fn=uintptr_t(get_transform);return valid_transform;}
 if((object==uintptr_t(vehicle)||object==uintptr_t(vehicle_b))&&slot==120){fn=uintptr_t(get_bounds);return true;}
 return false;
}
template<size_t N>static bool p34_body(uintptr_t fn,const unsigned char(&expected)[N]){using namespace balance_contract;
 return (fn==uintptr_t(tank_tick)&&+expected==+balance_fluid_contract::LIQUID_TANK_TICK)||(fn==uintptr_t(state_overlay)&&+expected==+HOVER_OVERLAY)||(fn==uintptr_t(lookup_vehicle)&&+expected==+VEHICLE_LOOKUP)||(fn==uintptr_t(get_transform)&&+expected==+TRANSFORM)||(fn==uintptr_t(get_bounds)&&+expected==+BOUNDS)||(fn==uintptr_t(overlay_original)&&+expected==+PROPERTY_OVERLAY);
}
namespace equipment_runtime {
 using Lookup=void(*)(uintptr_t*,void*,const int32_t*);static uintptr_t lookup;
 static AnyEquipmentSnapshotV1 equipped;static bool copy(AnyEquipmentSnapshotV1* out){*out=equipped;return true;}
 static const AnyEquipmentV1 api{sizeof(AnyEquipmentV1),1,copy,nullptr,nullptr};
}
#include "../anyapi_creation_balance.inc"
static void mass(double* out,void* object){if(object==tank_component+balance_fluid_contract::FLUID_PHYSICS){*out=40;return;}++mass_calls;*out=object==vehicle+0x4d0?100.:25.;}
static void fluid_transform(double* out,void* object){assert(object==tank_component+balance_fluid_contract::FLUID_PHYSICS);double m[12]={1,0,0,0,1,0,0,0,1,20,22,29};memcpy(out,m,sizeof(m));}
static void project(AnyBalanceScreenV1* out,void*,const AnyBalancePointV1* world,const double* aspect){assert(*aspect>0);*out={world->x*.01,world->y*.01,world->z*.01};}
static unsigned char camera[720]{};
static unsigned char actor[2048]{},klass[256]{};static uintptr_t table[20]{};
static void selected_lookup(uintptr_t* out,void* inventory,const int32_t* id){assert(inventory==actor+0x5c8&&*id==1);*out=uintptr_t(tool);}
int main(){using namespace balance_runtime;
 platform::game_window=CreateWindowExW(0,L"STATIC",L"Balance fixture",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(platform::game_window);
 double camera_transform[12]={1,0,0,0,1,0,0,0,1,0,0,0},projection[16]={2,0,0,0,0,3,0,0,0,0,0,1,0,0,1,0};memcpy(camera+8,camera_transform,sizeof(camera_transform));memcpy(camera+416,projection,sizeof(projection));
 uintptr_t state_ptr=uintptr_t(state);memcpy(tool+0x40,&state_ptr,8);int32_t id=7;memcpy(state+8,&id,4);memcpy(vehicle+8,&id,4);AnyBalancePointV1 centre{1,2,-1};memcpy(vehicle+0x408,&centre,sizeof(centre));
 vehicle_slot=16;transform_slot=104;state_slot=24;project_fn=uintptr_t(project);context=1;ready=true;
 assert(capture_inner(tool,camera,scene));AnyCreationBalanceSnapshotV1 out;assert(store.copy(&out,GetTickCount64()));assert(out.vehicle_id==7&&out.height_m==2&&out.offset_x_m==1&&out.centre_world.x==11&&out.centre_world.z==29&&!(out.valid_fields&ANY_BALANCE_BODY_MASS));
 valid_state=false;assert(!capture_inner(tool,camera,scene));valid_state=true;valid_target=false;assert(!capture_inner(tool,camera,scene));valid_target=true;valid_transform=false;assert(!capture_inner(tool,camera,scene));valid_transform=true;
 // Capture authoritative hinge links from native grid/component vectors on the
 // owning tick. Only copied IDs are retained; no server pointer reaches rendering.
 unsigned char server_vehicle[2928]{},server_other[2928]{},server_grid[192]{},other_component[600]{};
 memcpy(server_vehicle+64,&id,4);int32_t other_id=8;memcpy(server_other+64,&other_id,4);
 uintptr_t grid_refs[2]={0,uintptr_t(server_grid)},component_refs[2]={0,uintptr_t(server_component)};
 NativeVector grid_vector{uintptr_t(grid_refs),0,1,1,16,0,0},component_vector{uintptr_t(component_refs),0,1,1,16,0,0};
 memcpy(server_vehicle+96,&grid_vector,sizeof(grid_vector));memcpy(server_grid+96,&component_vector,sizeof(component_vector));
 uintptr_t other_component_ptr=uintptr_t(other_component),other_parent_ptr=uintptr_t(server_other);
 memcpy(server_component+528,&other_component_ptr,8);memcpy(other_component+320,&other_parent_ptr,8);
 requested={7};allowed_item=1;sample_links(server_vehicle,scene);assert(links[7].ids==std::vector<int32_t>{8}&&links[7].fluids.empty());
 // A liquid tank's server-only fluid body is copied as mass plus world position,
 // only when the native getters resolve and the body is in the physics scene.
 uintptr_t tank_refs[4]={0,uintptr_t(server_component),0,uintptr_t(tank_component)};NativeVector tank_vector{uintptr_t(tank_refs),0,2,2,16,0,0};
 memcpy(server_grid+96,&tank_vector,sizeof(tank_vector));uintptr_t fluid_body=1;memcpy(tank_component+balance_fluid_contract::FLUID_PHYSICS+8,&fluid_body,8);
 links.clear();sample_links(server_vehicle,scene);assert(links[7].fluids.empty()); // getters unresolved
 mass_fn=uintptr_t(mass);fluid_transform_fn=uintptr_t(fluid_transform);links.clear();sample_links(server_vehicle,scene);
 assert(links[7].ids==std::vector<int32_t>{8}&&links[7].fluids.size()==1&&links[7].fluids[0].mass==40&&links[7].fluids[0].world.x==20);
 tank_added_to_scene=false;links.clear();sample_links(server_vehicle,scene);assert(links[7].fluids.empty());tank_added_to_scene=true;
 mass_fn=0;fluid_transform_fn=0;memcpy(server_grid+96,&component_vector,sizeof(component_vector));
 int32_t unrelated=99;memcpy(server_other+64,&unrelated,4);auto reads=memory_reads;sample_links(server_other,scene);assert(memory_reads==reads+1);
 links.clear();allowed_item=0;
 // A hover on a bare body has no component target, but still resolves the creation.
 unsigned char hovered[272]{};int32_t type=1;memcpy(hovered,&type,4);memcpy(hovered+8,&id,4);
 NativeVector hv{uintptr_t(hovered),0,1,1,272,0,0};memcpy(scene+10672+8,&hv,sizeof(hv));int32_t absent=0;memcpy(state+8,&absent,4);
 assert(capture_inner(tool,camera,scene)&&store.copy(&out,GetTickCount64())&&out.vehicle_id==7);
 // The real native tick uses zero, not -1. Both unset forms take the fallback.
 absent=-1;memcpy(state+8,&absent,4);assert(capture_inner(tool,camera,scene));
 absent=0;memcpy(state+8,&absent,4);
 // An occluding non-vehicle must not reveal the vehicle behind it.
 type=3;memcpy(hovered,&type,4);assert(!capture_inner(tool,camera,scene));
 type=1;memcpy(hovered,&type,4);memcpy(hovered+8,&absent,4);assert(!capture_inner(tool,camera,scene));
 memcpy(hovered+8,&id,4);
 // Empty and wrapped hover vectors follow the native first-entry convention.
 hv.count=0;memcpy(scene+10672+8,&hv,sizeof(hv));assert(!capture_inner(tool,camera,scene));
 unsigned char wrapped_hover[544]{};memcpy(wrapped_hover+272,hovered,272);
 hv={uintptr_t(wrapped_hover),1,1,2,272,0,0};memcpy(scene+10672+8,&hv,sizeof(hv));
 assert(capture_inner(tool,camera,scene)&&store.copy(&out,GetTickCount64())&&out.vehicle_id==7);
 hv={uintptr_t(hovered),0,1,1,272,0,0};memcpy(scene+10672+8,&hv,sizeof(hv));
 memcpy(state+8,&id,4);
 // Connected bodies use mass-weighted world positions and a deterministic chassis
 // frame, regardless of which door/body the Properties Tool currently hovers.
 int32_t id_b=8;memcpy(vehicle_b+8,&id_b,4);memcpy(vehicle_b+0x408,&centre,sizeof(centre));
 uintptr_t physics=1;memcpy(vehicle+0x4d8,&physics,8);memcpy(vehicle_b+0x4d8,&physics,8);mass_fn=uintptr_t(mass);
 unsigned char client[4000]{};client[3748]=1;
 body_properties.clear();requested={7,8};links_scene=123;auto now=GetTickCount64();links[7]={now,{8}};links[8]={now,{}};
 assert(capture_inner(tool,camera,scene,client)&&store.copy(&out,GetTickCount64()));
 assert(out.body_count==2&&out.body_mass_kg==125&&std::abs(out.centre_world.x-11.8)<1e-9&&out.vehicle_id==7&&(out.valid_fields&ANY_BALANCE_CONNECTED_CREATION));
 assembly_ready=true;auto world=out.centre_world;memcpy(state+8,&id_b,4);
 assert(capture_inner(tool,camera,scene,client)&&store.copy(&out,GetTickCount64())&&out.vehicle_id==7&&out.centre_world.x==world.x&&out.centre_world.z==world.z);
 // Camera changes reproject a fixed world centre using the exact native matrix;
 // neither the physics mass nor connection scan is repeated every presentation.
 auto previous=out.centre;double shifted=2;memcpy(camera+8+72,&shifted,8);
 auto calls=bounds_calls,weights=mass_calls;
 for(int frame=0;frame<100;++frame)assert(capture_inner(tool,camera,scene,client));
 assert(store.copy(&out,GetTickCount64())&&out.centre_world.x==world.x&&out.centre.x!=previous.x&&bounds_calls==calls&&mass_calls==weights);
 shifted=0;memcpy(camera+8+72,&shifted,8);
 // Fluid from fresh server samples moves the centre and adds to the total mass.
 assert(!(out.valid_fields&ANY_BALANCE_FLUID_MASS));fluid_transform_fn=uintptr_t(fluid_transform);now=GetTickCount64();
 links[7]={now,{8},{{40,{20,22,29}}}};links[8]={now,{}};
 assert(capture_inner(tool,camera,scene,client)&&store.copy(&out,GetTickCount64()));
 assert((out.valid_fields&ANY_BALANCE_FLUID_MASS)&&out.body_mass_kg==165&&std::abs(out.centre_world.x-(1100.+375+800)/165)<1e-9&&out.centre_world.z==world.z);
 // A stale server sample for any body leaves fluid out rather than adding part of it.
 links[8].tick=now-1500;links[8].ids={7};std::vector<FluidMass> partial;assert(!assembly_fluids({7,8},partial)&&partial.size()<=1);
 links[7]={now,{8}};links[8]={now,{}};fluid_transform_fn=0;
 client[3748]=0;assert(capture_inner(tool,camera,scene,client)&&store.copy(&out,GetTickCount64())&&out.body_count==1&&!(out.valid_fields&ANY_BALANCE_CONNECTED_CREATION));
 links[8].tick=now-2000;client[3748]=1;assert(assembly_ids(7,true).empty());assembly_ready=false;links.clear();body_properties.clear();mass_fn=0;memcpy(state+8,&id,4);
 // A disconnected creation is not grouped just because its bounding box is nearby.
 assert(assembly_ids(7,true)==std::vector<int32_t>{7});
 // Native vectors can have a wrapped first element; reject malformed descriptors.
 uintptr_t items[3]={10,20,30};NativeVector wrapped{uintptr_t(items),2,2,3,8,0,0};std::vector<uintptr_t> values;
 assert(vector_items(uintptr_t(&wrapped),8,values)&&values==std::vector<uintptr_t>({30,10}));wrapped.size=16;values.clear();assert(!vector_items(uintptr_t(&wrapped),8,values));
 // A real writable shared call cell verifies forwarding and authority rejection.
 auto cell=(void**)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(cell);*cell=(void*)overlay_original;assert(overlay_hook.install(cell,(void*)observe));allowed_item=uintptr_t(tool);allowed_scene=uintptr_t(scene);allowed_id=1;
 bool authority=false;int32_t active=1;observe(tool,nullptr,nullptr,scene,&authority,&active,nullptr);assert(original_calls==1&&!store.copy(&out,GetTickCount64()));
 authority=true;observe(tool,camera,nullptr,scene,&authority,&active,nullptr);assert(original_calls==2&&store.copy(&out,GetTickCount64()));
 valid_target=false;observe(tool,camera,nullptr,scene,&authority,&active,nullptr);assert(original_calls==3&&!store.copy(&out,GetTickCount64()));valid_target=true;
 active=2;observe(tool,camera,nullptr,scene,&authority,&active,nullptr);assert(original_calls==4&&!store.copy(&out,GetTickCount64()));active=1;
 // Equipping a different tool clears a previously visible marker immediately
 // on the local actor's tick. A remote actor cannot replace local selection.
 uintptr_t local=uintptr_t(actor),class_ptr=uintptr_t(klass),table_ptr=uintptr_t(table);memcpy(scene+0x188,&local,8);memcpy(tool,&class_ptr,8);memcpy(klass+0x80,&table_ptr,8);table[12]=uintptr_t(cell);
 equipment_runtime::lookup=uintptr_t(selected_lookup);auto& equipped=equipment_runtime::equipped;equipped.count=1;equipped.selected_slot=0;equipped.slots[0].slot_index=0;equipped.slots[0].item_id=1;strcpy_s(equipped.slots[0].definition_id,"vehicle_editor_properties");
 tick_inner(scene,actor);assert(allowed_item==uintptr_t(tool)&&allowed_id==1);observe(tool,camera,nullptr,scene,&authority,&active,nullptr);assert(store.copy(&out,GetTickCount64()));
 // HUD projection must follow the final renderer camera even when the tool
 // camera and world geometry do not change between renders.
 auto fixture_window=platform::game_window;platform::game_window=GetForegroundWindow();
 unsigned char frontend[2200]{},render_scene[1024]{};int gameplay=1;memcpy(frontend+0x828,&gameplay,4);g_p34_frontend=uintptr_t(frontend);
 assert(!api.copy(&out));
 double final_matrix[16]={2,0,0,0,0,3,0,0,0,0,0,1,-2,0,0,0};memcpy(render_scene+0x240,final_matrix,sizeof(final_matrix));
 anyapi_creation_balance_render_camera(render_scene);assert(api.copy(&out));auto final_x=out.centre.x;auto final_world=out.centre_world;
 final_matrix[12]=-4;memcpy(render_scene+0x240,final_matrix,sizeof(final_matrix));anyapi_creation_balance_render_camera(render_scene);
 assert(api.copy(&out)&&out.centre.x<final_x&&out.centre_world.x==final_world.x&&out.centre_world.z==final_world.z);
 render_camera_tick=GetTickCount64()-151;assert(!api.copy(&out));platform::game_window=fixture_window;
 strcpy_s(equipped.slots[0].definition_id,"torch");tick_inner(scene,vehicle);assert(allowed_item==uintptr_t(tool));tick_inner(scene,actor);assert(!allowed_item&&!store.copy(&out,GetTickCount64()));
 equipped.count=0;tick_inner(scene,actor);assert(!allowed_item);
 overlay_hook.remove();VirtualFree(cell,0,MEM_RELEASE);DestroyWindow(platform::game_window);
}
