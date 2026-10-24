#pragma once
#include "anyapi_scene_effects_v1.h"
#include "anyapi_scene_effects_v2.h"
#include <cmath>
namespace scene_effects {
static std::mutex mutex;static int owner=-1;static AnySceneEffectsParametersV1 parameters;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
static std::atomic<float> gpu_ms{-1},adapted_exposure{0},scene_luminance{-1},sun_visibility{0},ao_ms{-1};static std::atomic<bool> ao_available{},ao_ready{};static std::atomic<uint64_t> ao_frames{};static std::atomic<bool> sky_ready{};static std::atomic<float> sky_ms{-1},night{0},contact_ms{-1},gi_ms{-1},lens_ms{-1},distance_used{1};static std::atomic<bool> distance_available{},photo_available{};static std::atomic<bool> contact_ready{},gi_ready{},lens_ready{};
// v2: ocean reflections and sun glint; off unless a v2 owner set them.
struct Water{float reflections{},glint{};uint32_t quality{1};};static Water water;static std::atomic<bool> water_ready{};static std::atomic<float> water_ms{-1};static std::atomic<uint64_t> water_frames{};
inline bool in(float v,float low,float high){return std::isfinite(v)&&v>=low&&v<=high;}
static bool valid(const AnySceneEffectsParametersV1& p){return p.enabled<=1&&p.finish<=1&&p.ambient_occlusion<=1&&in(p.ao_radius,.25f,4)&&in(p.ao_strength,0,2)&&p.night_sky<=1&&in(p.stars,0,2)&&in(p.moon,0,2)&&in(p.moonlight,0,2)&&p.contact_shadows<=1&&in(p.contact_length,.05f,4)&&in(p.bounce_light,0,2)&&in(p.depth_of_field,0,4)&&in(p.motion_blur,0,2)&&in(p.draw_distance,1,4)&&in(p.sun_disc,0,2)&&in(p.clouds,0,2)&&p.hide_ui<=1&&p.tonemap<=3&&p.look<=6&&p.eye_adaptation<=1&&in(p.look_strength,0,1)&&in(p.exposure,-3,3)&&in(p.adaptation_seconds,.2f,10)&&in(p.bloom,0,1)&&in(p.glare,0,2)&&in(p.vignette,0,1);}
static bool set(const AnySceneEffectsParametersV1* p){int who=platform::current_plugin;if(!p||p->struct_size!=sizeof(*p)||p->version!=1||who<0||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;water={};return true;}
static bool set_v2(const AnySceneEffectsParametersV2* p){int who=platform::current_plugin;if(!p||p->struct_size!=sizeof(*p)||p->version!=2||p->effects.struct_size!=sizeof(p->effects)||p->effects.version!=1||who<0||!valid(p->effects)||!in(p->water_reflections,0,1)||!in(p->sun_glint,0,2)||p->reflection_quality>2)return false;
 std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p->effects;water={p->water_reflections,p->sun_glint,p->reflection_quality};return true;}
static AnySceneEffectsParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
static Water copy_water(){std::lock_guard lock(mutex);if(owner<0||!anyapi_owner_active(owner)||!available||!parameters.enabled)return {};return water;}
static bool status(AnySceneEffectsStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->gpu_ms=gpu_ready?gpu_ms.load():-1;s->adapted_exposure=adapted_exposure;s->scene_luminance=scene_luminance;s->sun_visibility=sun_visibility;s->error=error;s->ao_available=ao_available;s->ao_ready=ao_ready;s->ao_frames=ao_frames;s->ao_ms=ao_ready?ao_ms.load():-1;s->sky_ready=sky_ready;s->sky_ms=sky_ready?sky_ms.load():-1;s->night=night;s->contact_ready=contact_ready;s->contact_ms=contact_ready?contact_ms.load():-1;s->gi_ready=gi_ready;s->gi_ms=gi_ready?gi_ms.load():-1;s->lens_ready=lens_ready;s->lens_ms=lens_ready?lens_ms.load():-1;s->distance_available=distance_available;s->distance_scale=distance_used;s->photo_available=photo_available;return true;}
static bool status_v2(AnySceneEffectsStatusV2* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=2)return false;s->effects=AnySceneEffectsStatusV1{};if(!status(&s->effects))return false;s->water_ready=water_ready;s->water_ms=water_ready?water_ms.load():-1;s->water_frames=water_frames;return true;}
static const AnySceneEffectsV1 api{sizeof(AnySceneEffectsV1),1,set,status};
static const AnySceneEffectsV2 api_v2{sizeof(AnySceneEffectsV2),2,set_v2,status,status_v2};
}
