#pragma once
#include "anyapi_scene_antialiasing_v1.h"
namespace scene_antialiasing {
static std::mutex mutex;static int owner=-1;static AnySceneAntialiasingParametersV1 parameters;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<uint32_t> width{},height{};static std::atomic<int32_t> error{};
static bool set(const AnySceneAntialiasingParametersV1* p){int who=platform::current_plugin;if(who<0||!p||p->struct_size!=sizeof(*p)||p->version!=1||p->enabled>1||(p->method!=1&&p->method!=2)||p->quality>3)return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;return true;}
static AnySceneAntialiasingParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static bool status(AnySceneAntialiasingStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->width=width;s->height=height;s->error=error;return true;}
static const AnySceneAntialiasingV1 api{sizeof(AnySceneAntialiasingV1),1,set,status};
}
