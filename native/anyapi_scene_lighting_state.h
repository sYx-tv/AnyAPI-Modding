#pragma once
#include "anyapi_scene_lighting_v1.h"
#include <mutex>
#include <atomic>
#include <cmath>
namespace scene_lighting {
static std::mutex mutex;static int owner=-1;static AnySceneLightingParametersV1 parameters;
static std::atomic<bool> available{},frame_valid{},gpu_ready{};static std::atomic<uint32_t> width{},height{},shadow_count{},reason{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
static bool valid(const AnySceneLightingParametersV1& p){if(p.struct_size!=sizeof(p)||p.version!=1||p.enabled>1||p.quality>3)return false;auto range=[](float f,float lo,float hi){return std::isfinite(f)&&f>=lo&&f<=hi;};return range(p.fog_density,0,.1f)&&range(p.sun_shafts,0,5)&&range(p.fog_strength,0,2)&&range(p.height_falloff,0,1)&&range(p.base_height,-1000,10000)&&range(p.maximum_distance,1,2000)&&range(p.anisotropy,-.8f,.8f);}
static bool set(const AnySceneLightingParametersV1* p){int who=platform::current_plugin;if(who<0||!p||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;return true;}
static AnySceneLightingParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneLightingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->frame_valid=frame_valid;s->ready=gpu_ready;s->width=width;s->height=height;s->shadow_count=shadow_count;s->reason=reason;s->frames=frames;s->rejected_frames=rejected;s->error=error;return true;}
static const AnySceneLightingV1 api{sizeof(AnySceneLightingV1),1,set,status};
}
