#include "../phase27_pointer_pool.h"
#include <cassert>
#include <random>
#include <unordered_map>
#include <iostream>
int main() {
    P27PointerPool<64,128> pool;std::unordered_map<uintptr_t,int> live;
    std::mt19937 random(27);
    for(int step=0;step<100000;++step) {
        uintptr_t key=((uintptr_t)(random()%256)+1)*16;
        if(random()%3==0) {
            auto it=live.find(key);if(it!=live.end()) {pool.retire(it->second);pool.retire(it->second);live.erase(it);}
        } else {
            bool changed=false;int slot=pool.ensure(key,changed);
            if(slot<0) assert(live.size()==64);
            else {
                auto previous=live.find(key);if(previous!=live.end()) assert(slot==previous->second && !changed);
                for(auto pair:live) if(pair.first!=key) assert(pair.second!=slot);
                live[key]=slot;
            }
        }
        for(auto pair:live) assert(pool.find(pair.first)==pair.second);
        assert(pool.highwater<=64);
    }
    std::cout<<"PASS: 100000 create/destroy/reuse operations; no live slot aliasing\n";
}
