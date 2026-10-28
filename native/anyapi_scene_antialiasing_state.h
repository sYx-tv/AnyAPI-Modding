#pragma once
#include "anyapi_scene_antialiasing_v3.h"
#include "anyapi_scene_frame.h"
namespace scene_antialiasing {
// V3 extras. V1 and V2 setters store {0,0,1,0}: no history bias, legacy TAA
// candidate rules, output dither on (FIN-4 applies to every version), no testing switch.
struct Extras {uint32_t history_bias{},taa_flags{},dither{1},flags{};};
struct Policy {AnySceneAntialiasingParametersV2 base;Extras extras;};
static std::mutex mutex;static int owner=-1;static AnySceneAntialiasingParametersV2 parameters;static Extras extras;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<uint32_t> width{},height{};static std::atomic<int32_t> error{};
// K46 status_v2. temporal_active: TAA history was written last frame (also read by contact).
static std::atomic<bool> temporal_active{},jitter_active{};static std::atomic<uint64_t> jittered{},jitter_skipped{};
static std::atomic<uint32_t> fallback_reason{},editor_overlay{};static std::atomic<float> gpu_ms{-1.f};
static bool set_policy(const AnySceneAntialiasingParametersV2& p,const Extras& x){int who=platform::current_plugin;if(who<0||p.enabled>1||p.method<1||p.method>4||p.quality>3||!(p.sharpening>=0&&p.sharpening<=1)||x.history_bias>1||(x.taa_flags&~7u)||x.dither>1||(x.flags&~uint32_t(ANY_AA_AFTER_OVERLAYS|ANY_AA_OCEAN_DEPTH)))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p;extras=x;return true;}
static bool set_policy(const AnySceneAntialiasingParametersV2& p){return set_policy(p,Extras{});}
static bool set(const AnySceneAntialiasingParametersV1* p){if(!p||p->struct_size!=sizeof(*p)||p->version!=1||(p->method!=1&&p->method!=2))return false;AnySceneAntialiasingParametersV2 v;v.enabled=p->enabled;v.method=p->method;v.quality=p->quality;return set_policy(v);}
static bool set_v2(const AnySceneAntialiasingParametersV2* p){return p&&p->struct_size==sizeof(*p)&&p->version==2&&set_policy(*p);}
static bool set_v3(const AnySceneAntialiasingParametersV3* p){if(!p||p->struct_size!=sizeof(*p)||p->version!=3||p->base.struct_size!=sizeof(p->base)||p->base.version!=2)return false;for(auto r:p->reserved)if(r)return false;return set_policy(p->base,Extras{p->history_bias,p->taa_flags,p->dither,p->flags});}
static Policy copy_v3(){std::lock_guard lock(mutex);Policy p{parameters,extras};if(owner<0||!anyapi_owner_active(owner)||!available)p.base.enabled=0;return p;}
static AnySceneAntialiasingParametersV2 copy(){return copy_v3().base;}
static bool status(AnySceneAntialiasingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->width=width;s->height=height;s->error=error;return true;}
static bool status_v2(AnySceneAntialiasingStatusV2* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=2)return false;AnySceneAntialiasingStatusV1 base;if(!status(&base))return false;s->base=base;
 s->temporal_active=temporal_active;s->jitter_active=jitter_active;s->last_fallback_reason=fallback_reason;s->editor_overlay=editor_overlay;s->jittered_frames=jittered;s->jitter_skipped=jitter_skipped;s->gpu_ms=gpu_ms;for(auto& r:s->reserved)r=0;return true;}
static const AnySceneAntialiasingV1 api{sizeof(AnySceneAntialiasingV1),1,set,status};
static const AnySceneAntialiasingV2 api_v2{sizeof(AnySceneAntialiasingV2),2,set_v2,status};
static const AnySceneAntialiasingV3 api_v3{sizeof(AnySceneAntialiasingV3),3,set_v3,status,status_v2};
ANYAPI_REGISTER_SERVICE(scene_antialiasing_v3,"anyapi.scene_antialiasing",3,&api_v3);
}
