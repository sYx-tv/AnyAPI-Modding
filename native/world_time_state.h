#pragma once
#include "anyapi_world_time_v1.h"
#include <cmath>
#include <mutex>
namespace world_time {
class Store {
 std::mutex mutex_;AnyWorldTimeSnapshotV1 value_;
public:
 void sample(double cycle,uint64_t now){AnyWorldTimeSnapshotV1 next;
  if(std::isfinite(cycle)&&std::abs(cycle)<1e6){next.cycle_factor=cycle-std::floor(cycle);next.seconds_since_midnight=uint32_t(next.cycle_factor*86400);next.sampled_tick=now;}
  std::lock_guard lock(mutex_);value_=next;
 }
 bool copy(AnyWorldTimeSnapshotV1* out,uint64_t now){if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;std::lock_guard lock(mutex_);*out={};if(!value_.sampled_tick||now<value_.sampled_tick||now-value_.sampled_tick>500)return false;*out=value_;return true;}
};
}
