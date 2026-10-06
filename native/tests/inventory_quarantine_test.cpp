#include "anymaker_mod_api.h"
#include <cassert>
#include <cstdint>
#include <iostream>
static void p29_note_hook(size_t){}
using LONG64=int64_t;static volatile LONG64 g_p27_hook_calls[24]{};
static LONG64 InterlockedIncrement64(volatile LONG64* p){auto v=*p+1;*p=v;return v;}
static void p27_validate_server_virtuals(void*){}
#include "../legacy_inventory_observer.inc"
int main(){
    (void)&p26_server_tick;
    assert(!p26_register_pulse("fixture",nullptr,nullptr));size_t count=999;bool compatible=true;
    assert(p26_enumerate(1,nullptr,100,&count)==ANY_INVENTORY_UNAVAILABLE && count==0);
    assert(p26_can_merge(1,2,3,&compatible)==ANY_INVENTORY_UNAVAILABLE && !compatible);
    assert(p26_request_merge(1,2,3)==ANY_INVENTORY_UNAVAILABLE);
    assert(p26_enumerate(1,nullptr,0,nullptr)==ANY_INVENTORY_UNAVAILABLE);
    assert(p26_can_merge(1,2,3,nullptr)==ANY_INVENTORY_UNAVAILABLE);
    assert(g_p27_hook_calls[22]==0 && g_p26_server_tick_original==nullptr);
    std::cout<<"PASS: production inventory quarantine rejects requests, resets outputs, and invokes no native tick\n";
}
