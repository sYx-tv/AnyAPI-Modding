#include "../enginesound_params.h"
#include <cassert>
#include <cmath>
using namespace enginesound;
int main(){
 // Packed values round-trip, carry the marker and never collide with indices.
 Params p=neutral();int32_t n=pack(p);assert(is_packed(n)&&n>0&&n>kCustomIndex);
 for(int s=0;s<kSliderCount;++s)assert(unpack(n).v[s]==kSliders[s].neutral);
 for(int s=0;s<kSliderCount;++s){Params q=neutral();q.v[s]=uint8_t(max_step(s));auto r=unpack(pack(q));for(int t=0;t<kSliderCount;++t)assert(r.v[t]==q.v[t]);}
 Params over=neutral();over.v[kPitch]=200;assert(unpack(pack(over)).v[kPitch]==max_step(kPitch));
 // Slider fields don't overlap and fit below the marker bit.
 int32_t used=0;for(auto& s:kSliders){int32_t m=((1<<s.bits)-1)<<s.shift;assert((used&m)==0);used|=m;}assert(used==kPackedMask);
 // Decoding: vanilla stays vanilla, presets and Custom map to their own params, junk falls back.
 for(int32_t i=kVanillaMin;i<=kVanillaMax;++i)assert(decode(i).mode==Mode::Vanilla);
 for(int32_t i=0;i<kPresetCount;++i){auto c=decode(kFirstPreset+i);assert(c.mode==Mode::Preset&&c.preset==i);}
 assert(decode(kCustomIndex).mode==Mode::Custom);assert(decode(kCustomIndex+1).mode==Mode::Vanilla);assert(decode(-1).mode==Mode::Vanilla);
 assert(decode(0x7fffffff).mode==Mode::Vanilla);Params c=neutral();c.v[kVolume]=3;assert(decode(pack(c)).mode==Mode::Custom&&decode(pack(c)).params.v[kVolume]==3);
 // Neutral custom sounds like stock: unit pitch multiplier and gains at every rpm.
 for(double r=0;r<=1;r+=.1)assert(std::abs(pitch_multiplier(neutral(),r)-1)<1e-12);
 assert(std::abs(volume(neutral())-1)<1e-12&&std::abs(idle_layer(neutral())-1)<1e-12&&smoothing_seconds(neutral())==0&&pops(neutral()));
 assert(base_effect(neutral())==kEngineHighA);
 // Rev range widens pitch at high rpm only; curve shapes the middle.
 Params wide=neutral();wide.v[kRange]=15;assert(std::abs(pitch_multiplier(wide,0)-1)<1e-12&&std::abs(pitch_multiplier(wide,1)-2)<1e-12);
 Params steep=wide;steep.v[kCurve]=6;assert(pitch_multiplier(steep,.5)<pitch_multiplier(wide,.5));
 assert(std::isfinite(pitch_multiplier(wide,NAN))&&pitch_multiplier(wide,5)==pitch_multiplier(wide,1));
 // Smoothing converges, passes through when off and ignores bad dt.
 assert(smooth(0,1,0,.016)==1);double v=0;for(int i=0;i<600;++i)v=smooth(v,1,.2,.016);assert(std::abs(v-1)<1e-6);
 assert(smooth(.3,1,.2,0)==1&&smooth(NAN,1,.2,.016)==1);
 assert(clamp_speed(NAN)==1&&clamp_speed(100)==4&&clamp_volume(-1)==0&&clamp_volume(NAN)==0);
 return 0;}
