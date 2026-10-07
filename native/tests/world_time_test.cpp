#include "../world_time_state.h"
#include <cassert>
#include <limits>
int main(){world_time::Store store;AnyWorldTimeSnapshotV1 s;
assert(!store.copy(&s,1000));store.sample(.5,1000);assert(store.copy(&s,1100)&&s.seconds_since_midnight==43200);
assert(!store.copy(&s,1501)&&s.sampled_tick==0);assert(!store.copy(&s,999));
store.sample(1.25,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==21600);
store.sample(-.25,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==64800);
store.sample(0,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==0);
store.sample(std::numeric_limits<double>::quiet_NaN(),2000);assert(!store.copy(&s,2000));
s.version=7;assert(!store.copy(&s,2000));assert(!store.copy(nullptr,2000));}
