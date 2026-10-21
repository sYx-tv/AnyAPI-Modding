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
static uint64_t add_mask(const char* id,uint32_t mask){if(!permitted(mask))return 0;if(!id||!ready||faulted)return 0;size_t length=strnlen_s(id,128);if(!length||length>=128)return 0;if(!catalog_runtime::contains(id))return 0;
 std::lock_guard lock(mutex);auto now=GetTickCount64();if(!context_scene||now<context_tick||now-context_tick>=500)return 0;size_t pending=0;for(auto& r:requests)if(r.state==ANY_ITEM_REQUEST_QUEUED)++pending;if(pending>=8)return 0;
 while(requests.size()>=64&&requests.front().state!=ANY_ITEM_REQUEST_QUEUED)requests.pop_front();if(requests.size()>=64)return 0;Request r;r.ticket=++next_ticket;r.queued_at=now;r.scene=context_scene;r.state=ANY_ITEM_REQUEST_QUEUED;r.allowed_modes=mask;memcpy(r.id,id,length+1);requests.push_back(r);return r.ticket;}
static uint64_t add(const char* id){return add_mask(id,UINT32_MAX);}
static uint32_t state(uint64_t ticket){std::lock_guard lock(mutex);for(auto& r:requests)if(r.ticket==ticket)return r.state;return ANY_ITEM_REQUEST_UNKNOWN;}
// Game-native calls stay in a POD-only SEH leaf on the existing player tick thread.
static uint32_t dispatch(void* client,void* scene,const char* id,bool building){__try{uintptr_t definition{};bool component_item=!strncmp(id,"component:",10);int32_t component=-1,color=0;
 if(component_item){uintptr_t part{};auto part_name=make_const_geostring(id+10);component_lookup(&part,(void*)((uintptr_t)scene+0x15d8),&part_name);if(!part||!safe_read_memory(part+0x290,&component,4)||component<0)return ANY_ITEM_REQUEST_FAILED;auto wrapper=make_const_geostring(building?"vehicle_editor_add_building_component":"vehicle_editor_add_component");lookup(&definition,(void*)((uintptr_t)scene+0x1530),&wrapper);
 }else{auto name=make_const_geostring(id);lookup(&definition,(void*)((uintptr_t)scene+0x1530),&name);}int32_t numeric_id=-1;if(!definition||!safe_read_memory(definition+0x408,&numeric_id,4)||numeric_id<0)return ANY_ITEM_REQUEST_FAILED;
 auto filter=make_const_geostring("");give((void*)((uintptr_t)client+0x90),&numeric_id,&component,&color,&filter);return ANY_ITEM_REQUEST_SENT;
 }__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;log_line(ANY_LOG_ERROR,"framework","INVENTORY_ACTION disabled_after_native_exception=1");return ANY_ITEM_REQUEST_FAILED;}}
// Mods add parts and items to the loaded world's definition containers (GpsPart, Turbo), after the asset
// files the catalog reads at startup. While the inventory is open, copy any definition those files did not
// list into the catalog so the item browser can find it. Client scene +0x1530 is the inventory definition
// container (definitions vector at +0x0), +0x15d8 the vehicle component container (vector at +0x8); both
// hold vector<ref<definition>> whose objects start with id, name, description and class strings. A component's
// mesh_static path (+0xe0) goes into its JSON so the item browser can draw its picture like a stock part's.
struct LiveVector {uintptr_t buffer;int32_t offset,count,capacity,size;uintptr_t ctor,dtor;};
static bool live_objects(uintptr_t address,std::vector<uintptr_t>& out){LiveVector v{};if(!safe_read_memory(address,&v,sizeof(v))||(v.size!=16&&v.size!=8)||v.count<0||v.count>10000||v.capacity<v.count||v.capacity>65536||v.offset<0||v.offset>=std::max(1,v.capacity)||(!v.buffer&&v.count))return false;
 for(int i=0;i<v.count;++i){uintptr_t object{};if(!safe_read_memory(v.buffer+uintptr_t((v.offset+i)%v.capacity)*v.size+(v.size==16?8:0),&object,8))return false;if(object)out.push_back(object);}return true;}
static std::string live_json_text(const char* s){std::string out="\"";for(;*s;++s){unsigned char c=*s;if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<0x20){char hex[8];std::snprintf(hex,sizeof(hex),"\\u%04x",c);out+=hex;}else out+=char(c);}return out+"\"";}
static void merge_live(uintptr_t scene,uint64_t now){static uintptr_t last_scene{};static uint64_t next_at{};static int32_t last_counts[2]{-1,-1};if(scene==last_scene&&now<next_at)return;next_at=now+5000;
 int32_t counts[2]{-1,-1};for(int pass=0;pass<2;++pass){LiveVector v{};if(safe_read_memory(scene+(pass?0x15d8+0x8:0x1530),&v,sizeof(v)))counts[pass]=v.count;}if(scene==last_scene&&counts[0]==last_counts[0]&&counts[1]==last_counts[1])return;last_scene=scene;last_counts[0]=counts[0];last_counts[1]=counts[1];
 std::vector<itemcatalog::Entry> found;
 for(int pass=0;pass<2;++pass){bool component=pass==1;std::vector<uintptr_t> objects;if(!live_objects(scene+(component?0x15d8+0x8:0x1530),objects))continue;
  for(auto object:objects){itemcatalog::Entry entry;auto& m=entry.metadata;char raw[118]{};if(!p34_copy_string(object,raw,sizeof(raw))||!raw[0]||!strncmp(raw,"vehicle_editor_",15))continue;
   std::string id=component?std::string("component:")+raw:std::string(raw);if(catalog_runtime::contains(id.c_str()))continue;strcpy_s(m.id,id.c_str());
   if(!p34_copy_string(object+0x10,m.name,sizeof(m.name))||!m.name[0])strcpy_s(m.name,raw);p34_copy_string(object+0x20,m.description,sizeof(m.description));p34_copy_string(object+0x30,m.item_class,sizeof(m.item_class));p34_copy_string(object+(component?0x1b8:0x330),m.category,sizeof(m.category));
   std::string json="{\"id\": "+live_json_text(raw)+", \"name\": "+live_json_text(m.name)+", \"description\": "+live_json_text(m.description)+", \"class\": "+live_json_text(m.item_class)+", \"category\": "+live_json_text(m.category);
   if(component){m.valid_fields|=ANY_ITEM_VEHICLE_COMPONENT;char mesh[260]{};if(p34_copy_string(object+0xe0,mesh,sizeof(mesh))&&mesh[0])json+=", \"mesh_static\": {\"mesh_path\": "+live_json_text(mesh)+"}";}else{int32_t tier{};if(safe_read_memory(object+0x318,&tier,4)&&tier>=0&&tier<=100){m.tech_tier=tier;m.valid_fields|=ANY_ITEM_TECH_TIER;json+=", \"tech_tier\": "+std::to_string(tier);}char mesh[260]{};if(p34_copy_string(object+0x40,mesh,sizeof(mesh))&&mesh[0])json+=", \"mesh_file\": "+live_json_text(mesh);}
   entry.json=json+", \"source\": \"loaded_world\"}";found.push_back(std::move(entry));if(found.size()>=2000)break;}}
 std::string ids;for(auto& f:found)if(ids.size()<1500)ids+=std::string(ids.empty()?"":",")+f.metadata.id;
 if(auto added=catalog_runtime::append(found))log_line(ANY_LOG_INFO,"framework",("ITEM_CATALOG live_added="+std::to_string(added)+" source=loaded_world ids="+ids).c_str());}
static void tick(void* client,void* scene,void* actor){if(!ready||faulted)return;uintptr_t local{};int32_t ui=-1;
 bool sampled=g_p34_frontend&&safe_read_memory(g_p34_frontend+0x828,&ui,4);
 bool local_read=scene&&safe_read_memory((uintptr_t)scene+0x188,&local,8);if(local_read&&!local)safe_read_memory((uintptr_t)scene+0x190,&local,8);
 bool valid=client&&scene&&actor&&local_read&&local==(uintptr_t)actor&&sampled&&ui==2;
 std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;auto now=GetTickCount64();if(sampled&&ui!=2){context_tick=0;context_scene=0;}if(valid){session_runtime::sample(scene);context_scene=(uintptr_t)scene;context_tick=now;merge_live((uintptr_t)scene,now);}
 // Requests are bound to the sampled world, expire when the inventory closes,
 // and are consumed exactly once; no retries can duplicate a native event.
 for(auto& r:requests){if(r.state!=ANY_ITEM_REQUEST_QUEUED)continue;if((sampled&&ui!=2)||now-r.queued_at>2000||(valid&&r.scene!=(uintptr_t)scene)){r.state=ANY_ITEM_REQUEST_EXPIRED;continue;}if(!valid)continue;if(!permitted(r.allowed_modes)){r.state=ANY_ITEM_REQUEST_EXPIRED;continue;}r.state=dispatch(client,scene,r.id,catalog_runtime::category_is(r.id,"building"));char message[256];std::snprintf(message,sizeof(message),"INVENTORY_ACTION ticket=%llu id=%s state=%s native_instances=1 server_confirmation=NOT_OBSERVED",(unsigned long long)r.ticket,r.id,r.state==ANY_ITEM_REQUEST_SENT?"SENT":"FAILED");log_line(ANY_LOG_INFO,"framework",message);break;}
}
static DWORD WINAPI worker(void*){while(!ready&&!faulted){auto owners=scan_exact(INV_GIVE_OWNER,sizeof(INV_GIVE_OWNER));if(owners.size()==1){uintptr_t get{},send{},get_component{};if(p34_cell(owners[0],0xd068,get)&&p34_body(get,INV_LOOKUP)&&p34_cell(owners[0],0xd0b0,send)&&p34_body(send,INV_GIVE_EVENT)&&p34_cell(owners[0],0xd0e8,get_component)&&p34_body(get_component,INV_COMPONENT_LOOKUP)&&session_runtime::resolve()){lookup=(LookupFn)get;component_lookup=(LookupFn)get_component;give=(GiveFn)send;ready=true;log_line(ANY_LOG_INFO,"framework","INVENTORY_ACTION ready=1 mechanism=NATIVE_GIVE_EVENT thread=PLAYER_TICK direct_server_writes=0");break;}}Sleep(5000);}return 0;}
static const AnyInventoryActionsV1 api{sizeof(AnyInventoryActionsV1),1,available,add,state};
static const AnyInventoryActionsV2 api_v2{sizeof(AnyInventoryActionsV2),2,&api,add_mask};
}
static const AnyInventoryActionsV1* anyapi_inventory_actions_service(){return &inventory_actions::api;}
static void inventory_actions_tick(void* client,void* scene,void* actor){inventory_actions::tick(client,scene,actor);}

static const AnyInventoryActionsV2* anyapi_inventory_actions_service_v2(){return &inventory_actions::api_v2;}
