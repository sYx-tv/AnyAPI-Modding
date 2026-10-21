#include "anyapi_item_catalog.h"
#include "item_catalog_preview.h"
#include "anyapi_item_images_v1.h"
#include <mutex>
namespace catalog_runtime {
static std::vector<itemcatalog::Entry> entries;
// Entries only ever append (mod-added definitions found in a loaded world), so indices stay stable.
// The mutex covers growth against copies on other threads.
static std::mutex entries_mutex,images_mutex;static std::map<uint32_t,std::vector<uint8_t>> images;
static bool image(uint32_t index,uint8_t* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(!required)return false;*required=128*128*4;if(!bytes||capacity<*required)return false;itemcatalog::Entry entry;{std::lock_guard lock(entries_mutex);if(index>=entries.size())return false;entry=entries[index];}std::lock_guard lock(images_mutex);try{auto it=images.find(index);if(it==images.end()){it=images.emplace(index,itemcatalog::preview(g_game_dir,entry)).first;}memcpy(bytes,it->second.data(),*required);return true;}catch(const std::exception& e){log_line(ANY_LOG_WARN,"framework",("ITEM_IMAGE unavailable=1 id="+std::string(entry.metadata.id)+" reason="+e.what()).c_str());return false;}}
static const AnyItemImagesV1 image_api{sizeof(AnyItemImagesV1),1,image};
static uint32_t count(){std::lock_guard lock(entries_mutex);return uint32_t(entries.size());}
static bool copy(uint32_t index,AnyItemDefinitionV1* out){if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;std::lock_guard lock(entries_mutex);if(index>=entries.size())return false;*out=entries[index].metadata;return true;}
static bool json(uint32_t index,char* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(!required)return false;std::lock_guard lock(entries_mutex);if(index>=entries.size())return false;auto& text=entries[index].json;*required=uint32_t(text.size()+1);if(!bytes||capacity<*required)return false;memcpy(bytes,text.c_str(),*required);return true;}
static const AnyItemCatalogV1 api{sizeof(AnyItemCatalogV1),1,count,copy,json};
static bool contains(const char* id){std::lock_guard lock(entries_mutex);for(auto& e:entries)if(!strcmp(e.metadata.id,id))return true;return false;}
static bool category_is(const char* id,const char* category){std::lock_guard lock(entries_mutex);for(auto& e:entries)if(!strcmp(e.metadata.id,id))return !strcmp(e.metadata.category,category);return false;}
// Appends definitions whose IDs are new; returns how many were added. The catalog stays capped at 10,000.
static size_t append(std::vector<itemcatalog::Entry>& found){std::lock_guard lock(entries_mutex);size_t added=0;for(auto& f:found){if(entries.size()>=10000)break;bool known=false;for(auto& e:entries)if(!strcmp(e.metadata.id,f.metadata.id)){known=true;break;}if(!known){entries.push_back(std::move(f));++added;}}return added;}
static void initialize(){try{entries=itemcatalog::load(g_game_dir);log_line(ANY_LOG_INFO,"framework",("ITEM_CATALOG ready=1 definitions="+std::to_string(entries.size())+" source=current_game_assets language=en read_only=1").c_str());}catch(const std::exception& e){entries.clear();log_line(ANY_LOG_ERROR,"framework",("ITEM_CATALOG ready=0 reason="+std::string(e.what())).c_str());}}
}
static const AnyItemCatalogV1* anyapi_item_catalog_service(){return &catalog_runtime::api;}

static const AnyItemImagesV1* anyapi_item_images_service(){return &catalog_runtime::image_api;}
