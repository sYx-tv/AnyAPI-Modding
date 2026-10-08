// Read connection records only on their owning server tick. Client rendering
// consumes IDs from a locked copy, never server object pointers.
#include "balance_assembly_contract.h"
#include <unordered_map>
namespace balance_runtime {
struct AssemblyLinks {uint64_t tick{};std::vector<int32_t> ids;};
static std::mutex links_mutex;static uintptr_t links_scene{};
static std::unordered_map<int32_t,AssemblyLinks> links;
static std::vector<int32_t> requested;
static std::unordered_map<uintptr_t,uintptr_t> connection_offsets;
static anyapi_runtime::CallCellHook server_hook;static std::atomic<bool> assembly_ready{};
using ServerTick=void(*)(void*,void*,const bool*);
struct NativeVector {uintptr_t buffer;int32_t offset,count,capacity,size;uintptr_t ctor,dtor;};
static bool vector_items(uintptr_t address,int expected,std::vector<uintptr_t>& out){
 NativeVector v{};if(!safe_read_memory(address,&v,sizeof(v))||v.size!=expected||v.count<0||v.count>4096||v.capacity<v.count||v.capacity>65536||v.offset<0||v.offset>=std::max(1,v.capacity)||(!v.buffer&&v.count))return false;
 for(int i=0;i<v.count;++i){uintptr_t object{};auto slot=v.buffer+uintptr_t((v.offset+i)%v.capacity)*expected+(expected==16?8:0);
  if(!safe_read_memory(slot,&object,8))return false;if(object)out.push_back(object);}
 return true;
}
static void sample_links(void* self,void* scene){
 int32_t id=-1;if(!safe_read_native(uintptr_t(self)+64,&id,4)||id<0)return;
 {std::lock_guard lock(links_mutex);auto it=links.find(id);if(std::find(requested.begin(),requested.end(),id)==requested.end())return;
  if(links_scene==uintptr_t(scene)&&it!=links.end()&&GetTickCount64()-it->second.tick<200)return;}
 std::vector<uintptr_t> grids;if(!vector_items(uintptr_t(self)+96,16,grids))return;
 AssemblyLinks entry;entry.tick=GetTickCount64();
 for(auto grid:grids){std::vector<uintptr_t> components;if(!vector_items(grid+96,16,components))return;
  for(auto component:components){uintptr_t fn{};if(!p34_virtual(component,78*8,fn))continue;
   auto cached=connection_offsets.find(fn);uintptr_t offset{};
   if(cached==connection_offsets.end()){unsigned char bytes[96]{};
    if(safe_read_memory(fn,bytes,sizeof(bytes)))for(auto& contract:balance_assembly_contract::LINKS)if(!memcmp(bytes,contract.bytes,contract.size)){offset=contract.offset;break;}
    connection_offsets[fn]=offset;
   }else offset=cached->second;
   if(!offset)continue;
   uintptr_t other{},parent{};int32_t other_id=-1;
   if(safe_read_memory(component+offset,&other,8)&&other&&safe_read_memory(other+320,&parent,8)&&parent&&safe_read_memory(parent+64,&other_id,4)&&other_id>=0&&other_id!=id)entry.ids.push_back(other_id);

  }
 }
 std::lock_guard lock(links_mutex);if(links_scene!=uintptr_t(scene)){links.clear();links_scene=uintptr_t(scene);}
 for(auto next:entry.ids)if(requested.size()<128&&std::find(requested.begin(),requested.end(),next)==requested.end())requested.push_back(next);
 links[id]=std::move(entry);
 if(links.size()>4096){auto now=GetTickCount64();for(auto it=links.begin();it!=links.end();)if(now-it->second.tick>2000)it=links.erase(it);else ++it;}
}
static void sample_links_safe(void* self,void* scene){__try{sample_links(self,scene);}__except(EXCEPTION_EXECUTE_HANDLER){}}
static void server_observe(void* self,void* scene,const bool* split){
 ((ServerTick)server_hook.original())(self,scene,split);
 if(allowed_item)sample_links_safe(self,scene);
}
static bool attach_assembly(){using namespace balance_assembly_contract;
 auto hits=scan_exact(SERVER_CTOR,sizeof(SERVER_CTOR));if(hits.size()!=1)return false;
 uintptr_t klass{},table{},cell{},fn{};
 if(!safe_read_memory(hits[0]+sizeof(SERVER_CTOR)+8,&klass,8)||!klass||!safe_read_memory(klass+0x80,&table,8)||!table||!safe_read_memory(table+9*8,&cell,8)||!cell||!safe_read_memory(cell,&fn,8)||!p34_body(fn,SERVER_TICK))return false;
 if(!server_hook.install((void**)cell,(void*)server_observe))return false;
 assembly_ready=true;log_line(ANY_LOG_INFO,"framework","BALANCE connections=1 source=OWNING_SERVER_TICK copied_ids_only=1");return true;
}
static std::vector<int32_t> assembly_ids(int32_t id,bool local){
 std::vector<int32_t> result{id};if(!local)return result;
 std::lock_guard lock(links_mutex);const auto now=GetTickCount64();
 if(std::find(requested.begin(),requested.end(),id)==requested.end()){requested={id};links.clear();}
 std::unordered_map<int32_t,std::vector<int32_t>> graph;
 for(auto& [key,entry]:links){if(now<entry.tick||now-entry.tick>1000)continue;graph[key];for(auto next:entry.ids){graph[key].push_back(next);graph[next].push_back(key);}}
 for(size_t i=0;i<result.size()&&result.size()<=128;++i){auto it=links.find(result[i]);
  if(it==links.end()||now<it->second.tick||now-it->second.tick>1000)return assembly_ready?std::vector<int32_t>{}:std::vector<int32_t>{id};
  for(auto next:graph[result[i]])if(std::find(result.begin(),result.end(),next)==result.end())result.push_back(next);
 }

 if(result.size()>128)return {id};std::sort(result.begin(),result.end());return result;
}
}
