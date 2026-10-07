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
} // namespace balance_runtime
#include "balance_assembly.inc"
namespace balance_runtime {
static bool capture_inner(void* item,void* camera,void* scene,void* client=nullptr){
 uintptr_t state{},fn{},vehicle{};int32_t id=-1;
 // ref<T> stores its object pointer in the second word; checked against the
 // Properties tool's actual overlay body, which reads item + 0x40.
 if(!safe_read_memory(uintptr_t(item)+0x40,&state,8)||!state||!p34_virtual(state,state_slot,fn)||
   (!p34_body(fn,HOVER_OVERLAY)&&!p34_body(fn,SELECT_OVERLAY))||!safe_read_memory(state+8,&id,4))return false;
 // Whole-body hovers do not populate the tool's component-only target.
 if(id<0){int32_t type{};NativeVector hovered{};if(!safe_read_memory(uintptr_t(scene)+10672+8,&hovered,sizeof(hovered))||hovered.size!=272||hovered.count<1||hovered.count>64||hovered.capacity<hovered.count||hovered.capacity>4096||hovered.offset<0||hovered.offset>=hovered.capacity||!hovered.buffer)return false;
  auto interaction=hovered.buffer+uintptr_t(hovered.offset)*272;
  if(!safe_read_memory(interaction,&type,4)||(type!=1&&type!=2)||!safe_read_memory(interaction+8,&id,4)||id<0)return false;}
 auto container=uintptr_t(scene)+0xa0;
 if(!p34_virtual(container,vehicle_slot,fn)||!p34_body(fn,VEHICLE_LOOKUP))return false;
 ((Lookup)fn)(&vehicle,(void*)container,&id);
 if(!vehicle)return false;int32_t observed_id=-1;
 if(!safe_read_memory(vehicle+8,&observed_id,4)||observed_id!=id)return false;
 AnyCreationBalanceSnapshotV1 sample;sample.vehicle_id=id;sample.context=context;sample.sampled_tick=GetTickCount64();
 bool local=false;if(client)safe_read_memory(uintptr_t(client)+3748,&local,1);
 auto ids=assembly_ids(id,local);if(ids.empty())return false;
 struct Body {int32_t id;double transform[12];AnyBalancePointV1 centre,bounds[2];double mass{};};
 std::vector<Body> bodies;
 for(auto body_id:ids){uintptr_t object{};((Lookup)fn)(&object,(void*)container,&body_id);if(!object)return false;
  Body body{};body.id=body_id;uintptr_t method{};
  if(!p34_virtual(object,transform_slot,method)||!p34_body(method,TRANSFORM))return false;
  ((Transform)method)(body.transform,(void*)object);
  for(double value:body.transform)if(!std::isfinite(value)||std::abs(value)>1e9)return false;
  if(!safe_read_memory(object+0x408,&body.centre,sizeof(body.centre))||!p34_virtual(object,15*8,method)||!p34_body(method,BOUNDS))return false;
  ((Bounds)method)(body.bounds,(void*)object);
  AnyCreationBalanceSnapshotV1 check;check.centre_local=body.centre;check.bounds_min=body.bounds[0];check.bounds_max=body.bounds[1];if(!balance::derive(check))return false;
  uintptr_t physics{};if(mass_fn&&safe_read_memory(object+0x4d8,&physics,8)&&physics)((Mass)mass_fn)(&body.mass,(void*)(object+0x4d0));
  if(!std::isfinite(body.mass)||body.mass<=0||body.mass>=1e12)body.mass=0;
  bodies.push_back(body);
 }
 // A stable chassis frame: the heaviest body, with ID as deterministic tie-break.
 auto root=std::max_element(bodies.begin(),bodies.end(),[](auto& a,auto& b){return a.mass==b.mass?a.id>b.id:a.mass<b.mass;});
 auto transform=root->transform;sample.vehicle_id=root->id;sample.body_count=uint32_t(bodies.size());
 sample.bounds_min={1e9,1e9,1e9};sample.bounds_max={-1e9,-1e9,-1e9};
 AnyBalancePointV1 weighted{};bool complete_mass=true;
 for(auto& body:bodies){auto world=balance::transform(body.transform,body.centre);auto relative=balance::inverse_transform(transform,world);
  auto weight=body.mass>0?body.mass:1.;complete_mass=complete_mass&&body.mass>0;sample.body_mass_kg+=weight;
  weighted.x+=relative.x*weight;weighted.y+=relative.y*weight;weighted.z+=relative.z*weight;
  for(unsigned i=0;i<8;++i){auto p=balance::inverse_transform(transform,balance::transform(body.transform,{i&1?body.bounds[1].x:body.bounds[0].x,i&2?body.bounds[1].y:body.bounds[0].y,i&4?body.bounds[1].z:body.bounds[0].z}));
   sample.bounds_min={std::min(sample.bounds_min.x,p.x),std::min(sample.bounds_min.y,p.y),std::min(sample.bounds_min.z,p.z)};
   sample.bounds_max={std::max(sample.bounds_max.x,p.x),std::max(sample.bounds_max.y,p.y),std::max(sample.bounds_max.z,p.z)};
  }
 }
 if(bodies.size()>1&&!complete_mass)return false; // Never label a partial/unweighted assembly as its centre of mass.
 sample.centre_local={weighted.x/sample.body_mass_kg,weighted.y/sample.body_mass_kg,weighted.z/sample.body_mass_kg};
 if(complete_mass)sample.valid_fields|=ANY_BALANCE_BODY_MASS;else sample.body_mass_kg=0;
 if(local&&ids.size()>1)sample.valid_fields|=ANY_BALANCE_CONNECTED_CREATION;
 if(!balance::derive(sample))return false;
 sample.centre_world=balance::transform(transform,sample.centre_local);if(!balance::finite(sample.centre_world))return false;
 AnyBalancePointV1 bounds[2]={sample.bounds_min,sample.bounds_max};
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
 if(last_vehicle!=vehicle){last_vehicle=vehicle;log_line(ANY_LOG_INFO,"framework",("BALANCE sampled=1 vehicle="+std::to_string(sample.vehicle_id)+" bodies="+std::to_string(sample.body_count)+" native_weighted_centre=1 body_mass_available="+std::to_string(bool(sample.valid_fields&ANY_BALANCE_BODY_MASS))).c_str());}
 return true;
}
static bool capture(void* item,void* camera,void* scene,void* client){__try{return capture_inner(item,camera,scene,client);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static void observe(void* item,void* camera,void* client,void* scene,const bool* authority,const int32_t* active,void* overlay){
 ((Overlay)overlay_hook.original())(item,camera,client,scene,authority,active,overlay);
 if(!ready||faulted||uintptr_t(item)!=allowed_item||uintptr_t(scene)!=allowed_scene)return;
 if(!authority||!*authority||!active||*active!=allowed_id||!camera||!capture(item,camera,scene,client))store.clear();
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
 attach_assembly();ready=true;log_line(ANY_LOG_INFO,"framework","BALANCE ready=1 awaiting_properties_tool=1 copied_snapshots=1 physics_writes=0");return true;
}
static DWORD WINAPI worker(void*){for(unsigned i=0;i<120;++i){if(prepare())return 0;Sleep(1000);}log_line(ANY_LOG_WARN,"framework","BALANCE unavailable=native_contract_unmatched");return 0;}
}
static const AnyCreationBalanceV1* anyapi_creation_balance_service(){return &balance_runtime::api;}
static void anyapi_creation_balance_tick(void* scene,void* actor){balance_runtime::tick(scene,actor);}
