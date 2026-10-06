#pragma once
#include <cstdint>
// Current authored item meshes, rendered into copied transparent BGRA previews.
// Query anyapi.item_images v1 after ModReady. No native object or texture pointers.
struct AnyItemImagesV1 {
 uint32_t struct_size{sizeof(AnyItemImagesV1)},version{1};
 // Fixed 128 x 128, pitch 512, premultiplied BGRA; required is 65536 bytes.
 // Invalid index/asset returns false. Insufficient buffers remain unchanged.
 bool (*copy)(uint32_t index,uint8_t* bgra,uint32_t capacity,uint32_t* required){};
};
