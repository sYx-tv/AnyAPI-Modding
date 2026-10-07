#include "equipment_state.h"
#include "equipment_contract.h"
namespace equipment_runtime {
using namespace equipment_contract;
static equipment::Store store;static std::atomic<bool> ready{},faulted{};
static uintptr_t inventory_slot{},bar_slot{},count_slot{},item_slot{},select_slot{},lookup{};
static int observed_count=-1;
static uintptr_t last_scene{},last_actor{};static uint64_t context{},sampled_at{};
using Pointer=void(*)(uintptr_t*,void*);
using Count=void(*)(int32_t*,void*);
using Item=void(*)(int32_t*,void*,const uintptr_t*,const int32_t*);
using Lookup=void(*)(uintptr_t*,void*,const int32_t*);
using Select=void(*)(void*,const uintptr_t*,const int32_t*);
static bool copy(AnyEquipmentSnapshotV1* out){return store.copy(out,GetTickCount64());}
static uint64_t select(uint64_t id,int32_t slot,int32_t item){return ready&&!faulted?store.select(id,slot,item,GetTickCount64()):0;}
static uint32_t state(uint64_t ticket){return store.state(ticket);}
static const AnyEquipmentV1 api{sizeof(AnyEquipmentV1),1,copy,select,state};
static bool capture_inner(void* actor,void* scene,uint64_t now){
 uintptr_t inventory{},bar{},fn{},count_fn{},item_fn{},select_fn{};
 if(!p34_virtual(uintptr_t(actor),inventory_slot,fn)||!p34_body(fn,INV))return false;
 ((Pointer)fn)(&inventory,actor);
 if(!p34_virtual(uintptr_t(actor),bar_slot,fn)||!p34_body(fn,BAR))return false;
 ((Pointer)fn)(&bar,actor);
 if(inventory!=uintptr_t(actor)+0x5c8||bar!=uintptr_t(actor)+0x658||!p34_virtual(bar,count_slot,count_fn)||!p34_body(count_fn,COUNT)||!p34_virtual(bar,item_slot,item_fn)||!p34_body(item_fn,ITEM)||!p34_virtual(bar,select_slot,select_fn)||!p34_body(select_fn,SELECT))return false;
 int32_t count{};((Count)count_fn)(&count,(void*)bar);if(count<0||count>32)return false;
 AnyEquipmentSnapshotV1 snapshot;snapshot.context=context;snapshot.sampled_tick=now;
 if(!safe_read_memory(bar+0x58,&snapshot.selected_slot,4))return false;
 for(int32_t slot=-1;slot<count;++slot){int32_t id=-1;((Item)item_fn)(&id,(void*)bar,&inventory,&slot);if(id<0&&slot!=-1)continue;
  AnyEquipmentSlotV1 entry;entry.slot_index=slot;entry.item_id=id;
  if(id>=0){uintptr_t item{},definition{};((Lookup)lookup)(&item,(void*)inventory,&id);
   if(!item||!safe_read_memory(item+0x28,&definition,8)||!definition||!p34_copy_string(definition,entry.definition_id,sizeof(entry.definition_id))||!p34_copy_string(definition+0x10,entry.name,sizeof(entry.name)))continue;
  }else strcpy_s(entry.name,"Empty hands");
  snapshot.slots[snapshot.count++]=entry;
 }
 if(observed_count!=int(snapshot.count)){observed_count=int(snapshot.count);log_line(ANY_LOG_INFO,"framework",("EQUIPMENT copied="+std::to_string(snapshot.count)+" selected="+std::to_string(snapshot.selected_slot)+" local_only=1").c_str());}
 store.publish(snapshot);
 auto job=store.take(now);if(job){int32_t current=-1;((Item)item_fn)(&current,(void*)bar,&inventory,&job->slot);
  bool applied=current==job->item;
  if(applied){((Select)select_fn)((void*)bar,&inventory,&job->slot);int32_t selected=-2;applied=safe_read_memory(bar+0x58,&selected,4)&&selected==job->slot;}
  store.finish(job->ticket,applied);
  log_line(applied?ANY_LOG_INFO:ANY_LOG_WARN,"framework",("EQUIPMENT_SELECTION ticket="+std::to_string(job->ticket)+" slot="+std::to_string(job->slot)+" applied="+std::to_string(applied)+" native_player_thread=1").c_str());
 }
 return true;
}
static bool capture(void* actor,void* scene,uint64_t now){__try{return capture_inner(actor,scene,now);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static void tick(void* scene,void* actor){if(!ready||faulted)return;uintptr_t local{};int32_t ui=-1;
 bool local_read=scene&&safe_read_memory(uintptr_t(scene)+0x188,&local,8);if(local_read&&!local)safe_read_memory(uintptr_t(scene)+0x190,&local,8);
 if(!local_read||!local){store.clear();last_scene=last_actor=0;return;}if(local!=uintptr_t(actor))return;
 bool gameplay=g_p34_frontend&&safe_read_memory(g_p34_frontend+0x828,&ui,4)&&ui==1;
 if(!gameplay||GetForegroundWindow()!=platform::game_window){store.clear();last_scene=last_actor=0;return;}
 auto now=GetTickCount64();if(last_scene!=uintptr_t(scene)||last_actor!=local){last_scene=uintptr_t(scene);last_actor=local;++context;sampled_at=0;}
 if(now-sampled_at<30)return;sampled_at=now;
 if(!capture(actor,scene,now)){store.clear();if(faulted)log_line(ANY_LOG_ERROR,"framework","EQUIPMENT disabled_after_native_exception=1");}
}
static bool slot_at(uintptr_t owner,size_t offset,uintptr_t& slot){return safe_read_memory(owner+offset,&slot,8)&&slot%8==0&&slot<0x10000;}
static DWORD WINAPI worker(void*){
 for(unsigned tries=0;tries<120;++tries){auto ui=scan_exact(HOTBAR_UI,sizeof(HOTBAR_UI)),overlay=scan_exact(OVERLAY,sizeof(OVERLAY)),left=scan_exact(LEFT,sizeof(LEFT)),items=scan_exact(LOOKUP,sizeof(LOOKUP));
 if(ui.size()==1&&overlay.size()==1&&left.size()==1&&items.size()==1&&p34_executable(items[0])&&slot_at(ui[0],HOTBAR_UI_END+16,count_slot)&&slot_at(ui[0],HOTBAR_UI_END+72,item_slot)&&slot_at(left[0],LEFT_END,select_slot)&&slot_at(overlay[0],OVERLAY_END,inventory_slot)&&slot_at(overlay[0],OVERLAY_END+8,bar_slot)){
  lookup=items[0];ready=true;log_line(ANY_LOG_INFO,"framework","EQUIPMENT ready=1 mechanism=NATIVE_HOTBAR_SELECTION local_player_only=1");return 0;}
 Sleep(1000);
 }log_line(ANY_LOG_WARN,"framework","EQUIPMENT unavailable=native_contract_unmatched");return 0;
}
}
static const AnyEquipmentV1* anyapi_equipment_service(){return &equipment_runtime::api;}
static void anyapi_equipment_tick(void* scene,void* actor){equipment_runtime::tick(scene,actor);}
