#include "anyapi_inventory_actions_v1.h"
#include "anyapi_inventory_patterns.h"
#include <deque>
namespace inventory_actions {
using LookupFn=void(*)(uintptr_t*,void*,const GeoString16*);
using GiveFn=void(*)(void*,const int32_t*,const int32_t*,const int32_t*,const GeoString16*);
static LookupFn lookup,component_lookup;static GiveFn give;static std::atomic<bool> ready{},faulted{};
struct Request {uint64_t ticket{},queued_at{};uintptr_t scene{};uint32_t state{},allowed_modes{UINT32_MAX};char id[128]{};};
static std::mutex mutex;static std::deque<Request> requests;static uint64_t next_ticket{},context_tick{};static uintptr_t context_scene{};
static uint32_t available(){std::lock_guard lock(mutex);auto now=GetTickCount64();return ready&&!faulted&&context_scene&&now>=context_tick&&now-context_tick<500;}
static bool permitted(uint32_t mask){if(mask==UINT32_MAX)return true;AnySessionStateV1 mode;return session_runtime::copy(&mode)&&mode.mode<32&&(mask&(1u<<mode.mode));}
static uint64_t add_mask(const char* id,uint32_t mask){if(!permitted(mask))return 0;if(!id||!ready||faulted)return 0;size_t length=strnlen_s(id,128);if(!length||length>=128)return 0;bool known=false;for(auto& entry:catalog_runtime::entries)if(!strcmp(id,entry.metadata.id)){known=true;break;}if(!known)return 0;
 std::lock_guard lock(mutex);auto now=GetTickCount64();if(!context_scene||now<context_tick||now-context_tick>=500)return 0;size_t pending=0;for(auto& r:requests)if(r.state==ANY_ITEM_REQUEST_QUEUED)++pending;if(pending>=8)return 0;
 while(requests.size()>=64&&requests.front().state!=ANY_ITEM_REQUEST_QUEUED)requests.pop_front();if(requests.size()>=64)return 0;Request r;r.ticket=++next_ticket;r.queued_at=now;r.scene=context_scene;r.state=ANY_ITEM_REQUEST_QUEUED;r.allowed_modes=mask;memcpy(r.id,id,length+1);requests.push_back(r);return r.ticket;}
static uint64_t add(const char* id){return add_mask(id,UINT32_MAX);}
static uint32_t state(uint64_t ticket){std::lock_guard lock(mutex);for(auto& r:requests)if(r.ticket==ticket)return r.state;return ANY_ITEM_REQUEST_UNKNOWN;}
// Game-native calls stay in a POD-only SEH leaf on the existing player tick thread.
static uint32_t dispatch(void* client,void* scene,const char* id){__try{uintptr_t definition{};bool component_item=!strncmp(id,"component:",10);int32_t component=-1,color=0;
 if(component_item){uintptr_t part{};auto part_name=make_const_geostring(id+10);component_lookup(&part,(void*)((uintptr_t)scene+0x15d8),&part_name);if(!part||!safe_read_memory(part+0x290,&component,4)||component<0)return ANY_ITEM_REQUEST_FAILED;bool building=false;for(auto& entry:catalog_runtime::entries)if(!strcmp(entry.metadata.id,id)){building=!strcmp(entry.metadata.category,"building");break;}auto wrapper=make_const_geostring(building?"vehicle_editor_add_building_component":"vehicle_editor_add_component");lookup(&definition,(void*)((uintptr_t)scene+0x1530),&wrapper);
 }else{auto name=make_const_geostring(id);lookup(&definition,(void*)((uintptr_t)scene+0x1530),&name);}int32_t numeric_id=-1;if(!definition||!safe_read_memory(definition+0x408,&numeric_id,4)||numeric_id<0)return ANY_ITEM_REQUEST_FAILED;
 auto filter=make_const_geostring("");give((void*)((uintptr_t)client+0x90),&numeric_id,&component,&color,&filter);return ANY_ITEM_REQUEST_SENT;
 }__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;log_line(ANY_LOG_ERROR,"framework","INVENTORY_ACTION disabled_after_native_exception=1");return ANY_ITEM_REQUEST_FAILED;}}
static void tick(void* client,void* scene,void* actor){if(!ready||faulted)return;uintptr_t local{};int32_t ui=-1;
 bool sampled=g_p34_frontend&&safe_read_memory(g_p34_frontend+0x828,&ui,4);
 bool local_read=scene&&safe_read_memory((uintptr_t)scene+0x188,&local,8);if(local_read&&!local)safe_read_memory((uintptr_t)scene+0x190,&local,8);
 bool valid=client&&scene&&actor&&local_read&&local==(uintptr_t)actor&&sampled&&ui==2;
 std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;auto now=GetTickCount64();if(sampled&&ui!=2){context_tick=0;context_scene=0;}if(valid){session_runtime::sample(scene);context_scene=(uintptr_t)scene;context_tick=now;}
 // Requests are bound to the sampled world, expire when the inventory closes,
 // and are consumed exactly once; no retries can duplicate a native event.
 for(auto& r:requests){if(r.state!=ANY_ITEM_REQUEST_QUEUED)continue;if((sampled&&ui!=2)||now-r.queued_at>2000||(valid&&r.scene!=(uintptr_t)scene)){r.state=ANY_ITEM_REQUEST_EXPIRED;continue;}if(!valid)continue;if(!permitted(r.allowed_modes)){r.state=ANY_ITEM_REQUEST_EXPIRED;continue;}r.state=dispatch(client,scene,r.id);char message[256];std::snprintf(message,sizeof(message),"INVENTORY_ACTION ticket=%llu id=%s state=%s native_instances=1 server_confirmation=NOT_OBSERVED",(unsigned long long)r.ticket,r.id,r.state==ANY_ITEM_REQUEST_SENT?"SENT":"FAILED");log_line(ANY_LOG_INFO,"framework",message);break;}
}
static DWORD WINAPI worker(void*){while(!ready&&!faulted){auto owners=scan_exact(INV_GIVE_OWNER,sizeof(INV_GIVE_OWNER));if(owners.size()==1){uintptr_t get{},send{},get_component{};if(p34_cell(owners[0],0xd068,get)&&p34_body(get,INV_LOOKUP)&&p34_cell(owners[0],0xd0b0,send)&&p34_body(send,INV_GIVE_EVENT)&&p34_cell(owners[0],0xd0e8,get_component)&&p34_body(get_component,INV_COMPONENT_LOOKUP)&&session_runtime::resolve()){lookup=(LookupFn)get;component_lookup=(LookupFn)get_component;give=(GiveFn)send;ready=true;log_line(ANY_LOG_INFO,"framework","INVENTORY_ACTION ready=1 mechanism=NATIVE_GIVE_EVENT thread=PLAYER_TICK direct_server_writes=0");break;}}Sleep(5000);}return 0;}
static const AnyInventoryActionsV1 api{sizeof(AnyInventoryActionsV1),1,available,add,state};
static const AnyInventoryActionsV2 api_v2{sizeof(AnyInventoryActionsV2),2,&api,add_mask};
}
static const AnyInventoryActionsV1* anyapi_inventory_actions_service(){return &inventory_actions::api;}
static void inventory_actions_tick(void* client,void* scene,void* actor){inventory_actions::tick(client,scene,actor);}

static const AnyInventoryActionsV2* anyapi_inventory_actions_service_v2(){return &inventory_actions::api_v2;}
