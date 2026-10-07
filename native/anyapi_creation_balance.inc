#include "balance_math.h"
#include "balance_contract.h"
#include "runtime_call_cell.h"
namespace balance_runtime {
using namespace balance_contract;
using Overlay=void(*)(void*,void*,void*,void*,const bool*,const int32_t*,void*);
using Lookup=void(*)(uintptr_t*,void*,const int32_t*);
using Transform=void(*)(double*,void*);
using Bounds=void(*)(AnyBalancePointV1*,void*);
using Project=void(*)(AnyBalanceScreenV1*,void*,const AnyBalancePointV1*,const double*);
using Mass=void(*)(double*,void*);
static balance::Store store;static anyapi_runtime::CallCellHook overlay_hook;
static std::atomic<uintptr_t> allowed_item{},allowed_scene{};
static std::atomic<int32_t> allowed_id{-1};
static std::atomic<uint64_t> context{};static std::atomic<bool> ready{},faulted{};
static uintptr_t vehicle_slot{},transform_slot{},state_slot{},mass_fn{},project_fn{};
static uintptr_t last_vehicle{};static uint64_t captures{};
static bool copy(AnyCreationBalanceSnapshotV1* out){int32_t ui=-1;
 if(!ready||faulted||!allowed_item||GetForegroundWindow()!=platform::game_window||!g_p34_frontend||!safe_read_memory(g_p34_frontend+0x828,&ui,4)||ui!=1)return false;
 return store.copy(out,GetTickCount64());
}
static const AnyCreationBalanceV1 api{sizeof(AnyCreationBalanceV1),1,copy};
static bool capture_inner(void* item,void* camera,void* scene){
 uintptr_t state{},fn{},vehicle{};int32_t id=-1;
 // ref<T> stores its object pointer in the second word; checked against the
 // Properties tool's actual overlay body, which reads item + 0x40.
 if(!safe_read_memory(uintptr_t(item)+0x40,&state,8)||!state||!p34_virtual(state,state_slot,fn)||
   (!p34_body(fn,HOVER_OVERLAY)&&!p34_body(fn,SELECT_OVERLAY))||!safe_read_memory(state+8,&id,4)||id<0)return false;
 auto container=uintptr_t(scene)+0xa0;
 if(!p34_virtual(container,vehicle_slot,fn)||!p34_body(fn,VEHICLE_LOOKUP))return false;
 ((Lookup)fn)(&vehicle,(void*)container,&id);
 if(!vehicle)return false;int32_t observed_id=-1;
 if(!safe_read_memory(vehicle+8,&observed_id,4)||observed_id!=id)return false;
 AnyCreationBalanceSnapshotV1 sample;sample.vehicle_id=id;sample.context=context;sample.sampled_tick=GetTickCount64();
 double transform[12]{};
 if(!p34_virtual(vehicle,transform_slot,fn)||!p34_body(fn,TRANSFORM))return false;
 ((Transform)fn)(transform,(void*)vehicle);
 for(double value:transform)if(!std::isfinite(value)||std::abs(value)>1e9)return false;
 if(!safe_read_memory(vehicle+0x408,&sample.centre_local,sizeof(sample.centre_local)))return false;
 AnyBalancePointV1 bounds[2];
 if(!p34_virtual(vehicle,15*8,fn)||!p34_body(fn,BOUNDS))return false;
 ((Bounds)fn)(bounds,(void*)vehicle);sample.bounds_min=bounds[0];sample.bounds_max=bounds[1];
 if(!balance::derive(sample))return false;
 sample.centre_world=balance::transform(transform,sample.centre_local);
 if(!balance::finite(sample.centre_world))return false;
 if(mass_fn){uintptr_t physics_data{};if(safe_read_memory(vehicle+0x4d8,&physics_data,8)&&physics_data){
  ((Mass)mass_fn)(&sample.body_mass_kg,(void*)(vehicle+0x4d0));
  if(std::isfinite(sample.body_mass_kg)&&sample.body_mass_kg>0&&sample.body_mass_kg<1e12)sample.valid_fields|=ANY_BALANCE_BODY_MASS;
 }}
 RECT rect{};if(!GetClientRect(platform::game_window,&rect)||rect.right<=0||rect.bottom<=0)return false;
 double aspect=double(rect.right)/rect.bottom;
 auto project=[&](AnyBalancePointV1 local,AnyBalanceScreenV1& out){auto world=balance::transform(transform,local);((Project)project_fn)(&out,camera,&world,&aspect);};
 project(sample.centre_local,sample.centre);
 auto base=sample.centre_local;base.y=sample.bounds_min.y;project(base,sample.base);
 project({(bounds[0].x+bounds[1].x)*.5,(bounds[0].y+bounds[1].y)*.5,(bounds[0].z+bounds[1].z)*.5},sample.geometric_centre);
 double axis=std::clamp(std::max({bounds[1].x-bounds[0].x,bounds[1].y-bounds[0].y,bounds[1].z-bounds[0].z})*.15,.25,1.5);
 auto p=sample.centre_local;p.x+=axis;project(p,sample.axes[0]);p=sample.centre_local;p.y+=axis;project(p,sample.axes[1]);p=sample.centre_local;p.z+=axis;project(p,sample.axes[2]);
 for(unsigned i=0;i<8;++i)project({i&1?bounds[1].x:bounds[0].x,i&2?bounds[1].y:bounds[0].y,i&4?bounds[1].z:bounds[0].z},sample.corners[i]);
 AnyEquipmentSnapshotV1 equipped;if(equipment_runtime::api.copy(&equipped))for(unsigned i=0;i<equipped.count;++i)if(equipped.slots[i].slot_index==equipped.selected_slot)sample.tool_item_id=equipped.slots[i].item_id;
 store.publish(sample);++captures;
 if(last_vehicle!=vehicle){last_vehicle=vehicle;log_line(ANY_LOG_INFO,"framework",("BALANCE sampled=1 vehicle="+std::to_string(id)+" native_weighted_centre=1 body_mass_available="+std::to_string(bool(sample.valid_fields&ANY_BALANCE_BODY_MASS))).c_str());}
 return true;
}
static bool capture(void* item,void* camera,void* scene){__try{return capture_inner(item,camera,scene);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static void observe(void* item,void* camera,void* client,void* scene,const bool* authority,const int32_t* active,void* overlay){
 ((Overlay)overlay_hook.original())(item,camera,client,scene,authority,active,overlay);
 if(!ready||faulted||uintptr_t(item)!=allowed_item||uintptr_t(scene)!=allowed_scene)return;
 if(!authority||!*authority||!active||*active!=allowed_id||!camera||!capture(item,camera,scene))store.clear();
}
static bool bind(uintptr_t item){uintptr_t klass{},table{},cell{},fn{};
 if(!safe_read_memory(item,&klass,8)||!klass||!safe_read_memory(klass+0x80,&table,8)||!table||!safe_read_memory(table+12*8,&cell,8)||!cell||!safe_read_memory(cell,&fn,8))return false;
 if(fn==uintptr_t(observe))return true;
 if(!p34_body(fn,PROPERTY_OVERLAY)||!safe_read_memory(fn+PROPERTY_OVERLAY_END+2*8,&state_slot,8)||state_slot%8||state_slot>0x10000)return false;
 bool result=overlay_hook.install((void**)cell,(void*)observe);if(result)log_line(ANY_LOG_INFO,"framework","BALANCE attached=1 properties_overlay=1 native_forwarding=1");return result;
}
static void tick_inner(void* scene,void* actor){uintptr_t local{};
 if(!scene||!safe_read_memory(uintptr_t(scene)+0x188,&local,8))return;if(!local)safe_read_memory(uintptr_t(scene)+0x190,&local,8);if(local!=uintptr_t(actor))return;
 AnyEquipmentSnapshotV1 snapshot;uintptr_t selected{};int32_t selected_id=-1;
 if(ready&&!faulted&&equipment_runtime::api.copy(&snapshot))for(unsigned i=0;i<snapshot.count;++i){auto& s=snapshot.slots[i];
  if(s.slot_index==snapshot.selected_slot&&!strcmp(s.definition_id,"vehicle_editor_properties")&&s.item_id>0){
   ((equipment_runtime::Lookup)equipment_runtime::lookup)(&selected,(void*)(uintptr_t(actor)+0x5c8),&s.item_id);selected_id=s.item_id;break;
  }}
 if(selected&&!bind(selected))selected=0;
 if(allowed_item!=selected||allowed_scene!=uintptr_t(scene)){store.clear();++context;last_vehicle=0;}
 allowed_scene=uintptr_t(scene);allowed_id=selected?selected_id:-1;allowed_item=selected;
}
static void tick(void* scene,void* actor){__try{tick_inner(scene,actor);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;allowed_item=0;store.clear();}}
static bool prepare(){auto hits=scan_exact(HOVER_OVERLAY,sizeof(HOVER_OVERLAY)),mass=scan_exact(MASS_OWNER,sizeof(MASS_OWNER));
 if(hits.size()!=1||!safe_read_memory(hits[0]+HOVER_OVERLAY_END,&vehicle_slot,8)||vehicle_slot%8||vehicle_slot>0x10000||
 !safe_read_memory(hits[0]+HOVER_OVERLAY_END+9*8,&transform_slot,8)||transform_slot%8||transform_slot>0x10000)return false;
 project_fn=uintptr_t(GetModuleHandleW(nullptr))+PROJECT_RVA;if(!p34_body(project_fn,PROJECT))return false;
 if(mass.size()==1)p34_cell(mass[0],MASS_OWNER_END+7*8,mass_fn);
 ready=true;log_line(ANY_LOG_INFO,"framework","BALANCE ready=1 awaiting_properties_tool=1 copied_snapshots=1 physics_writes=0");return true;
}
static DWORD WINAPI worker(void*){for(unsigned i=0;i<120;++i){if(prepare())return 0;Sleep(1000);}log_line(ANY_LOG_WARN,"framework","BALANCE unavailable=native_contract_unmatched");return 0;}
}
static const AnyCreationBalanceV1* anyapi_creation_balance_service(){return &balance_runtime::api;}
static void anyapi_creation_balance_tick(void* scene,void* actor){balance_runtime::tick(scene,actor);}
