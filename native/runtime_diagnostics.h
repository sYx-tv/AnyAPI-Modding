#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
// Pure comparison of real monotonically increasing observations, no native calls.
constexpr std::size_t P31_HOOK_COUNT=165;
using P31Counts=std::array<uint64_t,P31_HOOK_COUNT>;
struct P31Window {
    P31Counts before{}; uint64_t sequence=0; std::size_t expected=0; bool active=false;
    bool begin(uint64_t seq,std::size_t slot,const P31Counts& counts) {
        if(active || seq<=sequence || slot>=P31_HOOK_COUNT)return false;
        before=counts;sequence=seq;expected=slot;active=true;return true;
    }
    bool finish(uint64_t seq,const P31Counts& after,P31Counts& delta,bool& reset) {
        if(!active || seq!=sequence)return false;
        reset=false;
        for(std::size_t i=0;i<P31_HOOK_COUNT;++i){if(after[i]<before[i])reset=true;delta[i]=after[i]>=before[i]?after[i]-before[i]:0;}
        active=false;return true;
    }
};
inline const char* p31_window_status(bool success,bool unavailable,bool reset,uint64_t expected_delta) {
    if(reset)return "INVALID_COUNTER_RESET";
    if(unavailable)return "USER_UNAVAILABLE";
    if(success && !expected_delta)return "SUCCESS_EXPECTED_ROUTE_ZERO";
    if(success)return "SUCCESS_ROUTE_OBSERVED_SEMANTICS_PENDING";
    return expected_delta?"NOT_CONFIRMED_ROUTE_OBSERVED":"NOT_CONFIRMED_NO_EXPECTED_ROUTE";
}
