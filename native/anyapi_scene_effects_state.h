#pragma once
#include "anyapi_scene_effects_v1.h"
#include <cmath>
namespace scene_effects {
static std::mutex mutex;static int owner=-1;static AnySceneEffectsParametersV1 parameters;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
static std::atomic<float> gpu_ms{-1},adapted_exposure{0},scene_luminance{-1},sun_visibility{0};
inline bool in(float v,float low,float high){return std::isfinite(v)&&v>=low&&v<=high;}
static bool valid(const AnySceneEffectsParametersV1& p){return p.enabled<=1&&p.tonemap<=2&&p.look<=5&&p.eye_adaptation<=1&&in(p.look_strength,0,1)&&in(p.exposure,-3,3)&&in(p.adaptation_seconds,.2f,10)&&in(p.bloom,0,1)&&in(p.glare,0,2)&&in(p.vignette,0,1);}
static bool set(const AnySceneEffectsParametersV1* p){int who=platform::current_plugin;if(!p||p->struct_size!=sizeof(*p)||p->version!=1||who<0||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;return true;}
static AnySceneEffectsParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneEffectsStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->gpu_ms=gpu_ready?gpu_ms.load():-1;s->adapted_exposure=adapted_exposure;s->scene_luminance=scene_luminance;s->sun_visibility=sun_visibility;s->error=error;return true;}
static const AnySceneEffectsV1 api{sizeof(AnySceneEffectsV1),1,set,status};
}
