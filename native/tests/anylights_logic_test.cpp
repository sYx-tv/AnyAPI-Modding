#include "../anylights_logic.h"
#include <cassert>
using namespace anylights;
int main(){
 // Tags round-trip and sit as the last word of the player's name.
 Config c;c.has_colour=true;c.colour={255,34,0};c.pattern=kStrobe;c.speed=3;
 assert(tag(c)=="ALff220023"&&tag(c).size()==kTagLength);
 assert(join_name("Left beacon",c)=="Left beacon ALff220023");
 std::string base;Config back;assert(split_name("Left beacon ALff220023",base,back)&&base=="Left beacon"&&back==c);
 assert(split_name("ALff220023",base,back)&&base.empty()&&back==c);
 assert(split_name("Left beacon  ALFF220023 ",base,back)&&base=="Left beacon"&&back==c);
 // Plain names keep their text and read as stock; near misses are not tags.
 assert(!split_name("Headlight",base,back)&&base=="Headlight"&&!back.modded());
 assert(!split_name("Headlight ALff22002",base,back)&&!split_name("Headlight ALgg220023",base,back)&&!split_name("   ",base,back)&&base.empty());
 // Black means stock colour; back to stock drops the tag; bad digits fall back safely.
 Config stock_blink;stock_blink.pattern=kBlink;assert(tag(stock_blink)=="AL00000012");
 assert(split_name("AL00000012",base,back)&&!back.has_colour&&back.pattern==kBlink);
 assert(join_name("Lamp",Config{})=="Lamp");
 assert(split_name("AL123456ff",base,back)&&back.pattern==kSteady&&back.speed==kDefaultSpeed);
 // Sync words are always negative (vanilla abilities are not) and round-trip every pattern, speed and colour.
 assert(is_word(pack(Config{}))&&!unpack(pack(Config{})).modded()&&!unpack(int32_t(kMarker)).modded()&&!is_word(0)&&!is_word(5));
 for(uint8_t p=0;p<kPatternCount;++p)for(uint8_t s=0;s<kSpeedCount;++s){Config k=c;k.pattern=p;k.speed=s;assert(is_word(pack(k))&&unpack(pack(k))==k);k.has_colour=false;assert(unpack(pack(k))==k);}
 // Data inputs: NaN is unconnected; 0-1 and 0-255 both work; pattern and speed are clamped or snapped.
 Inputs none;assert(effective(c,none)==c);
 Inputs rgb;rgb.r=1;rgb.g=0.5;rgb.b=0;Config e=effective(Config{},rgb);assert(e.has_colour&&e.colour.r==255&&e.colour.g==128&&e.colour.b==0);
 Inputs bytes;bytes.r=200;bytes.g=300;bytes.b=-5;e=effective(Config{},bytes);assert(e.colour.r==200&&e.colour.g==255&&e.colour.b==0);
 Inputs dark;dark.r=0;dark.g=0;dark.b=0;assert(!effective(c,dark).has_colour);
 Inputs pat;pat.pattern=4.2;pat.speed=5.1;e=effective(Config{},pat);assert(e.pattern==kPulse&&kSpeedHz[e.speed]==6);
 pat.pattern=99;pat.speed=0.3;e=effective(c,pat);assert(e.pattern==kSteady&&kSpeedHz[e.speed]==0.25);
 // Patterns: steady is always on, blink is half on, alternates are opposite, strobe is short, pulse stays in range.
 int on[kPatternCount]={};const int N=1000;
 for(int i=0;i<N;++i){double t=i/double(N);for(uint8_t p=0;p<kPatternCount;++p){double l=level(p,2,t);assert(l>=0&&l<=1);on[p]+=lit(p,2,t);}
  assert(lit(kAlternateA,2,t)!=lit(kAlternateB,2,t));}
 assert(on[kSteady]==N&&on[kColourCycle]==N&&on[kBlink]==N/2&&on[kStrobe]>0&&on[kStrobe]<N/5&&on[kDoubleFlash]>0&&on[kDoubleFlash]<N/4);
 assert(level(kPulse,2,0)<0.2&&level(kPulse,2,0.5)>0.99);
 // Speed scales the period: blink at 2 Hz has flipped by a quarter second.
 assert(lit(kBlink,4,0.1)&&!lit(kBlink,4,0.3));
 // Colour: chosen colour, stock (none), or a full-saturation sweep for Colour cycle.
 Colour out;assert(colour_at(c,1,out)&&out==c.colour&&!colour_at(Config{},1,out));
 Config cyc;cyc.pattern=kColourCycle;assert(colour_at(cyc,0,out)&&out==(Colour{255,0,0}));
 Colour g=hsv(1/3.0,1,1);assert(g.g==255&&g.r==0&&g.b==0);
 // Misread names never go back to the game: control bytes and broken UTF-8 are dropped, real text is kept.
 assert(clean_text("Left beacon")=="Left beacon"&&clean_text(" caf\xc3\xa9 ")=="caf\xc3\xa9");
 assert(clean_text("\xd9\x8f,")==("\xd9\x8f,")&&clean_text("\x01\xff\x80P-")=="P-"&&clean_text("\xc0\x80")==""&&clean_text("\x7f")=="");
 return 0;
}
