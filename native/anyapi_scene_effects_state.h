#pragma once
#include "anyapi_scene_effects_v1.h"
#include "anyapi_scene_effects_v2.h"
#include "anyapi_scene_effects_v3.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_scene_frame.h"
#include <atomic>
#include <cmath>
#include <mutex>
namespace scene_effects {
static std::mutex mutex;static int owner=-1;static AnySceneEffectsParametersV1 parameters;
static std::atomic<bool> available{},gpu_ready{};static std::atomic<uint64_t> frames{},rejected{};static std::atomic<int32_t> error{};
static std::atomic<float> gpu_ms{-1},adapted_exposure{0},scene_luminance{-1},sun_visibility{0},ao_ms{-1};static std::atomic<bool> ao_available{},ao_ready{};static std::atomic<uint64_t> ao_frames{};static std::atomic<bool> sky_ready{};static std::atomic<float> sky_ms{-1},night{0},contact_ms{-1},gi_ms{-1},lens_ms{-1},distance_used{1};static std::atomic<bool> distance_available{},photo_available{};static std::atomic<bool> contact_ready{},gi_ready{},lens_ready{};
// v2: ocean reflections and sun glint; off unless a v2 owner set them.
struct Water{float reflections{},glint{};uint32_t quality{1};};static Water water;static std::atomic<bool> water_ready{};static std::atomic<float> water_ms{-1};static std::atomic<uint64_t> water_frames{};
// v3: finish/shading extras. V1 and V2 setters reset them to these defaults, which give V2's output
// (dither 1 and the bug fixes in docs/api/scene-effects.md apply to every version).
struct Extras{uint32_t shading_revision{0},tone_revision{0};float bloom_falloff{1};uint32_t dither{1},flags{0},weather{0};};static Extras extras_v3;
static std::atomic<bool> native_ssao_skipped{},dither_active{};
inline bool in(float v,float low,float high){return std::isfinite(v)&&v>=low&&v<=high;}
static bool valid(const AnySceneEffectsParametersV1& p){return p.enabled<=1&&p.finish<=1&&p.ambient_occlusion<=1&&in(p.ao_radius,.25f,4)&&in(p.ao_strength,0,2)&&p.night_sky<=1&&in(p.stars,0,2)&&in(p.moon,0,2)&&in(p.moonlight,0,2)&&p.contact_shadows<=1&&in(p.contact_length,.05f,4)&&in(p.bounce_light,0,2)&&in(p.depth_of_field,0,4)&&in(p.motion_blur,0,2)&&in(p.draw_distance,1,4)&&in(p.sun_disc,0,2)&&in(p.clouds,0,2)&&p.hide_ui<=1&&p.tonemap<=3&&p.look<=6&&p.eye_adaptation<=1&&in(p.look_strength,0,1)&&in(p.exposure,-3,3)&&in(p.adaptation_seconds,.2f,10)&&in(p.bloom,0,1)&&in(p.glare,0,2)&&in(p.vignette,0,1);}
static bool valid_v2(const AnySceneEffectsParametersV2& p){return p.struct_size==sizeof(p)&&p.version==2&&p.effects.struct_size==sizeof(p.effects)&&p.effects.version==1&&valid(p.effects)&&in(p.water_reflections,0,1)&&in(p.sun_glint,0,2)&&p.reflection_quality<=2;}
// Caller holds mutex. The sky policy goes to the board for sky-water (V1/V2: all 0).
static void publish_policy(const scene_frame::SkyPolicy& policy){scene_frame::board.sky_policy.store(policy);}
static bool set(const AnySceneEffectsParametersV1* p){int who=platform::current_plugin;if(!p||p->struct_size!=sizeof(*p)||p->version!=1||who<0||!valid(*p))return false;std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=*p;water={};extras_v3={};publish_policy({});return true;}
static bool set_v2(const AnySceneEffectsParametersV2* p){int who=platform::current_plugin;if(!p||who<0||!valid_v2(*p))return false;
 std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p->effects;water={p->water_reflections,p->sun_glint,p->reflection_quality};extras_v3={};publish_policy({});return true;}
static bool set_v3(const AnySceneEffectsParametersV3* p){int who=platform::current_plugin;if(!p||p->struct_size!=sizeof(*p)||p->version!=3||who<0||!valid_v2(p->water))return false;
 if(p->sky_model>1||p->cloud_model>1||p->weather>1||p->moon_disc_follows_light>1||p->shading_revision>1||p->tone_revision>1||!in(p->bloom_falloff,.5f,1)||p->dither>1||(p->flags&~uint32_t(ANY_FX_KEEP_NATIVE_SSAO)))return false;
 for(auto r:p->reserved)if(r)return false;
 std::lock_guard lock(mutex);if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;parameters=p->water.effects;water={p->water.water_reflections,p->water.sun_glint,p->water.reflection_quality};
 extras_v3={p->shading_revision,p->tone_revision,p->bloom_falloff,p->dither,p->flags,p->weather};publish_policy({p->sky_model,p->cloud_model,p->weather,p->moon_disc_follows_light});return true;}
static AnySceneEffectsParametersV1 copy(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner)||!available)p.enabled=0;return p;}
// SC-16: photo mode and draw distance work without the lighting cell; they need only an active owner.
static AnySceneEffectsParametersV1 copy_owned(){std::lock_guard lock(mutex);auto p=parameters;if(owner<0||!anyapi_owner_active(owner))p.enabled=0;return p;}
static Extras copy_extras(){std::lock_guard lock(mutex);return extras_v3;}
// FIN-12: a finish that would change nothing (no copy, no composite). Controls uses it too (K04): such a finish never
// replaces the game's bloom.
inline bool finish_identity(const AnySceneEffectsParametersV1& p,const Extras& x){
 return p.tonemap==0&&(p.look==0||p.look_strength<=0)&&p.exposure==0&&!p.eye_adaptation&&p.bloom<=0&&p.glare<=0&&p.vignette<=0&&!x.dither;}
static Water copy_water(){std::lock_guard lock(mutex);if(owner<0||!anyapi_owner_active(owner)||!available||!parameters.enabled)return {};return water;}
static bool status(AnySceneEffectsStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->available=available;s->ready=gpu_ready;s->frames=frames;s->rejected_frames=rejected;s->gpu_ms=gpu_ready?gpu_ms.load():-1;s->adapted_exposure=adapted_exposure;s->scene_luminance=scene_luminance;s->sun_visibility=sun_visibility;s->error=error;s->ao_available=ao_available;s->ao_ready=ao_ready;s->ao_frames=ao_frames;s->ao_ms=ao_ready?ao_ms.load():-1;s->sky_ready=sky_ready;s->sky_ms=sky_ready?sky_ms.load():-1;s->night=night;s->contact_ready=contact_ready;s->contact_ms=contact_ready?contact_ms.load():-1;s->gi_ready=gi_ready;s->gi_ms=gi_ready?gi_ms.load():-1;s->lens_ready=lens_ready;s->lens_ms=lens_ready?lens_ms.load():-1;s->distance_available=distance_available;s->distance_scale=distance_used;s->photo_available=photo_available;return true;}
static bool status_v2(AnySceneEffectsStatusV2* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=2)return false;s->effects=AnySceneEffectsStatusV1{};if(!status(&s->effects))return false;s->water_ready=water_ready;s->water_ms=water_ready?water_ms.load():-1;s->water_frames=water_frames;return true;}
// v3 status: base is status_v2; the rest comes from the board and this package's passes.
static bool status_v3(AnySceneEffectsStatusV3* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=3)return false;s->base=AnySceneEffectsStatusV2{};if(!status_v2(&s->base))return false;
 auto& b=scene_frame::board;for(int i=0;i<16;++i)s->pass_error[i]=b.pass_error[i].load(std::memory_order_relaxed);
 scene_frame::SkyPolicy policy{};b.sky_policy.load(policy);auto p=copy();uint64_t now=b.frame.load(std::memory_order_relaxed);
 auto recent=[&](uint64_t frame){return frame&&now>=frame&&now-frame<=2;};
 scene_frame::Clock clock{};bool clock_ok=b.clock.load(clock)&&recent(clock.frame)&&clock.source!=0;
 scene_frame::Weather weather{};bool weather_ok=b.weather.load(weather)&&recent(weather.frame)&&weather.source==1;
 s->clock_valid=clock_ok;s->sky_model_active=p.enabled&&policy.sky_model&&sky_ready&&clock_ok;s->cloud_model_active=p.enabled&&policy.cloud_model&&sky_ready&&p.clouds>0;
 s->weather_active=p.enabled&&policy.weather&&weather_ok;s->native_ssao_skipped=native_ssao_skipped;s->dither_active=dither_active;s->sun_visibility=sun_visibility;return true;}
static const AnySceneEffectsV1 api{sizeof(AnySceneEffectsV1),1,set,status};
static const AnySceneEffectsV2 api_v2{sizeof(AnySceneEffectsV2),2,set_v2,status,status_v2};
static const AnySceneEffectsV3 api_v3{sizeof(AnySceneEffectsV3),3,set_v3,status,status_v2,status_v3};
}
ANYAPI_REGISTER_SERVICE(scene_effects_v3,"anyapi.scene_effects",3,&scene_effects::api_v3);
