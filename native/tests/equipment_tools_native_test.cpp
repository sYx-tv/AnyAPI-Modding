#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include "../equipment_state.h"
#include "../native_player_patterns.h"
static constexpr int ANY_LOG_INFO=0,ANY_LOG_WARN=1,ANY_LOG_ERROR=2;
static uintptr_t g_p34_frontend;
namespace platform {static HWND game_window;}
static bool safe_read_memory(uintptr_t p,void* out,size_t n){if(!p)return false;memcpy(out,(void*)p,n);return true;}
static bool p34_executable(uintptr_t p){return p!=0;}
static std::map<const unsigned char*,std::vector<uintptr_t>> scan_hits;
static std::vector<uintptr_t> scan_exact(const unsigned char* p,size_t){return scan_hits[p];}
static void log_line(int,const char*,const char*){}
static unsigned char actor[2048]{},scene[512]{},client[256]{},item1[64]{},item2[64]{},item3[64]{},item4[64]{},item5[64]{},definition1[64]{},definition2[64]{},definition3[64]{},definition4[64]{};
static int32_t ids[3]{111,222,-1};static unsigned select_calls;static bool verified=true;
static std::map<uintptr_t,std::string> strings;
static bool p34_copy_string(uintptr_t object,char* out,size_t capacity){auto it=strings.find(object);if(it==strings.end()||it->second.size()>=capacity)return false;strcpy_s(out,capacity,it->second.c_str());return true;}
static void inv(uintptr_t* out,void* self){assert(self==actor);*out=uintptr_t(actor)+0x5c8;}
static void bar(uintptr_t* out,void* self){assert(self==actor);*out=uintptr_t(actor)+0x658;}
static void count(int32_t* out,void* self){assert(self==actor+0x658);*out=3;}
static void item(int32_t* out,void* self,const uintptr_t* inventory,const int32_t* index){assert(self==actor+0x658);assert(*inventory==uintptr_t(actor)+0x5c8);assert(*index>=-1&&*index<3);*out=*index==-1?-1:ids[*index];}
static void fake_select(void* self,const uintptr_t* inventory,const int32_t* index){assert(self==actor+0x658&&*inventory==uintptr_t(actor)+0x5c8);++select_calls;memcpy(actor+0x658+0x58,index,4);}
static void lookup(uintptr_t* out,void* inventory,const int32_t* id){assert(inventory==actor+0x5c8);*out=*id==111?uintptr_t(item1):*id==222?uintptr_t(item2):*id==300?uintptr_t(item3):*id==333?uintptr_t(item4):*id==500?uintptr_t(item5):0;}
static void property(uintptr_t* out,void* self){*out=uintptr_t(self)+8;}
static bool p34_virtual(uintptr_t,uintptr_t slot,uintptr_t& fn){switch(slot){case 0:fn=uintptr_t(inv);break;case 8:fn=uintptr_t(bar);break;case 16:fn=uintptr_t(count);break;case 24:fn=uintptr_t(item);break;case 32:fn=uintptr_t(fake_select);break;case 40:fn=uintptr_t(property);break;default:return false;}return true;}
template<size_t N>static bool p34_body(uintptr_t,const unsigned char(&)[N]){return verified;}
#include "../anyapi_equipment.inc"

static uintptr_t carried[5]{};static unsigned ctor_calls,dtor_calls,assign_calls;static int32_t assigned_slot=-1,assigned_id=-1;static int32_t sources[5]{2,2,2,2,3};
static void ctor(equipment_runtime::PointerVector* v){++ctor_calls;*v={};v->stride=8;}
static void dtor(equipment_runtime::PointerVector* v){++dtor_calls;assert(v->stride==8);}
static void enumerate(void* inv,const uintptr_t* filter,equipment_runtime::PointerVector* v){assert(inv==actor+0x5c8&&!*filter);v->data=uintptr_t(carried);v->count=v->capacity=5;}
static void source(int32_t* out,void* inv,const int32_t* id){assert(inv==actor+0x5c8);*out=*id==222?2:*id==300?2:0;}
static void assign(void* peer,const int32_t* slot,const equipment_runtime::InventoryId* id,const bool* activate){assert(peer==client+0x90&&!*activate);assert(id->type==2&&id->actor==10&&id->floor==-1&&id->vehicle==-1&&id->component==-1);++assign_calls;assigned_slot=*slot;assigned_id=id->item;}
static void setup(unsigned char* item,unsigned char* def,int32_t id,const char* key,const char* name,const char* cls){uintptr_t p=uintptr_t(def);memcpy(item+0x28,&p,8);memcpy(item+16,&id,4);strings[p]=key;strings[p+16]=name;strings[p+48]=cls;}
static void resolver_test(){using namespace equipment_runtime;using namespace equipment_tools_contract;
 static unsigned char parent[54000]{},gun[1200]{},owner[600]{},passive[600]{};
 uintptr_t owner_cell=uintptr_t(owner),ctor_cell=uintptr_t(ctor),dtor_cell=uintptr_t(dtor),event_cell=uintptr_t(assign),prop=40;
 auto put=[](unsigned char* b,size_t offset,uintptr_t p){memcpy(b+offset,&p,8);};
 put(parent,ASSIGN_PARENT_OFFSET,uintptr_t(&owner_cell));put(gun,VECTOR_OWNER_END+8,uintptr_t(&ctor_cell));put(gun,VECTOR_OWNER_END+80,uintptr_t(&dtor_cell));put(gun,VECTOR_OWNER_END+72,prop);put(owner,ASSIGN_OWNER_END+16,uintptr_t(&event_cell));
 scan_hits[TOOLS]={uintptr_t(enumerate)};scan_hits[TOOL_SOURCE]={uintptr_t(source)};scan_hits[VECTOR_OWNER]={uintptr_t(gun)};scan_hits[ASSIGN_PARENT]={uintptr_t(parent)};
 // Active/passive functions have identical code: resolve through the unique caller.
 scan_hits[ASSIGN_OWNER]={uintptr_t(owner),uintptr_t(passive)};
 assert(prepare_tools()&&assign_item==assign&&enumerate_items==enumerate&&property_slot==40);
 verified=false;assert(!prepare_tools());verified=true;
 scan_hits[ASSIGN_PARENT].push_back(uintptr_t(passive));assert(!prepare_tools());scan_hits.clear();
}
int main(){resolver_test();using namespace equipment_runtime;inventory_slot=0;bar_slot=8;count_slot=16;item_slot=24;select_slot=32;equipment_runtime::lookup=uintptr_t(::lookup);property_slot=40;context=1;ready=tools_ready=true;vector_ctor=ctor;vector_dtor=dtor;enumerate_items=enumerate;item_source=source;assign_item=assign;
 int32_t actor_id=10;memcpy(actor+8,&actor_id,4);
 setup(item1,definition1,111,"torch","Torch","torch");setup(item2,definition2,222,"vehicle_editor_repair","Repair Tool","vehicle_editor_repair");setup(item3,definition3,300,"vehicle_editor_add_edge_2","Edge Tool 3x3","vehicle_editor_add_edge_2");setup(item4,definition2,333,"vehicle_editor_repair","Repair Tool","vehicle_editor_repair");setup(item5,definition4,500,"vehicle_editor_add_component","Part","vehicle_editor_add_component");
 carried[0]=uintptr_t(item1);carried[1]=uintptr_t(item2);carried[2]=uintptr_t(item3);carried[3]=uintptr_t(item4);carried[4]=uintptr_t(item5);
 auto capture=[&](uint64_t now){assert(capture_inner(actor,scene,now,client));assert(ctor_calls==dtor_calls);};
 auto now=GetTickCount64();capture(now);AnyEquipmentToolsSnapshotV2 snap;assert(tools_api.copy(&snap)&&snap.count==2&&snap.tools[0].item_id==300&&snap.tools[1].item_id==222);
 // Already assigned tool selects directly, with no event or inventory relocation.
 auto ticket=tools_api.equip(context,222);assert(ticket);capture(GetTickCount64());assert(tools_api.state(ticket)==ANY_EQUIPMENT_APPLIED&&select_calls==1&&assign_calls==0);
 // Unassigned bag tool uses the first empty slot, waits for replication before selecting.
 ticket=tools_api.equip(context,300);capture(GetTickCount64());assert(assign_calls==1&&assigned_slot==2&&assigned_id==300&&tools_api.state(ticket)==ANY_EQUIPMENT_QUEUED&&select_calls==1);
 capture(GetTickCount64());assert(assign_calls==1&&select_calls==1);ids[2]=300;capture(GetTickCount64());assert(select_calls==2&&tools_api.state(ticket)==ANY_EQUIPMENT_APPLIED&&managed_slot==2);
 // Reuse the one managed slot after the old manual assignment is removed.
 ids[1]=999;ticket=tools_api.equip(context,222);capture(GetTickCount64());assert(assign_calls==2&&assigned_slot==2&&assigned_id==222);ids[2]=222;capture(GetTickCount64());assert(tools_api.state(ticket)==ANY_EQUIPMENT_APPLIED);
 // User overwrote that slot: all slots occupied, so fail without overwriting any.
 ids[2]=888;ticket=tools_api.equip(context,300);capture(GetTickCount64());assert(tools_api.state(ticket)==ANY_EQUIPMENT_FAILED&&assign_calls==2&&ids[0]==111&&ids[1]==999&&ids[2]==888);
 // A dropped tool removes its slice and expires an unexecuted request.
 ids[2]=-1;ticket=tools_api.equip(context,300);carried[2]=0;capture(GetTickCount64());assert(tools_api.state(ticket)==ANY_EQUIPMENT_EXPIRED&&assign_calls==2);assert(tools_api.copy(&snap)&&snap.count==1);
 carried[2]=uintptr_t(item3);capture(GetTickCount64());ticket=tools_api.equip(context,300);capture(GetTickCount64());assert(assign_calls==3);capture(GetTickCount64()+3001);assert(tools_api.state(ticket)==ANY_EQUIPMENT_FAILED&&select_calls==3);
 assert(!tools_api.equip(context,500));assert(!tools_api.equip(context,111));
}
