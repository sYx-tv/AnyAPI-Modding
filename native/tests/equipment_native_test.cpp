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
static std::vector<uintptr_t> scan_exact(const unsigned char*,size_t){return {};}
static void log_line(int,const char*,const char*){}
static unsigned char actor[2048]{},scene[512]{},item1[64]{},item2[64]{},definition1[64]{},definition2[64]{};
static int32_t ids[3]{111,222,-1};static unsigned select_calls;static bool verified=true;
static std::map<uintptr_t,std::string> strings;
static bool p34_copy_string(uintptr_t object,char* out,size_t capacity){auto it=strings.find(object);if(it==strings.end()||it->second.size()>=capacity)return false;strcpy_s(out,capacity,it->second.c_str());return true;}
static void inv(uintptr_t* out,void* self){assert(self==actor);*out=uintptr_t(actor)+0x5c8;}
static void bar(uintptr_t* out,void* self){assert(self==actor);*out=uintptr_t(actor)+0x658;}
static void count(int32_t* out,void* self){assert(self==actor+0x658);*out=3;}
static void item(int32_t* out,void* self,const uintptr_t* inventory,const int32_t* index){assert(self==actor+0x658);assert(*inventory==uintptr_t(actor)+0x5c8);assert(*index>=-1&&*index<3);*out=*index==-1?-1:ids[*index];}
static void fake_select(void* self,const uintptr_t* inventory,const int32_t* index){assert(self==actor+0x658&&*inventory==uintptr_t(actor)+0x5c8);++select_calls;memcpy(actor+0x658+0x58,index,4);}
static void lookup(uintptr_t* out,void* inventory,const int32_t* id){assert(inventory==actor+0x5c8);*out=*id==111?uintptr_t(item1):*id==222?uintptr_t(item2):0;}
static bool p34_virtual(uintptr_t,uintptr_t slot,uintptr_t& fn){switch(slot){case 0:fn=uintptr_t(inv);break;case 8:fn=uintptr_t(bar);break;case 16:fn=uintptr_t(count);break;case 24:fn=uintptr_t(item);break;case 32:fn=uintptr_t(fake_select);break;default:return false;}return true;}
template<size_t N>static bool p34_body(uintptr_t,const unsigned char(&)[N]){return verified;}
#include "../anyapi_equipment.inc"
int main(){using namespace equipment_runtime;inventory_slot=0;bar_slot=8;count_slot=16;item_slot=24;select_slot=32;equipment_runtime::lookup=uintptr_t(::lookup);context=1;ready=true;
 uintptr_t a=uintptr_t(definition1),b=uintptr_t(definition2);memcpy(item1+0x28,&a,8);memcpy(item2+0x28,&b,8);strings[a]="torch";strings[a+16]="Torch";strings[b]="repair_tool";strings[b+16]="Repair tool";
 auto now=GetTickCount64();assert(capture_inner(actor,scene,now));AnyEquipmentSnapshotV1 s;assert(api.copy(&s)&&s.count==3&&s.slots[1].item_id==111&&std::string(s.slots[2].name)=="Repair tool");
 auto request=api.select(s.context,1,222);assert(request&&api.state(request)==ANY_EQUIPMENT_QUEUED);assert(capture_inner(actor,scene,GetTickCount64()));assert(select_calls==1&&api.state(request)==ANY_EQUIPMENT_APPLIED);assert(api.copy(&s)&&s.selected_slot==0); // Snapshot precedes selection; next sample confirms it.
 assert(capture_inner(actor,scene,GetTickCount64()));assert(api.copy(&s)&&s.selected_slot==1);
 request=api.select(s.context,1,222);ids[1]=333;assert(capture_inner(actor,scene,GetTickCount64()));assert(select_calls==1&&api.state(request)==ANY_EQUIPMENT_EXPIRED);
 request=api.select(context,-1,-1);assert(request);assert(capture_inner(actor,scene,GetTickCount64()));assert(select_calls==2&&api.state(request)==ANY_EQUIPMENT_APPLIED);
 request=api.select(context,0,111);++context;assert(capture_inner(actor,scene,GetTickCount64()));assert(select_calls==2&&api.state(request)==ANY_EQUIPMENT_EXPIRED);
 verified=false;assert(!capture_inner(actor,scene,GetTickCount64()));
}
