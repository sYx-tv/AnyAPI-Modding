#define NOMINMAX
#include "../../sdk/experimental/include/anyapi_experimental.hpp"
#include "../../sdk/experimental/include/anymaker_sdk_types.hpp"
#include <cassert>
int main() {
    using namespace anymaker;
    assert(!experimental::matching_build()); // This test is not the supported game.
    assert(!experimental::native(0x1000));
    assert(!experimental::function(sym::server_tick));
    assert(pattern("GG").bytes.empty());
    assert(pattern("F").bytes.empty());
    assert(pattern("?? ??").bytes.empty());
    unsigned char code[]={0x48,0x8b,0x12};
    assert(pattern("48 8B ??").match(code));
    assert(!readable(code,SIZE_MAX));
    assert(!read_slot(nullptr,8));
    slot_route invalid{};invalid.nhops=9;assert(!follow_route(invalid));
    int items[]={1,2,3};gc_vector_raw v{};v.buffer=(uint8_t*)items;v.capacity=v.count=3;v.offset=2;v.element_size=sizeof(int);
    assert(*v.at<int>(0)==3&&*v.at<int>(1)==1);v.offset=-1;assert(!v.at<int>(0));v.offset=0;v.element_size=1;assert(!v.at<int>(0));
    void* cell=(void*)1;cell_hook hook;assert(!hook.install(&cell,nullptr));
}
