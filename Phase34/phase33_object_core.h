#pragma once
#include "anymaker_mod_extension.h"
#include <array>
#include <atomic>
#include <limits>

inline AnyObjectTokenV1 p33_token(uint64_t epoch,uint64_t generation,size_t slot,uint32_t kind) {
    return {epoch,generation,uint64_t(slot)+1,kind,0};
}
inline bool p33_token_shape(AnyObjectTokenV1 t,uint32_t kind,size_t capacity) {
    return t.world_epoch && t.object_generation && t.object_id &&
        t.object_id<=capacity && t.kind==kind && !t.reserved;
}
template<class Entry> bool p33_token_accepts(const Entry& e,AnyObjectTokenV1 t,uint64_t epoch) {
    return epoch && t.world_epoch==epoch && e.p33_epoch==epoch && e.live &&
        e.identity.live && e.identity.ticket.generation==t.object_generation;
}
// Caller owns synchronization. Journal overflow requires a fresh enumeration;
// it never pretends that omitted retirement notifications were delivered.
template<size_t Capacity> struct P33Invalidations {
    static_assert(Capacity>0);
    std::array<AnyInvalidationV1,Capacity> rows{};
    uint64_t sequence{};
    bool exhausted{};
    void push(AnyObjectTokenV1 token,uint32_t reason) {
        if(sequence==UINT64_MAX){exhausted=true;return;}
        ++sequence;rows[(sequence-1)%Capacity]={sequence,reason,0,token};
    }
    AnyExtResult read(uint64_t cursor,AnyInvalidationV1* out,size_t capacity,size_t* count,uint64_t* next) const {
        if(!count || !next || (capacity && !out))return ANY_EXT_BAD_ARGUMENT;
        *count=0;*next=cursor;
        if(cursor>sequence)return ANY_EXT_BAD_ARGUMENT;
        if(exhausted || sequence-cursor>Capacity){*next=sequence;return ANY_EXT_RESYNC_REQUIRED;}
        size_t available=size_t(sequence-cursor),n=available<capacity?available:capacity;
        for(size_t i=0;i<n;++i)out[i]=rows[(cursor+i)%Capacity];
        *count=n;*next=cursor+n;
        return n<available?ANY_EXT_BUFFER_TOO_SMALL:ANY_EXT_OK;
    }
};
inline uint64_t p33_advance_epoch(std::atomic<uint64_t>& epoch) {
    uint64_t old=epoch.load();
    do { if(!old || old==UINT64_MAX){epoch.store(0);return 0;} }
    while(!epoch.compare_exchange_weak(old,old+1));
    return old+1;
}
// Meaningful changes are emitted promptly; routine snapshots are bounded.
struct P33ReportLimiter {
    uint64_t last_tick{},last_transition{},last_errors{};
    bool initialized{};
    bool due(uint64_t tick,uint64_t transition,uint64_t errors) {
        bool emit=!initialized || transition!=last_transition || errors!=last_errors ||
            tick<last_tick || tick-last_tick>=60000;
        if(emit){initialized=true;last_tick=tick;last_transition=transition;last_errors=errors;}
        return emit;
    }
};
