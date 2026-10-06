#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
// Fixed storage: no allocator calls on game lifecycle hooks. Dead slots are
// recycled in O(1); pointer lookup uses hashed chains rather than a full scan.
template<size_t Capacity,size_t Buckets> struct P27PointerPool {
    static_assert((Buckets&(Buckets-1))==0);
    uintptr_t keys[Capacity]{};
    int heads[Buckets],chain[Capacity],free_next[Capacity],free_prev[Capacity];
    bool free_slot[Capacity]{};
    int highwater=0,free_head=-1;
    P27PointerPool() {std::fill_n(heads,Buckets,-1);}
    size_t bucket(uintptr_t key) const {
        uint64_t v=(uint64_t)key;v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;v^=v>>31;return (size_t)v&(Buckets-1);
    }
    int find(uintptr_t key) const {
        if(!key) return -1;
        for(int i=heads[bucket(key)];i>=0;i=chain[i]) if(keys[i]==key) return i;
        return -1;
    }
    void unfree(int i) {
        if(!free_slot[i]) return;
        if(free_prev[i]>=0) free_next[free_prev[i]]=free_next[i];else free_head=free_next[i];
        if(free_next[i]>=0) free_prev[free_next[i]]=free_prev[i];
        free_slot[i]=false;
    }
    void retire(int i) {
        if(i<0 || i>=highwater || free_slot[i]) return;
        free_slot[i]=true;free_prev[i]=-1;free_next[i]=free_head;
        if(free_head>=0) free_prev[free_head]=i;
        free_head=i;
    }
    int ensure(uintptr_t key,bool& replaced) {
        replaced=false;if(!key) return -1;
        int found=find(key);if(found>=0) {unfree(found);return found;}
        int slot=free_head;
        if(slot>=0) {
            unfree(slot);
            int* link=&heads[bucket(keys[slot])];
            while(*link>=0 && *link!=slot) link=&chain[*link];
            if(*link==slot) *link=chain[slot];
        } else {
            if(highwater==(int)Capacity) return -1;
            slot=highwater++;
        }
        replaced=true;keys[slot]=key;size_t hash=bucket(key);chain[slot]=heads[hash];heads[hash]=slot;return slot;
    }
};
