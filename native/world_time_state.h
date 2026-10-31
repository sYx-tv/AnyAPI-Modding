#pragma once
#include "anyapi_world_time_v1.h"
#include <cmath>
#include <cstdint>
#include <mutex>
namespace world_time {
// client_scene offsets (0.1.27): m_time_of_day +0x2168; m_weather_particles +0x2b68, whose m_factor_rain (+0x50),
// m_factor_snow (+0x58) and m_wind_vector (+0x60) the game fills each tick from weather_data at the camera.
constexpr uintptr_t TIME_OF_DAY=0x2168,WEATHER_RAIN=0x2bb8,WEATHER_BYTES=32;
constexpr uint64_t FRESH_MS=500;
// What scene controls reads next to the public snapshot: the getter's tick and the camera weather.
struct Observed {double cycle{-1};int64_t tick{-1};uint64_t sampled{};uint32_t weather_valid{},epoch{};double rain{},snow{},wind[2]{};uint64_t weather_sampled{};};
// Rain and snow must be finite and within 0..1 (1e-6 tolerance, then clamped).
inline bool weather_in_range(const double raw[2]){for(int i=0;i<2;++i)if(!std::isfinite(raw[i])||raw[i]<-1e-6||raw[i]>1+1e-6)return false;return true;}
class Store {
 mutable std::mutex mutex_;AnyWorldTimeSnapshotV1 value_;int64_t tick_{-1};
 uintptr_t client_{};uint32_t epoch_{},weather_valid_{};double weather_[4]{};uint64_t weather_tick_{};
public:
 void sample(double cycle,uint64_t now,int64_t tick=-1){AnyWorldTimeSnapshotV1 next;bool ok=std::isfinite(cycle)&&std::abs(cycle)<1e6;
  if(ok){next.cycle_factor=cycle-std::floor(cycle);next.seconds_since_midnight=uint32_t(next.cycle_factor*86400);next.sampled_tick=now;}
  std::lock_guard lock(mutex_);value_=next;tick_=ok?tick:-1;
 }
 // raw: rain, snow, wind x, wind z as read at client+WEATHER_RAIN. A new client_scene pointer starts a new epoch
 // (scene controls then restarts its ramps); a failed read or an out-of-range value clears the weather.
 void sample_weather(uintptr_t client,const double raw[4],bool read_ok,uint64_t now){std::lock_guard lock(mutex_);
  if(client!=client_){client_=client;++epoch_;weather_valid_=0;}
  weather_tick_=now;weather_valid_=read_ok&&weather_in_range(raw);if(!weather_valid_)return;
  for(int i=0;i<2;++i)weather_[i]=std::fmin(1.,std::fmax(0.,raw[i]));weather_[2]=std::isfinite(raw[2])?raw[2]:0;weather_[3]=std::isfinite(raw[3])?raw[3]:0;
 }
 bool copy(AnyWorldTimeSnapshotV1* out,uint64_t now){if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;std::lock_guard lock(mutex_);*out={};if(!value_.sampled_tick||now<value_.sampled_tick||now-value_.sampled_tick>FRESH_MS)return false;*out=value_;return true;}
 // False when the cycle sample is missing or older than FRESH_MS; weather_valid is set only for a fresh, valid weather sample.
 bool observed(Observed& out,uint64_t now) const {std::lock_guard lock(mutex_);out={};out.epoch=epoch_;
  if(weather_valid_&&now>=weather_tick_&&now-weather_tick_<=FRESH_MS){out.weather_valid=1;out.rain=weather_[0];out.snow=weather_[1];out.wind[0]=weather_[2];out.wind[1]=weather_[3];out.weather_sampled=weather_tick_;}
  if(!value_.sampled_tick||now<value_.sampled_tick||now-value_.sampled_tick>FRESH_MS)return false;
  out.cycle=value_.cycle_factor;out.tick=tick_;out.sampled=value_.sampled_tick;return true;
 }
};
// The process-wide store: written by the getter observer (anyapi_world_time.inc), read by scene controls.
inline Store shared;
}
