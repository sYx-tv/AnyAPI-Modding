#pragma once
#include "anyapi_equipment_v1.h"
#include <mutex>
#include <deque>
#include <optional>
namespace equipment {
struct Request {uint64_t ticket{},context{},queued_at{};int32_t slot{},item{};uint32_t state{ANY_EQUIPMENT_QUEUED};};
class Store {
 std::mutex mutex_;AnyEquipmentSnapshotV1 snapshot_;std::deque<Request> jobs_;uint64_t ticket_{};
 bool fresh(uint64_t now)const{return snapshot_.context&&snapshot_.sampled_tick&&now>=snapshot_.sampled_tick&&now-snapshot_.sampled_tick<=500;}
 bool matches(int32_t slot,int32_t item)const{for(uint32_t i=0;i<snapshot_.count;++i)if(snapshot_.slots[i].slot_index==slot&&snapshot_.slots[i].item_id==item)return true;return false;}
public:
 void publish(const AnyEquipmentSnapshotV1& s){std::lock_guard lock(mutex_);snapshot_=s;if(s.count>ANY_EQUIPMENT_CAPACITY)snapshot_={};}
 void clear(){std::lock_guard lock(mutex_);snapshot_={};for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED)j.state=ANY_EQUIPMENT_EXPIRED;}
 bool copy(AnyEquipmentSnapshotV1* out,uint64_t now){if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;std::lock_guard lock(mutex_);*out={};if(!fresh(now))return false;*out=snapshot_;return true;}
 uint64_t select(uint64_t context,int32_t slot,int32_t item,uint64_t now){std::lock_guard lock(mutex_);if(!fresh(now)||context!=snapshot_.context||!matches(slot,item))return 0;
  for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED)j.state=ANY_EQUIPMENT_EXPIRED;
  if(jobs_.size()==32)jobs_.pop_front();jobs_.push_back({++ticket_,context,now,slot,item,ANY_EQUIPMENT_QUEUED});return ticket_;}
 std::optional<Request> take(uint64_t now){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED){if(!fresh(now)||now<j.queued_at||now-j.queued_at>500||j.context!=snapshot_.context||!matches(j.slot,j.item)){j.state=ANY_EQUIPMENT_EXPIRED;continue;}j.state=ANY_EQUIPMENT_FAILED;return j;}return {};}
 void finish(uint64_t ticket,bool applied){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.ticket==ticket)j.state=applied?ANY_EQUIPMENT_APPLIED:ANY_EQUIPMENT_FAILED;}
 uint32_t state(uint64_t ticket){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.ticket==ticket)return j.state;return ANY_EQUIPMENT_UNKNOWN;}
};
}
