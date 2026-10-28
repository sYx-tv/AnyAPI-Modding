#pragma once
#include "anyapi_scene_frame.h"
#include "scene_celestial.h"
#include "world_time_state.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_controls_v3.h"
#include "anyapi_scene_controls_v4.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <limits>
#include <string>
namespace scene_controls {
static std::mutex mutex;
static AnySceneParametersV1 parameters;
static std::array<uint32_t,3> details{};
// v3: light tints (key, shade) and the render-only wind scale; identity unless a v3 owner set them.
struct Look{float key[3]{1,1,1},shade[3]{1,1,1},wind{1};};
static Look look;
// v4 extras. Every V1-V3 setter stores Extras{}: V3 tint targets, static tints, no moonlight, no fog keep (today's output).
enum Palette:uint32_t{NOON_KEY,NOON_SHADE,GOLDEN_KEY,GOLDEN_SHADE,BLUE_KEY,BLUE_SHADE,MOON_KEY,MOON_SHADE,PALETTES};
struct Extras{uint32_t tint_targets{0},time_shaped{0};float palette[PALETTES][3]{{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1}};
 uint32_t moon_mode{0};float moonlight{0},moon_phase_floor{.35f};uint32_t weather_fog_keep{0};};
static Extras extras;
static int owner=-1;
static std::atomic<bool> ready{};
static std::atomic<uint64_t> renderer_calls{},scene_calls{},rejected_frames{};
static std::atomic<uint32_t> tint_targets_active{};   // status v2: the mapping scene_hook used last frame (0 when the policy is off)
static bool valid(const AnySceneParametersV1& p){
 if(p.struct_size!=sizeof(p)||p.version!=1||p.enabled>1||p.aa>2||p.bloom>2||p.ssao>2||p.shadows>2||p.fog_blur>2)return false;
 for(float f:{p.sun,p.sky,p.ambient,p.fog})if(!std::isfinite(f)||f<0||f>3)return false;
 return std::isfinite(p.bloom_threshold)&&p.bloom_threshold>=0&&p.bloom_threshold<=4&&std::isfinite(p.bloom_intensity)&&p.bloom_intensity>=0&&p.bloom_intensity<=2&&std::isfinite(p.light_exposure)&&std::abs(p.light_exposure)<=2;
}
// Caller holds mutex. Another active owner's enabled policy cannot be replaced.
static bool claim(int who){if(owner>=0&&owner!=who&&anyapi_owner_active(owner)&&parameters.enabled)return false;owner=who;return true;}
static bool set(const AnySceneParametersV1* p){int who=platform::current_plugin;if(who<0||!p||!valid(*p))return false;std::lock_guard lock(mutex);
 if(!claim(who))return false;
 parameters=*p;details={};look={};extras={};return true;
}
struct Policy{AnySceneParametersV1 scene;std::array<uint32_t,3> details{};Look look{};Extras extras{};};
static Policy snapshot(){std::lock_guard lock(mutex);return owner>=0&&anyapi_owner_active(owner)&&ready?Policy{parameters,details,look,extras}:Policy{};}
static AnySceneParametersV1 copy(){return snapshot().scene;}
static bool set_v2(const AnySceneParametersV2* p){int who=platform::current_plugin;if(who<0||!p||p->struct_size!=sizeof(*p)||p->version!=2||!valid(p->scene)||p->clouds>2||p->grass>2||p->foliage>2)return false;std::lock_guard lock(mutex);if(!claim(who))return false;parameters=p->scene;details={p->clouds,p->grass,p->foliage};look={};extras={};return true;}
static bool tints_valid(const float (&t)[3]){for(float v:t)if(!std::isfinite(v)||v<.5f||v>1.5f)return false;return true;}
static bool valid_v3(const AnySceneParametersV3& p){auto& d=p.detailed;
 if(p.struct_size!=sizeof(p)||p.version!=3||d.struct_size!=sizeof(d)||d.version!=2||!valid(d.scene)||d.clouds>2||d.grass>2||d.foliage>2||!std::isfinite(p.wind_scale)||p.wind_scale<0||p.wind_scale>4)return false;
 return tints_valid(p.key_tint)&&tints_valid(p.shade_tint);}
// Caller holds mutex.
static void store_v3(const AnySceneParametersV3& p){auto& d=p.detailed;parameters=d.scene;details={d.clouds,d.grass,d.foliage};
 for(int i=0;i<3;++i){look.key[i]=p.key_tint[i];look.shade[i]=p.shade_tint[i];}look.wind=p.wind_scale;}
static bool set_v3(const AnySceneParametersV3* p){int who=platform::current_plugin;if(who<0||!p||!valid_v3(*p))return false;
 std::lock_guard lock(mutex);if(!claim(who))return false;store_v3(*p);extras={};return true;}
// V4: moon_mode 2 (moving moon) and the game-lever fields (shadow_split, shadow_steps, grass_far) are rejected in 0.40.2.
static bool valid_v4(const AnySceneParametersV4& p){
 if(p.struct_size!=sizeof(p)||p.version!=4||!valid_v3(p.looked)||p.tint_targets>1||p.time_shaped>1||p.moon_mode>1||p.weather_fog_keep>1)return false;
 for(auto t:{&p.noon_key,&p.noon_shade,&p.golden_key,&p.golden_shade,&p.blue_key,&p.blue_shade,&p.moon_key,&p.moon_shade})if(!tints_valid(*t))return false;
 if(!std::isfinite(p.moonlight)||p.moonlight<0||p.moonlight>.03f||!std::isfinite(p.moon_phase_floor)||p.moon_phase_floor<0||p.moon_phase_floor>1)return false;
 if(p.shadow_split!=0||p.shadow_steps!=0||p.grass_far!=0)return false;
 for(auto r:p.reserved)if(r)return false;return true;}
static bool set_v4(const AnySceneParametersV4* p){int who=platform::current_plugin;if(who<0||!p||!valid_v4(*p))return false;
 Extras x;x.tint_targets=p->tint_targets;x.time_shaped=p->time_shaped;x.moon_mode=p->moon_mode;x.moonlight=p->moonlight;x.moon_phase_floor=p->moon_phase_floor;x.weather_fog_keep=p->weather_fog_keep;
 const float (*from[PALETTES])[3]={&p->noon_key,&p->noon_shade,&p->golden_key,&p->golden_shade,&p->blue_key,&p->blue_shade,&p->moon_key,&p->moon_shade};
 for(uint32_t i=0;i<PALETTES;++i)for(int c=0;c<3;++c)x.palette[i][c]=(*from[i])[c];
 std::lock_guard lock(mutex);if(!claim(who))return false;store_v3(p->looked);extras=x;return true;}
static bool status(AnySceneStatusV1* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=1)return false;s->ready=ready;s->renderer_calls=renderer_calls;s->scene_calls=scene_calls;s->rejected_frames=rejected_frames;return true;}
// Board records older than this many renderer frames read as absent (loading screens skip scene_hook).
static constexpr uint64_t STATUS_FRAMES=30;
template<class T> static bool recent(const scene_frame::Slot<T>& slot,T& out){if(!slot.load(out)||!out.frame)return false;uint64_t now=scene_frame::board.frame.load(std::memory_order_relaxed);return now>=out.frame&&now-out.frame<=STATUS_FRAMES;}
static bool status_v2(AnySceneStatusV2* s){if(!s||s->struct_size!=sizeof(*s)||s->version!=2)return false;
 *s=AnySceneStatusV2{};status(&s->base);using namespace scene_frame;
 Clock c{};if(recent(board.clock,c)&&c.source){s->clock_source=c.source;s->parked=c.parked;s->t=c.source==1?float(c.t):-1.f;s->sun_y=float(c.sun_y);s->moon_y=float(c.moon[1]);}
 Weather w{};if(recent(board.weather,w)){s->weather_source=w.source;s->fog_ratio=w.fog_ratio;if(w.source){s->rain=w.rain;s->snow=w.snow;s->cover=w.cover;s->wet=w.wet;}}
 MoonState m{};if(recent(board.moon,m)&&m.active){s->moon_mode_active=1;s->moon_light=m.added_luminance;}
 Flash f{};if(recent(board.flash,f))s->flash_frames=f.frames_total;
 s->tint_targets_active=tint_targets_active.load(std::memory_order_relaxed);return true;}
static const AnySceneControlsV4 api_v4{sizeof(AnySceneControlsV4),4,set_v4,status,status_v2};
static const AnySceneControlsV3 api_v3{sizeof(AnySceneControlsV3),3,set_v3,status};
static const AnySceneControlsV2 api_v2{sizeof(AnySceneControlsV2),2,set_v2,status};
static const AnySceneControlsV1 api{sizeof(AnySceneControlsV1),1,set,status};
ANYAPI_REGISTER_SERVICE(scene_controls_v4,"anyapi.scene_controls",4,&api_v4);
}
