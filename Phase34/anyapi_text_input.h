#pragma once
#include <cstdint>
// Decode Windows UTF-16 character messages without changing plugin ABI layouts.
struct AnyTextDecoder {
 uint32_t high{};
 bool decode(uint32_t unit,uint32_t& scalar){if(unit>=0xd800&&unit<=0xdbff){high=unit;return false;}if(unit>=0xdc00&&unit<=0xdfff){if(!high)return false;scalar=0x10000+((high-0xd800)<<10)+(unit-0xdc00);high=0;return true;}high=0;if(unit>0x10ffff)return false;scalar=unit;return true;}
 void reset(){high=0;}
};
