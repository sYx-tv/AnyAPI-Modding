#pragma once
#include "anyapi_scene_antialiasing_v2.h"
namespace scene_antialiasing {
static std::mutex mutex;static int owner=-1;static AnySceneAntialiasingParametersV2 parameters;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<uint32_t> width{},height{};static std::atomic<int32_t> error{};
static bool set_policy(const AnySceneAntialiasingParametersV2& p){int who=platform::current_plugin;if(who<0||p.enabled>1||p.method<1||p.method>4||p.quality>3||!(p.sharpening>=0&&p.sharpening<=1))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p;return true;}
static bool set(const AnySceneAntialiasingParametersV1* p){if(!p||p->struct_size!=sizeof(*p)||p->version!=1||(p->method!=1&&p->method!=2))return false;AnySceneAntialiasingParametersV2 v;v.enabled=p->enabled;v.method=p->method;v.quality=p->quality;return set_policy(v);}
static bool set_v2(const AnySceneAntialiasingParametersV2* p){return p&&p->struct_size==sizeof(*p)&&p->version==2&&set_policy(*p);}
static AnySceneAntialiasingParametersV2 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneAntialiasingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->width=width;s->height=height;s->error=error;return true;}
static const AnySceneAntialiasingV1 api{sizeof(AnySceneAntialiasingV1),1,set,status};
static const AnySceneAntialiasingV2 api_v2{sizeof(AnySceneAntialiasingV2),2,set_v2,status};
}
