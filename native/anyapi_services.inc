#include "anyapi_ui_state_v1.h"
#include "anyapi_build_v1.h"
static const AnyBuildV1* anyapi_build_service();
#include "anyapi_client_tasks_v1.h"
static const AnyClientTasksV1* anyapi_client_tasks_service();
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_post_process_v1.h"
static const AnyPostProcessV1* anyapi_post_process_service();
static const AnyGpuDrawV1* anyapi_gpu_draw_service();
#include "anyapi_inventory_ui_v1.h"
#include "anyapi_inventory_ui_v2.h"
#include "anyapi_inventory_ui_v3.h"
static const AnyInventoryUiV3* anyapi_inventory_ui_service_v3();
static const AnyInventoryUiV2* anyapi_inventory_ui_service_v2();
static const AnyInventoryUiV1* anyapi_inventory_ui_service();
static const AnyUiStateV1* anyapi_ui_state_service();
#include "anyapi_session_v1.h"
#include "anyapi_inventory_actions_v2.h"
static const AnySessionV1* anyapi_session_service();
static const AnyInventoryActionsV2* anyapi_inventory_actions_service_v2();
#include "anyapi_services_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_item_images_v1.h"
#include "anyapi_inventory_actions_v1.h"
#include "anyapi_screen_layout_v1.h"
static const AnyScreenLayoutV1* anyapi_screen_layout_service();
static const AnyItemImagesV1* anyapi_item_images_service();
static const AnyInventoryActionsV1* anyapi_inventory_actions_service();
static const AnyItemCatalogV1* anyapi_item_catalog_service();
#include "anyapi_menu_v3.h"
#include <mutex>
#include <algorithm>
#include <string>
#include <vector>
namespace platform {static thread_local int current_plugin=-1;}
static const AnyMenuV1* anyapi_menu_service();
static const AnyMenuV2* anyapi_menu_service_v2();
static const AnyMenuV3* anyapi_menu_service_v3();
static bool anyapi_owner_active(int);
static void anyapi_owner_fault(int);
#include "anyapi_scene_controls_state.h"
#include "anyapi_scene_antialiasing_state.h"
#include "anyapi_scene_lighting_state.h"
namespace services {
struct Published {std::string id;uint32_t version;const void* value;int owner;};
struct Filter {uint32_t(*call)(const AnyInputV1*,void*);void* user;int32_t priority;int owner;bool faulted{};};
static std::mutex mutex;static std::vector<Published> published;static std::vector<Filter> filters;
static bool identifier(const char* s){if(!s||!*s||strlen(s)>95)return false;for(;*s;++s)if(!(*s>='a'&&*s<='z'||*s>='0'&&*s<='9'||*s=='.'||*s=='_'||*s=='-'))return false;return true;}
// Query tables are immutable and process-lifetime; never unload their provider.
static const void* query(const char* id,uint32_t version){if(!identifier(id))return nullptr;if(!strcmp(id,"anyapi.scene_lighting"))return version==1?&scene_lighting::api:nullptr;if(!strcmp(id,"anyapi.scene_antialiasing"))return version==1?&scene_antialiasing::api:nullptr;if(!strcmp(id,"anyapi.scene_controls"))return version==1?(const void*)&scene_controls::api:version==2?(const void*)&scene_controls::api_v2:nullptr;if(!strcmp(id,"anyapi.client_tasks"))return version==1?anyapi_client_tasks_service():nullptr;if(!strcmp(id,"anyapi.build"))return version==1?anyapi_build_service():nullptr;if(!strcmp(id,"anyapi.post_process"))return version==1?anyapi_post_process_service():nullptr;if(!strcmp(id,"anyapi.gpu_draw"))return version==1?anyapi_gpu_draw_service():nullptr;if(!strcmp(id,"anyapi.inventory_ui"))return version==1?(const void*)anyapi_inventory_ui_service():version==2?(const void*)anyapi_inventory_ui_service_v2():version==3?(const void*)anyapi_inventory_ui_service_v3():nullptr;if(!strcmp(id,"anyapi.ui_state"))return version==1?anyapi_ui_state_service():nullptr;if(!strcmp(id,"anyapi.screen_layout"))return version==1?anyapi_screen_layout_service():nullptr;if(!strcmp(id,"anyapi.item_images"))return version==1?anyapi_item_images_service():nullptr;if(!strcmp(id,"anyapi.session"))return version==1?anyapi_session_service():nullptr;if(!strcmp(id,"anyapi.inventory_actions"))return version==1?(const void*)anyapi_inventory_actions_service():version==2?(const void*)anyapi_inventory_actions_service_v2():nullptr;if(!strcmp(id,"anyapi.item_catalog"))return version==1?anyapi_item_catalog_service():nullptr;if(!strcmp(id,"anyapi.menu"))return version==1?(const void*)anyapi_menu_service():version==2?(const void*)anyapi_menu_service_v2():version==3?(const void*)anyapi_menu_service_v3():nullptr;
 std::lock_guard<std::mutex> lock(mutex);for(auto& p:published)if(p.id==id&&p.version==version&&anyapi_owner_active(p.owner))return p.value;return nullptr;}
// A provider cannot replace an existing table or claim the framework namespace.
static bool publish(const char* id,uint32_t version,const void* value){if(platform::current_plugin<0||!identifier(id)||!strncmp(id,"anyapi.",7)||!version||!value)return false;
 std::lock_guard<std::mutex> lock(mutex);if(published.size()>=64)return false;for(auto& p:published)if(p.id==id&&p.version==version)return false;
 published.push_back({id,version,value,platform::current_plugin});log_line(ANY_LOG_INFO,"framework",("SERVICE_REGISTER id="+std::string(id)+" version="+std::to_string(version)).c_str());return true;}
// Filters run before ordinary input callbacks, highest priority first; ties keep registration order.
static bool add_filter(uint32_t(*fn)(const AnyInputV1*,void*),void* user,int32_t priority){if(platform::current_plugin<0||!fn)return false;std::lock_guard<std::mutex> lock(mutex);
 if(filters.size()>=32)return false;filters.push_back({fn,user,priority,platform::current_plugin});std::stable_sort(filters.begin(),filters.end(),[](auto& a,auto& b){return a.priority>b.priority;});return true;}
static uint32_t safe_filter(Filter* f,const AnyInputV1* e){__try{return f->call(e,f->user);}__except(EXCEPTION_EXECUTE_HANDLER){f->faulted=true;return 0;}}
static bool input(const AnyInputV1& e){std::vector<Filter> local;{std::lock_guard<std::mutex> lock(mutex);local=filters;}
 bool consumed=false;for(auto& f:local){if(f.faulted||!anyapi_owner_active(f.owner))continue;int previous=platform::current_plugin;platform::current_plugin=f.owner;auto used=safe_filter(&f,&e);platform::current_plugin=previous;
  if(f.faulted){anyapi_owner_fault(f.owner);std::lock_guard<std::mutex> lock(mutex);for(auto& actual:filters)if(actual.owner==f.owner&&actual.call==f.call)actual.faulted=true;log_line(ANY_LOG_ERROR,"framework","INPUT_FILTER disabled_after_exception=1");}
  consumed=consumed||used;if(used&&e.kind!=ANY_FOCUS_LOST)break;
 }return consumed;}
static const AnyServicesV1 api{sizeof(AnyServicesV1),1,query,publish,add_filter};
}
extern "C" __declspec(dllexport) const AnyServicesV1* AnyAPI_GetServices(uint32_t version){return version==1?&services::api:nullptr;}
