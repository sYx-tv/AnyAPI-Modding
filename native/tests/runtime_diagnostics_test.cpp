#include <cstddef>
#include <cassert>
#include <cstring>
#include "../runtime_diagnostics.h"
int main(){
 P31Window window;P31Counts before{},after{},delta{};bool reset=false;
 before[23]=7;after=before;after[42]=9;
 assert(!window.begin(1,165,before));assert(window.begin(1,23,before));
 assert(!window.begin(2,23,before));assert(!window.finish(2,after,delta,reset));
 assert(window.finish(1,after,delta,reset));assert(!reset && delta[23]==0 && delta[42]==9);
 assert(!std::strcmp(p31_window_status(true,false,reset,delta[23]),"SUCCESS_EXPECTED_ROUTE_ZERO"));
 assert(!window.begin(1,23,after));assert(window.begin(2,23,after));after[23]=9;
 assert(window.finish(2,after,delta,reset));assert(delta[23]==2);
 assert(!std::strcmp(p31_window_status(false,true,false,0),"USER_UNAVAILABLE"));
 assert(window.begin(3,23,after));after[42]=0;assert(window.finish(3,after,delta,reset));assert(reset);
 assert(!std::strcmp(p31_window_status(true,false,reset,2),"INVALID_COUNTER_RESET"));
}
