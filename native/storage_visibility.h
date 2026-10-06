#pragma once
#include "anyapi_item_catalog_v1.h"
#include "storage_sort.h"
namespace storage_visibility {
inline bool visible(const AnyItemDefinitionV1& d){
 auto cls=storage_sort::lower(d.item_class),cat=storage_sort::lower(d.category),id=storage_sort::lower(d.id),name=storage_sort::lower(d.name);
 if(cls=="backpack")return true;
 if(cls=="clothing"||cat=="weapon"||cls.starts_with("gun_")||cls.find("torch")!=std::string::npos)return false;
 if(id.find("pouch")!=std::string::npos||name.find("pouch")!=std::string::npos||id.find("signal_detector")!=std::string::npos||name.find("signal detector")!=std::string::npos)return false;
 if(name.find("torch")!=std::string::npos)return false;
 // Category fallback covers every wearable variant, without hiding backpacks.
 for(auto wear:{"body","body_over","legs","feet","hands","head","hat","eyes","neck"})if(cat==wear)return false;
 return true;
}
}
