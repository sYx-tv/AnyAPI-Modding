#include "../world_time_state.h"
#include <cassert>
#include <limits>
int main(){world_time::Store store;AnyWorldTimeSnapshotV1 s;
assert(!store.copy(&s,1000));store.sample(.5,1000);assert(store.copy(&s,1100)&&s.seconds_since_midnight==43200);
assert(!store.copy(&s,1501)&&s.sampled_tick==0);assert(!store.copy(&s,999));
store.sample(1.25,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==21600);
store.sample(-.25,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==64800);
store.sample(0,2000);assert(store.copy(&s,2000)&&s.seconds_since_midnight==0);
store.sample(std::numeric_limits<double>::quiet_NaN(),2000);assert(!store.copy(&s,2000));
s.version=7;assert(!store.copy(&s,2000));assert(!store.copy(nullptr,2000));
// 0.40.2: the getter tick and the camera weather (client_scene weather_particles) next to the cycle, for scene controls.
{world_time::Store w;world_time::Observed o;assert(!w.observed(o,1000)&&o.cycle==-1&&o.tick==-1&&!o.weather_valid);
 w.sample(.25,1000,12345);assert(w.observed(o,1200)&&o.cycle==.25&&o.tick==12345&&o.sampled==1000&&!o.weather_valid);assert(!w.observed(o,1501));
 double raw[4]{.5,.25,3,-4};w.sample_weather(0x1000,raw,true,1000);assert(w.observed(o,1100)&&o.weather_valid&&o.rain==.5&&o.snow==.25&&o.wind[0]==3&&o.wind[1]==-4&&o.epoch==1&&o.weather_sampled==1000);
 w.sample_weather(0x1000,raw,true,1100);assert(w.observed(o,1100)&&o.epoch==1);w.sample_weather(0x2000,raw,true,1100);assert(w.observed(o,1100)&&o.epoch==2&&o.weather_valid);
 double high[4]{1.5,0,0,0},nan[4]{std::numeric_limits<double>::quiet_NaN(),0,0,0},edge[4]{1+5e-7,-5e-7,0,0};
 w.sample_weather(0x2000,high,true,1100);assert(w.observed(o,1100)&&!o.weather_valid);w.sample_weather(0x2000,nan,true,1100);assert(w.observed(o,1100)&&!o.weather_valid);
 w.sample_weather(0x2000,raw,false,1100);assert(w.observed(o,1100)&&!o.weather_valid);w.sample_weather(0x2000,edge,true,1100);assert(w.observed(o,1100)&&o.rain==1&&o.snow==0);
 assert(!w.observed(o,1700)&&!o.weather_valid&&o.epoch==2);w.sample(std::numeric_limits<double>::quiet_NaN(),1200,7);assert(!w.observed(o,1200)&&o.tick==-1);
 world_time::shared.sample(.75,5000,1);AnyWorldTimeSnapshotV1 shared_copy;assert(world_time::shared.copy(&shared_copy,5000)&&shared_copy.seconds_since_midnight==64800);}}
