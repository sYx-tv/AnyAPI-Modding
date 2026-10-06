#include "anyapi_services_v1.h"
#include "anyapi_item_images_v1.h"
#include <vector>
// Catalog indices belong to this process and build. Copy pixels before rendering.
std::vector<uint8_t> image_for(uint32_t index){auto services=AnyAPI_Services();auto api=services?(const AnyItemImagesV1*)services->query("anyapi.item_images",1):nullptr;std::vector<uint8_t> pixels(65536);uint32_t required{};if(!api||api->struct_size!=sizeof(*api)||api->version!=1||!api->copy(index,pixels.data(),uint32_t(pixels.size()),&required))pixels.clear();return pixels;}
