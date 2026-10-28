#pragma once
#include "anyapi_scene_lighting_v4.h"
#include "anyapi_scene_frame.h"
#include <algorithm>
#include <mutex>
#include <atomic>
#include <cmath>
namespace scene_lighting {
static float local_strength{};static uint32_t local_budget{4};
// V3 shaping. V1/V2 callers get zeros, which is the original look.
struct Shaping {float clarity{},beam_reach{};uint32_t sun_response{},temporal{};};static Shaping shaping;
// V4 extras (AnyAPI 0.40.2). V1-V3 setters store Extras{}: lamp_model 0, flags 0, atmosphere_flags 0, amd_path 0
// (auto), weather_response 0, shadows auto. That is exactly the 0.40.1 behaviour.
struct Extras {uint32_t v4{},lamp_budget{8},lamp_shadows{1},flags{},amd_path{},atmosphere_flags{};float weather_response{};};static Extras extras;
constexpr uint32_t ATMOS_SUPPORTED=ANY_ATMOS_SEA_PLANE|ANY_ATMOS_WEATHER_DENSITY|ANY_ATMOS_PHASE_CAP|ANY_ATMOS_FLASH_GLOW; // 120
static std::mutex mutex;static int owner=-1;static AnySceneLightingParametersV1 parameters;
static std::atomic<bool> available{},frame_valid{},gpu_ready{};static std::atomic<uint32_t> width{},height{},shadow_count{},reason{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
// Status v2 (last frame unless noted).
static std::atomic<uint32_t> lamps_drawn{},lamps_shadowed{},flashes_seen{}/*onsets, total*/,candidates_omni{},candidates_spot{},game_spot_maps{},
 lamp_shadows_active{},amd_path_active{},underground_active{},atmosphere_active{},raw29_hits{}/*total*/,raw29_misses{}/*total*/,ring_waits{}/*total*/,bypass_reason{}/*last non-zero*/;
static std::atomic<float> capture_us{-1};
static bool valid(const AnySceneLightingParametersV1& p){if(p.struct_size!=sizeof(p)||p.version!=1||p.enabled>1||p.quality>3)return false;auto range=[](float f,float lo,float hi){return std::isfinite(f)&&f>=lo&&f<=hi;};return range(p.fog_density,0,.1f)&&range(p.sun_shafts,0,15)&&range(p.fog_strength,0,2)&&range(p.height_falloff,0,1)&&range(p.base_height,-1000,10000)&&range(p.maximum_distance,1,2000)&&range(p.anisotropy,-.8f,.8f);}
static bool set_policy(const AnySceneLightingParametersV1* p,float local,uint32_t budget,Shaping extra={},Extras v4={}){int who=platform::current_plugin;if(who<0||!p||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;local_strength=local;local_budget=budget;shaping=extra;extras=v4;return true;}
static bool set(const AnySceneLightingParametersV1* p){return set_policy(p,0,4);}
static bool set_v2(const AnySceneLightingParametersV2* p){return p&&p->struct_size==sizeof(*p)&&p->version==2&&std::isfinite(p->local_strength)&&p->local_strength>=0&&p->local_strength<=15&&p->local_budget>=1&&p->local_budget<=8&&set_policy(&p->scene,p->local_strength,p->local_budget);}
static bool set_v3(const AnySceneLightingParametersV3* p){if(!p||p->struct_size!=sizeof(*p)||p->version!=3)return false;auto& l=p->lighting;
 if(l.struct_size!=sizeof(l)||l.version!=2||!std::isfinite(l.local_strength)||l.local_strength<0||l.local_strength>15||l.local_budget<1||l.local_budget>8)return false;
 if(!std::isfinite(p->clarity)||p->clarity<0||p->clarity>1||!std::isfinite(p->beam_reach)||(p->beam_reach!=0&&(p->beam_reach<10||p->beam_reach>2000))||p->sun_response>1||p->temporal>1)return false;
 return set_policy(&l.scene,l.local_strength,l.local_budget,{p->clarity,p->beam_reach,p->sun_response,p->temporal});}
// V4: shaped is checked as V3 (its local_budget is then ignored: lamp_budget wins, clamped to 8). Unsupported values
// are rejected, never silently changed, except lamp_budget above lamp_budget_max (clamped, as documented).
static bool valid_v4(const AnySceneLightingParametersV4& p){if(p.struct_size!=sizeof(p)||p.version!=4)return false;auto unit=[](float f,float hi){return std::isfinite(f)&&f>=0&&f<=hi;};
 for(auto r:p.reserved)if(r)return false;
 return p.lamp_model==0&&p.lamp_quality<=3&&p.lamp_budget>=1&&p.lamp_budget<=16&&p.lamp_shadows<=2&&p.lamp_shape<=3&&unit(p.weather_response,1)&&unit(p.ambient_washout,1)&&
  !(p.flags&~uint32_t(ANY_LAMP_UNDERGROUND|ANY_LAMP_TRACKING|ANY_LAMP_FLASHES|ANY_LAMP_OCEAN))&&p.native_halos==0&&p.amd_path<=3&&!(p.atmosphere_flags&~ATMOS_SUPPORTED)&&unit(p.sky_inscatter,2);}
static bool set_v4(const AnySceneLightingParametersV4* p){if(!p||!valid_v4(*p))return false;auto& v=p->shaped;auto& l=v.lighting;
 if(v.struct_size!=sizeof(v)||v.version!=3||l.struct_size!=sizeof(l)||l.version!=2||!std::isfinite(l.local_strength)||l.local_strength<0||l.local_strength>15||l.local_budget<1||l.local_budget>8)return false;
 if(!std::isfinite(v.clarity)||v.clarity<0||v.clarity>1||!std::isfinite(v.beam_reach)||(v.beam_reach!=0&&(v.beam_reach<10||v.beam_reach>2000))||v.sun_response>1||v.temporal>1)return false;
 Extras x{1,std::min(p->lamp_budget,8u),p->lamp_shadows,p->flags,p->amd_path,p->atmosphere_flags,p->weather_response};
 return set_policy(&l.scene,l.local_strength,x.lamp_budget,{v.clarity,v.beam_reach,v.sun_response,v.temporal},x);}
[[maybe_unused]] static Shaping copy_shaping(){std::lock_guard lock(mutex);return shaping;}
// Everything record() needs under one lock. An inactive owner gives enabled 0 (and Extras{} so nothing V4 leaks).
struct Policy {AnySceneLightingParametersV2 v2;Shaping shaping;Extras extras;};
static Policy copy_policy(){std::lock_guard lock(mutex);Policy p;p.v2.scene=parameters;p.v2.local_strength=local_strength;p.v2.local_budget=local_budget;p.shaping=shaping;p.extras=extras;if(owner<0||!anyapi_owner_active(owner)||!available){p.v2.scene.enabled=0;p.extras=Extras{};}return p;}
[[maybe_unused]] static AnySceneLightingParametersV2 copy_v2(){std::lock_guard lock(mutex);AnySceneLightingParametersV2 p;p.scene=parameters;p.local_strength=local_strength;p.local_budget=local_budget;if(owner<0||!anyapi_owner_active(owner)||!available)p.scene.enabled=0;return p;}
static AnySceneLightingParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneLightingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->frame_valid=frame_valid;s->ready=gpu_ready;s->width=width;s->height=height;s->shadow_count=shadow_count;s->reason=reason;s->frames=frames;s->rejected_frames=rejected;s->error=error;return true;}
static bool status_v2(AnySceneLightingStatusV2* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=2)return false;s->base.struct_size=sizeof(s->base);s->base.version=1;status(&s->base);
 s->lamps_drawn=lamps_drawn;s->lamps_shadowed=lamps_shadowed;s->flashes_seen=flashes_seen;s->candidates_omni=candidates_omni;s->candidates_spot=candidates_spot;s->game_spot_maps=game_spot_maps;
 s->lamp_budget_max=8;s->lamp_model_max=0;s->lamp_model_active=0;s->lamp_shadows_active=lamp_shadows_active;s->amd_path_active=amd_path_active;s->underground_active=underground_active;
 s->atmosphere_flags_supported=ATMOS_SUPPORTED;s->atmosphere_flags_active=atmosphere_active;s->raw29_hits=raw29_hits;s->raw29_misses=raw29_misses;s->ring_waits=ring_waits;s->bypass_reason=bypass_reason;
 s->capture_us=capture_us;for(auto& r:s->reserved)r=0;return true;}
static const AnySceneLightingV4 api_v4{sizeof(AnySceneLightingV4),4,set_v4,status,status_v2};
static const AnySceneLightingV3 api_v3{sizeof(AnySceneLightingV3),3,set_v3,status};
static const AnySceneLightingV2 api_v2{sizeof(AnySceneLightingV2),2,set_v2,status};
static const AnySceneLightingV1 api{sizeof(AnySceneLightingV1),1,set,status};
}
ANYAPI_REGISTER_SERVICE(scene_lighting_v4,"anyapi.scene_lighting",4,&scene_lighting::api_v4);
