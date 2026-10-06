#include "anyapi_item_catalog.h"
#include "item_catalog_preview.h"
#include "anyapi_item_images_v1.h"
#include <mutex>
namespace catalog_runtime {
static std::vector<itemcatalog::Entry> entries;
static std::mutex images_mutex;static std::map<uint32_t,std::vector<uint8_t>> images;
static bool image(uint32_t index,uint8_t* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(!required||index>=entries.size())return false;*required=128*128*4;if(!bytes||capacity<*required)return false;std::lock_guard lock(images_mutex);try{auto it=images.find(index);if(it==images.end()){it=images.emplace(index,itemcatalog::preview(g_game_dir,entries[index])).first;}memcpy(bytes,it->second.data(),*required);return true;}catch(const std::exception& e){log_line(ANY_LOG_WARN,"framework",("ITEM_IMAGE unavailable=1 id="+std::string(entries[index].metadata.id)+" reason="+e.what()).c_str());return false;}}
static const AnyItemImagesV1 image_api{sizeof(AnyItemImagesV1),1,image};
static uint32_t count(){return uint32_t(entries.size());}
static bool copy(uint32_t index,AnyItemDefinitionV1* out){if(!out||out->struct_size!=sizeof(*out)||out->version!=1||index>=entries.size())return false;*out=entries[index].metadata;return true;}
static bool json(uint32_t index,char* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(index>=entries.size()||!required)return false;auto& text=entries[index].json;*required=uint32_t(text.size()+1);if(!bytes||capacity<*required)return false;memcpy(bytes,text.c_str(),*required);return true;}
static const AnyItemCatalogV1 api{sizeof(AnyItemCatalogV1),1,count,copy,json};
static void initialize(){try{entries=itemcatalog::load(g_game_dir);log_line(ANY_LOG_INFO,"framework",("ITEM_CATALOG ready=1 definitions="+std::to_string(entries.size())+" source=current_game_assets language=en read_only=1").c_str());}catch(const std::exception& e){entries.clear();log_line(ANY_LOG_ERROR,"framework",("ITEM_CATALOG ready=0 reason="+std::string(e.what())).c_str());}}
}
static const AnyItemCatalogV1* anyapi_item_catalog_service(){return &catalog_runtime::api;}

static const AnyItemImagesV1* anyapi_item_images_service(){return &catalog_runtime::image_api;}
