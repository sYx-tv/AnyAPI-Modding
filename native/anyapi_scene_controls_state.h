#pragma once
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include <array>
#include <mutex>
#include <cmath>
#include <atomic>
namespace scene_controls {
static std::mutex mutex;
static AnySceneParametersV1 parameters;
static std::array<uint32_t,3> details{};
static int owner=-1;
static std::atomic<bool> ready{};
static std::atomic<uint64_t> renderer_calls{},scene_calls{},rejected_frames{};
static bool valid(const AnySceneParametersV1& p){
 if(p.struct_size!=sizeof(p)||p.version!=1||p.enabled>1||p.aa>2||p.bloom>2||p.ssao>2||p.shadows>2||p.fog_blur>2)return false;
 for(float f:{p.sun,p.sky,p.ambient,p.fog})if(!std::isfinite(f)||f<0||f>3)return false;
 return std::isfinite(p.bloom_threshold)&&p.bloom_threshold>=0&&p.bloom_threshold<=4&&std::isfinite(p.bloom_intensity)&&p.bloom_intensity>=0&&p.bloom_intensity<=2&&std::isfinite(p.light_exposure)&&std::abs(p.light_exposure)<=2;
}
static bool set(const AnySceneParametersV1* p){int who=platform::current_plugin;if(who<0||!p||!valid(*p))return false;std::lock_guard lock(mutex);
 if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;
 owner=who;parameters=*p;details={};return true;
}
struct Policy{AnySceneParametersV1 scene;std::array<uint32_t,3> details{};};
static Policy snapshot(){std::lock_guard lock(mutex);return owner>=0&&anyapi_owner_active(owner)&&ready?Policy{parameters,details}:Policy{};}
static AnySceneParametersV1 copy(){return snapshot().scene;}
static bool set_v2(const AnySceneParametersV2* p){int who=platform::current_plugin;if(who<0||!p||p->struct_size!=sizeof(*p)||p->version!=2||!valid(p->scene)||p->clouds>2||p->grass>2||p->foliage>2)return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p->scene;details={p->clouds,p->grass,p->foliage};return true;}
static bool status(AnySceneStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->ready=ready;s->renderer_calls=renderer_calls;s->scene_calls=scene_calls;s->rejected_frames=rejected_frames;return true;}
static const AnySceneControlsV2 api_v2{sizeof(AnySceneControlsV2),2,set_v2,status};
static const AnySceneControlsV1 api{sizeof(AnySceneControlsV1),1,set,status};
}
