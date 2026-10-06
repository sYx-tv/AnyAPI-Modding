#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
struct P272Pattern {const unsigned char* bytes;size_t size;size_t id;};
class P272PatternIndex {
    std::unordered_map<uint64_t,std::vector<P272Pattern>> buckets;
public:
    size_t longest=8;size_t count=0;
    void add(P272Pattern p){if(p.size<8)return;uint64_t key=0;std::memcpy(&key,p.bytes,8);buckets[key].push_back(p);++count;if(p.size>longest)longest=p.size;}
    template<class Fn> void scan(const unsigned char* data,size_t size,size_t core,Fn match) const {
        for(size_t i=0;i<core && i+8<=size;++i){
            // Every audited entry begins with push or a REX prefix. Reject most bytes cheaply.
            auto first=data[i];if(first!=0x53 && first!=0x55 && first!=0x56 && first!=0x57 && first!=0x41 && first!=0x48 && first!=0x40 && first!=0x54)continue;
            uint64_t key=0;std::memcpy(&key,data+i,8);auto it=buckets.find(key);if(it==buckets.end())continue;
            for(const auto& p:it->second)if(p.size<=size-i && !std::memcmp(data+i,p.bytes,p.size))match(p.id,i);
        }
    }
};
