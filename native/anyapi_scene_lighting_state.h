#pragma once
#include "anyapi_scene_lighting_v3.h"
#include <mutex>
#include <atomic>
#include <cmath>
namespace scene_lighting {
static float local_strength{};static uint32_t local_budget{4};
// V3 shaping. V1/V2 callers get zeros, which is the original look.
struct Shaping {float clarity{},beam_reach{};uint32_t sun_response{},temporal{};};static Shaping shaping;
static std::mutex mutex;static int owner=-1;static AnySceneLightingParametersV1 parameters;
static std::atomic<bool> available{},frame_valid{},gpu_ready{};static std::atomic<uint32_t> width{},height{},shadow_count{},reason{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
static bool valid(const AnySceneLightingParametersV1& p){if(p.struct_size!=sizeof(p)||p.version!=1||p.enabled>1||p.quality>3)return false;auto range=[](float f,float lo,float hi){return std::isfinite(f)&&f>=lo&&f<=hi;};return range(p.fog_density,0,.1f)&&range(p.sun_shafts,0,15)&&range(p.fog_strength,0,2)&&range(p.height_falloff,0,1)&&range(p.base_height,-1000,10000)&&range(p.maximum_distance,1,2000)&&range(p.anisotropy,-.8f,.8f);}
static bool set_policy(const AnySceneLightingParametersV1* p,float local,uint32_t budget,Shaping extra={}){int who=platform::current_plugin;if(who<0||!p||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;local_strength=local;local_budget=budget;shaping=extra;return true;}
static bool set(const AnySceneLightingParametersV1* p){return set_policy(p,0,4);}
static bool set_v2(const AnySceneLightingParametersV2* p){return p&&p->struct_size==sizeof(*p)&&p->version==2&&std::isfinite(p->local_strength)&&p->local_strength>=0&&p->local_strength<=15&&p->local_budget>=1&&p->local_budget<=8&&set_policy(&p->scene,p->local_strength,p->local_budget);}
static bool set_v3(const AnySceneLightingParametersV3* p){if(!p||p->struct_size!=sizeof(*p)||p->version!=3)return false;auto& l=p->lighting;
 if(l.struct_size!=sizeof(l)||l.version!=2||!std::isfinite(l.local_strength)||l.local_strength<0||l.local_strength>15||l.local_budget<1||l.local_budget>8)return false;
 if(!std::isfinite(p->clarity)||p->clarity<0||p->clarity>1||!std::isfinite(p->beam_reach)||(p->beam_reach!=0&&(p->beam_reach<10||p->beam_reach>2000))||p->sun_response>1||p->temporal>1)return false;
 return set_policy(&l.scene,l.local_strength,l.local_budget,{p->clarity,p->beam_reach,p->sun_response,p->temporal});}
static Shaping copy_shaping(){std::lock_guard lock(mutex);return shaping;}
static AnySceneLightingParametersV2 copy_v2(){std::lock_guard lock(mutex);AnySceneLightingParametersV2 p;p.scene=parameters;p.local_strength=local_strength;p.local_budget=local_budget;if(owner<0||!anyapi_owner_active(owner)||!available)p.scene.enabled=0;return p;}
static AnySceneLightingParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneLightingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->frame_valid=frame_valid;s->ready=gpu_ready;s->width=width;s->height=height;s->shadow_count=shadow_count;s->reason=reason;s->frames=frames;s->rejected_frames=rejected;s->error=error;return true;}
static const AnySceneLightingV3 api_v3{sizeof(AnySceneLightingV3),3,set_v3,status};
static const AnySceneLightingV2 api_v2{sizeof(AnySceneLightingV2),2,set_v2,status};
static const AnySceneLightingV1 api{sizeof(AnySceneLightingV1),1,set,status};
}
