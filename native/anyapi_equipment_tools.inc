// Included inside equipment_runtime, sharing the reviewed local actor/hotbar contracts.
using namespace equipment_tools_contract;
static equipment_tools::Store tools_store;static std::atomic<bool> tools_ready{};
struct PointerVector {uintptr_t data{};int32_t flags{},count{},capacity{},stride{};uintptr_t ctor{},dtor{};};
struct InventoryId {int32_t type{},floor{-1},vehicle{-1},actor{-1},component{-1},item{-1};};
static_assert(sizeof(PointerVector)==40&&sizeof(InventoryId)==24);
using Enumerate=void(*)(void*,const uintptr_t*,PointerVector*);
using VectorLife=void(*)(PointerVector*);
using Source=void(*)(int32_t*,void*,const int32_t*);
using Assign=void(*)(void*,const int32_t*,const InventoryId*,const bool*);
static Enumerate enumerate_items;static VectorLife vector_ctor,vector_dtor;static Source item_source;static Assign assign_item;
static uintptr_t property_slot{};
static uintptr_t managed_actor{},managed_scene{};static int32_t managed_slot=-1,managed_item=-1;static int tools_observed=-1;
struct Pending {uint64_t ticket{},context{},sent_at{};int32_t slot{-1},item{-1};};static Pending pending;
static bool copy_tools(AnyEquipmentToolsSnapshotV2* out){return tools_store.copy(out,GetTickCount64());}
static uint64_t equip_tool(uint64_t c,int32_t id){return ready&&tools_ready&&!faulted?tools_store.equip(c,id,GetTickCount64()):0;}
static uint32_t tool_state(uint64_t ticket){return tools_store.state(ticket);}
static const AnyEquipmentV2 tools_api{sizeof(AnyEquipmentV2),2,copy_tools,equip_tool,tool_state};
static bool collect_items(void* inventory,uintptr_t* items,int32_t* count){PointerVector v{};bool constructed=false,ok=false;uintptr_t filter{};
 __try{vector_ctor(&v);constructed=true;enumerate_items(inventory,&filter,&v);ok=v.count>=0&&v.count<=1024&&v.capacity>=v.count&&v.stride==8&&(!v.count||v.data)&&(!v.count||safe_read_memory(v.data,items,size_t(v.count)*8));if(ok)*count=v.count;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
 __try{if(constructed)vector_dtor(&v);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;ok=false;}return ok&&!faulted;
}
static bool item_number(uintptr_t item,int32_t& id){uintptr_t fn{},out{};if(!p34_virtual(item+8,property_slot,fn)||!p34_body(fn,P34_PROP_S32_GET))return false;((Pointer)fn)(&out,(void*)(item+8));return out&&safe_read_memory(out,&id,4)&&id>=0;}
static bool tool_entry(uintptr_t item,AnyEquipmentToolV2& entry){uintptr_t definition{};char item_class[128]{};
 if(!item||!safe_read_memory(item+0x28,&definition,8)||!definition||!p34_copy_string(definition+0x30,item_class,sizeof(item_class))||!equipment_tools::tool_class(item_class))return false;
 if(!item_number(item,entry.item_id)||!p34_copy_string(definition,entry.definition_id,sizeof(entry.definition_id))||!p34_copy_string(definition+0x10,entry.name,sizeof(entry.name)))return false;return true;
}
static bool tools_tick(void* client,void* scene,void* actor,uintptr_t inventory,uintptr_t bar,int32_t count,Item get_item,Select select_item,uint64_t now){
 if(!tools_ready||!client)return true;
 if(managed_actor!=uintptr_t(actor)||managed_scene!=uintptr_t(scene)){managed_actor=uintptr_t(actor);managed_scene=uintptr_t(scene);managed_slot=managed_item=-1;pending={};}
 uintptr_t items[1024]{};int32_t item_count{};if(!collect_items((void*)inventory,items,&item_count))return false;
 AnyEquipmentToolsSnapshotV2 snapshot;snapshot.context=context;snapshot.sampled_tick=now;
 for(int i=0;i<item_count;++i){AnyEquipmentToolV2 entry;if(!tool_entry(items[i],entry))continue;bool duplicate=false;for(uint32_t j=0;j<snapshot.count;++j)if(!strcmp(entry.definition_id,snapshot.tools[j].definition_id)){duplicate=true;if(entry.item_id<snapshot.tools[j].item_id)snapshot.tools[j]=entry;break;}if(!duplicate&&snapshot.count<ANY_EQUIPMENT_TOOLS_CAPACITY)snapshot.tools[snapshot.count++]=entry;}
 std::sort(snapshot.tools,snapshot.tools+snapshot.count,[](const auto& a,const auto& b){return strcmp(a.definition_id,b.definition_id)<0;});tools_store.publish(snapshot);
 if(tools_observed!=int(snapshot.count)){tools_observed=int(snapshot.count);log_line(ANY_LOG_INFO,"framework",("EQUIPMENT_TOOLS carried_types="+std::to_string(snapshot.count)+" recursive_inventory=1").c_str());}
 if(pending.ticket){int32_t current=-1;get_item(&current,(void*)bar,&inventory,&pending.slot);
  if(pending.context!=context||tools_store.state(pending.ticket)!=ANY_EQUIPMENT_QUEUED){pending={};}
  else if(current==pending.item){select_item((void*)bar,&inventory,&pending.slot);int32_t selected=-2;bool applied=safe_read_memory(bar+0x58,&selected,4)&&selected==pending.slot;tools_store.finish(pending.ticket,applied);if(applied){managed_slot=pending.slot;managed_item=pending.item;}log_line(applied?ANY_LOG_INFO:ANY_LOG_WARN,"framework",("EQUIPMENT_TOOL_EQUIP item="+std::to_string(pending.item)+" applied="+std::to_string(applied)+" replicated_slot_confirmed=1").c_str());pending={};}
  else if(now<pending.sent_at||now-pending.sent_at>3000){tools_store.finish(pending.ticket,false);pending={};}
 }
 auto job=tools_store.take(now);if(!job)return true;
 // Revalidate source through the local inventory, never an external crate/floor lookup.
 uintptr_t object{};((Lookup)lookup)(&object,(void*)inventory,&job->item);AnyEquipmentToolV2 candidate;
 int32_t source=0;item_source(&source,(void*)inventory,&job->item);int32_t actor_id=-1;
 if(!tool_entry(object,candidate)||candidate.item_id!=job->item||(source!=2&&source!=3)||!safe_read_memory(uintptr_t(actor)+8,&actor_id,4)||actor_id<0){tools_store.finish(job->ticket,false);return true;}
 // Native get_slot_item_id returns zero for empty slots; -1 denotes primary hands.
 int32_t target=-1,empty=-1;for(int32_t slot=0;slot<count;++slot){int32_t id=-1;get_item(&id,(void*)bar,&inventory,&slot);if(id==job->item){target=slot;break;}if(id<=0&&empty<0)empty=slot;}
 if(target>=0){select_item((void*)bar,&inventory,&target);int32_t selected=-2;tools_store.finish(job->ticket,safe_read_memory(bar+0x58,&selected,4)&&selected==target);return true;}
 // Only replace the shared slot if it still contains the item we assigned.
 if(managed_slot>=0&&managed_slot<count){int32_t id=-1;get_item(&id,(void*)bar,&inventory,&managed_slot);if(id==managed_item||id<=0)target=managed_slot;else managed_slot=managed_item=-1;}
 if(target<0)target=empty;
 if(target<0){bool holds_tool[32]{};int32_t selected=-1;safe_read_memory(bar+0x58,&selected,4);
  for(int32_t slot=0;slot<count&&slot<32;++slot){int32_t id=-1;get_item(&id,(void*)bar,&inventory,&slot);uintptr_t held{};AnyEquipmentToolV2 entry;if(id>0){((Lookup)lookup)(&held,(void*)inventory,&id);holds_tool[slot]=tool_entry(held,entry);}}
  target=equipment_tools::reclaim_slot(holds_tool,count<32?count:32,selected);if(target>=0)log_line(ANY_LOG_INFO,"framework",("EQUIPMENT_TOOL_RECLAIM shared_slot="+std::to_string(target)+" reason=no_free_slot replaces_tool_only=1").c_str());}
 if(target<0){tools_store.finish(job->ticket,false);log_line(ANY_LOG_WARN,"framework","EQUIPMENT_TOOL_EQUIP needs_free_or_tool_hotbar_slot=1 no_existing_slot_overwritten=1");return true;}
 InventoryId id;id.type=source;id.actor=actor_id;id.item=job->item;bool activate=false;
 assign_item((void*)(uintptr_t(client)+0x90),&target,&id,&activate);pending={job->ticket,context,now,target,job->item};
 log_line(ANY_LOG_INFO,"framework",("EQUIPMENT_TOOL_ASSIGN item="+std::to_string(job->item)+" shared_slot="+std::to_string(target)+" native_event=1").c_str());return true;
}
static bool dependency(uintptr_t owner,size_t offset,uintptr_t& fn){uintptr_t cell{};return safe_read_memory(owner+offset,&cell,8)&&cell&&safe_read_memory(cell,&fn,8)&&p34_executable(fn);}
static bool prepare_tools(){auto items=scan_exact(TOOLS,sizeof(TOOLS)),source=scan_exact(TOOL_SOURCE,sizeof(TOOL_SOURCE)),gun=scan_exact(VECTOR_OWNER,sizeof(VECTOR_OWNER)),parent=scan_exact(ASSIGN_PARENT,sizeof(ASSIGN_PARENT));uintptr_t owner{},ctor{},dtor{},event{},prop{};
 if(items.size()!=1||source.size()!=1||gun.size()!=1||parent.size()!=1||!dependency(parent[0],ASSIGN_PARENT_OFFSET,owner)||!p34_body(owner,ASSIGN_OWNER)||!p34_executable(items[0])||!p34_executable(source[0])||!dependency(gun[0],VECTOR_OWNER_END+8,ctor)||!p34_body(ctor,PTR_VECTOR_CTOR)||!dependency(gun[0],VECTOR_OWNER_END+80,dtor)||!dependency(owner,ASSIGN_OWNER_END+16,event)||!p34_body(event,ASSIGN)||!safe_read_memory(gun[0]+VECTOR_OWNER_END+72,&prop,8)||prop%8||prop>=0x10000)return false;
 enumerate_items=(Enumerate)items[0];item_source=(Source)source[0];vector_ctor=(VectorLife)ctor;vector_dtor=(VectorLife)dtor;assign_item=(Assign)event;property_slot=prop;tools_ready=true;log_line(ANY_LOG_INFO,"framework","EQUIPMENT_TOOLS ready=1 native_recursive_inventory=1 shared_hotbar_slot=1 preserves_non_tool_slots=1");return true;
}
