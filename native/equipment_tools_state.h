#pragma once
#include "anyapi_equipment_v2.h"
#include <mutex>
#include <deque>
#include <optional>
#include <cstring>
namespace equipment_tools {
inline bool tool_class(const char* s){return s&&strncmp(s,"vehicle_editor_",15)==0&&strcmp(s,"vehicle_editor_add_component")&&strcmp(s,"vehicle_editor_add_building_component");}
// A world reload forgets which slot the wheel owns, so a full hotbar would block every equip.
// Reclaim a slot that already holds a construction tool (the selected one first); other items stay protected.
inline int32_t reclaim_slot(const bool* holds_tool,int32_t count,int32_t selected){if(!holds_tool||count<=0)return -1;if(selected>=0&&selected<count&&holds_tool[selected])return selected;for(int32_t slot=0;slot<count;++slot)if(holds_tool[slot])return slot;return -1;}
struct Request {uint64_t ticket{},context{},queued_at{};int32_t item{-1};uint32_t state{ANY_EQUIPMENT_QUEUED};bool taken{};};
class Store {
 std::mutex mutex_;AnyEquipmentToolsSnapshotV2 snapshot_;std::deque<Request> jobs_;uint64_t ticket_{};
 bool fresh(uint64_t now)const{return snapshot_.context&&snapshot_.sampled_tick&&now>=snapshot_.sampled_tick&&now-snapshot_.sampled_tick<=500;}
 bool matches(int32_t item)const{for(uint32_t i=0;i<snapshot_.count;++i)if(snapshot_.tools[i].item_id==item)return true;return false;}
public:
 void publish(const AnyEquipmentToolsSnapshotV2& s){std::lock_guard lock(mutex_);snapshot_=s;if(s.count>ANY_EQUIPMENT_TOOLS_CAPACITY)snapshot_={};for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED&&(j.context!=snapshot_.context||!matches(j.item)))j.state=ANY_EQUIPMENT_EXPIRED;}
 void clear(){std::lock_guard lock(mutex_);snapshot_={};for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED)j.state=ANY_EQUIPMENT_EXPIRED;}
 bool copy(AnyEquipmentToolsSnapshotV2* out,uint64_t now){if(!out||out->struct_size!=sizeof(*out)||out->version!=2)return false;std::lock_guard lock(mutex_);*out={};if(!fresh(now))return false;*out=snapshot_;return true;}
 uint64_t equip(uint64_t context,int32_t item,uint64_t now){std::lock_guard lock(mutex_);if(!fresh(now)||context!=snapshot_.context||!matches(item))return 0;for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED)j.state=ANY_EQUIPMENT_EXPIRED;if(jobs_.size()==32)jobs_.pop_front();jobs_.push_back({++ticket_,context,now,item,ANY_EQUIPMENT_QUEUED,false});return ticket_;}
 std::optional<Request> take(uint64_t now){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.state==ANY_EQUIPMENT_QUEUED&&!j.taken){if(!fresh(now)||now<j.queued_at||now-j.queued_at>500||j.context!=snapshot_.context||!matches(j.item)){j.state=ANY_EQUIPMENT_EXPIRED;continue;}j.taken=true;return j;}return {};}
 void finish(uint64_t ticket,bool applied){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.ticket==ticket&&j.state==ANY_EQUIPMENT_QUEUED)j.state=applied?ANY_EQUIPMENT_APPLIED:ANY_EQUIPMENT_FAILED;}
 uint32_t state(uint64_t ticket){std::lock_guard lock(mutex_);for(auto& j:jobs_)if(j.ticket==ticket)return j.state;return ANY_EQUIPMENT_UNKNOWN;}
};
}
