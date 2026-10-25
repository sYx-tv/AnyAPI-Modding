#pragma once
// Lamp capture for the volumetric pass (AnyAPI 0.40.2). Reads the game's GPU light rings, one element per read,
// through the caller's Read functor: scene_frame::reader in the runtime (an in-process memcpy, no syscall), fake
// memory in tests. Header-only and free of Windows types.
//   scene_renderer.m_gpu_omni_lights +0xc48: ring of gpu_omni_light, 56 B (pos xyz, radius, rgb, volume, box min/max)
//   scene_renderer.m_gpu_spot_lights +0xc70: ring of gpu_spot_light, 120 B (m_mat_vp, pos, radius, dir, tan, rgb,
//   volume, cos, s32 m_spotlight_shadow_index @0x74 = raw[29], -1 = none)          (render_scene.gc)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace lighting {
constexpr uint32_t OMNI_STRIDE=56,SPOT_STRIDE=120,MAX_RING=4096;
constexpr float FLASH_RADIUS=4096;   // weather lightning pushes an 8192 m omni (rendering atlas): never a lamp
constexpr float MAX_COLOUR=1e4f;     // per channel; real lamps are far below, garbage up to 1e8 used to pass
struct Ring{uintptr_t buffer{};uint32_t start{},size{},capacity{},stride{};};
struct Flash{bool seen{};float colour[3]{},direction[3]{},intensity{};};
struct LampStats{uint32_t omni{},spot{},flashes{},rejected{},raw29_hits{},raw29_misses{},shadowed{},tracked{};};
// data: the 16-float record of volumetric::Frame::lights (position/radius, direction/cos, rgb/spot flag,
// shadow slot/depth mode/intensity/reserved). order: ring order (omni first), the tie-break of the old stable_sort.
struct Lamp{std::array<float,16> data{};std::array<float,16> matrix{};float score{};uint32_t order{};int32_t game_shadow{-1};bool spot{};uint16_t track{0xffff};};
inline bool before(const Lamp& a,const Lamp& b){return a.score>b.score||(a.score==b.score&&a.order<b.order);}
// The game's own surface falloff (pipeline_cluster_draw): (1-x^2)^2/(1+64x), x=saturate(d/r).
inline float game_falloff(float d,float r){float x=std::clamp(d/std::max(r,1e-3f),0.f,1.f),a=1-x*x;return a*a/(1+64*x);}
inline uintptr_t element(const Ring& r,uint32_t i){return r.buffer+uintptr_t((r.start+i)%r.capacity)*r.stride;}
// Reads the game's light rings through the caller's Read functor (held by reference, so a counting reader in a test
// sees every call). Per-element reads: scene_frame::read is an in-process copy, so chunking would save little.
template<class Read> class LampReader{
public:
 explicit LampReader(Read& read):read_(read){}
 // One read of the 24-byte ring header {buffer, start, size, capacity, stride}; same limits as 0.40.1.
 bool ring(uintptr_t address,uint32_t expected,Ring& r){
  unsigned char h[24]{};if(!read_(address,h,sizeof(h)))return false;
  memcpy(&r.buffer,h,8);memcpy(&r.start,h+8,4);memcpy(&r.size,h+12,4);memcpy(&r.capacity,h+16,4);memcpy(&r.stride,h+20,4);
  return r.size&&r.buffer&&r.size<=MAX_RING&&r.capacity>=r.size&&r.capacity<=1048576&&r.start<r.capacity&&r.stride==expected;}
 // Candidates for the lamp budget. Acceptance is exactly 0.40.1's (native_frame.h:40-44 there), plus a colour clamp
 // to MAX_COLOUR. Lights beyond radius+max_distance are culled. camera: graphics-relative position.
 void gather(uintptr_t scene,const float camera[3],float max_distance,std::vector<Lamp>& out,LampStats& s){
  out.clear();uint32_t order=0;
  auto one=[&](uintptr_t address,uint32_t expected,bool spot){Ring r;if(!ring(address,expected,r))return;
   for(uint32_t i=0;i<r.size;++i){float raw[30]{};if(!read_(element(r,i),raw,expected)){++s.rejected;continue;}
    bool finite=true;for(uint32_t j=0;j<(spot?29u:14u);++j)finite&=std::isfinite(raw[j])&&std::abs(raw[j])<=1e8f;if(!finite){++s.rejected;continue;}
    auto position=raw+(spot?16:0),colour=raw+(spot?24:4);float radius=position[3];
    if(radius<=.01f||radius>2000||colour[0]<0||colour[1]<0||colour[2]<0){if(spot||radius<FLASH_RADIUS)++s.rejected;else ++s.flashes;continue;}
    Lamp c;c.spot=spot;c.order=order++;for(unsigned j=0;j<4;++j)c.data[j]=position[j];for(unsigned j=0;j<3;++j)c.data[8+j]=std::min(colour[j],MAX_COLOUR);c.data[11]=spot?1.f:0.f;c.data[12]=-1;c.data[14]=1;
    float d2=0;for(unsigned j=0;j<3;++j){float d=position[j]-camera[j];d2+=d*d;}if(d2>(radius+max_distance)*(radius+max_distance))continue;
    c.score=std::max({c.data[8],c.data[9],c.data[10]})*radius*radius/(1+d2);
    if(spot){float norm=raw[20]*raw[20]+raw[21]*raw[21]+raw[22]*raw[22];if(norm<.99f||norm>1.01f||raw[28]<0||raw[28]>=1){++s.rejected;continue;}
     for(unsigned j=0;j<3;++j)c.data[4+j]=raw[20+j];c.data[7]=raw[28];for(unsigned j=0;j<16;++j)c.matrix[j]=raw[j];memcpy(&c.game_shadow,raw+29,4);}
    ++(spot?s.spot:s.omni);out.push_back(c);
   }};
  one(scene+0xc48,OMNI_STRIDE,false);one(scene+0xc70,SPOT_STRIDE,true);
 }
 // Lightning: the strongest omni with radius >= FLASH_RADIUS this frame. Reads 4 bytes (the radius) per omni and the
 // whole record only for a flash. intensity: the game falloff at the camera. Runs every lighting_hook (board.flash).
 Flash scan_flash(uintptr_t scene,const float camera[3]){
  Flash best;Ring r;if(!ring(scene+0xc48,OMNI_STRIDE,r))return best;float strongest=0;
  for(uint32_t i=0;i<r.size;++i){float radius{};if(!read_(element(r,i)+12,&radius,4)||!(radius>=FLASH_RADIUS)||radius>1e6f)continue;
   float raw[14]{};if(!read_(element(r,i),raw,sizeof(raw)))continue;bool finite=true;for(float v:raw)finite&=std::isfinite(v)&&std::abs(v)<=1e8f;if(!finite)continue;
   float delta[3]{raw[0]-camera[0],raw[1]-camera[1],raw[2]-camera[2]},d=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
   float colour[3];for(int j=0;j<3;++j)colour[j]=std::clamp(raw[4+j],0.f,MAX_COLOUR);float k=game_falloff(d,raw[3]),level=std::max({colour[0],colour[1],colour[2]})*k;
   if(level<=strongest&&best.seen)continue;strongest=level;best.seen=true;best.intensity=k;for(int j=0;j<3;++j){best.colour[j]=colour[j];best.direction[j]=d>1e-3f?delta[j]/d:(j==1?1.f:0.f);}
  }
  return best;
 }
private:
 Read& read_;
};
// Legacy selection: the first min(budget,8) by (score desc, ring order asc), the same set and order the old
// stable_sort gave. partial_sort keeps the cost at O(n log budget).
inline void select_legacy(std::vector<Lamp>& lamps,uint32_t budget){uint32_t n=std::min<uint32_t>({uint32_t(lamps.size()),budget,8u});
 std::partial_sort(lamps.begin(),lamps.begin()+n,lamps.end(),before);lamps.resize(n);}
// K26 selection for V4 callers (ANY_LAMP_TRACKING). Lights keep an identity across frames, so a lamp at the budget
// edge does not flip every frame, a strobe keeps its slot through its dark phase and a one-frame muzzle flash does
// not evict a headlight. CPU only; the shader input is the same 16-float record with the colour scaled by fade.
struct LampTrack{bool used{},spot{},selected{},present{},grace{};float world[3]{},dir[3]{},radius{},ema[3]{},score{},fade{1};double last_seen{};uint32_t age{};};
class LampTracker{
public:
 static constexpr uint32_t TRACKS=32;
 static constexpr double DORMANT=.75,EMA_TAU=.3,FADE=.2,SNAP_GAP=.25,RESET_GAP=1;
 static constexpr float KEEP=1.5f,YOUNG=.25f,MARGIN=1.2f,SPEED=60;
 void reset(){tracks={};last=-1;}
 uint32_t active()const{uint32_t n=0;for(auto& t:tracks)n+=t.used;return n;}
 // lamps: this frame's candidates (from gather). origin: graphics origin (world = graphics + origin). Replaces lamps
 // with the drawn set: selected lights by rank, then fading-out lights while there is room under cap.
 void select(std::vector<Lamp>& lamps,uint32_t budget,uint32_t cap,const double origin[3],double now){
  double gap=last<0?1e9:now-last;if(gap>RESET_GAP||gap<0)reset();bool snap=gap>SNAP_GAP;float dt=float(std::clamp(gap,0.,.1));last=now;
  budget=std::min(budget,cap);for(auto& t:tracks)t.present=false;
  std::sort(lamps.begin(),lamps.end(),before);   // match the strongest first
  float a=snap?1.f:float(1-std::exp(-dt/EMA_TAU));
  for(auto& l:lamps){float w[3];for(int j=0;j<3;++j)w[j]=float(double(l.data[j])+origin[j]);float c=std::max({l.data[8],l.data[9],l.data[10]});
   int best=-1;float best_d=1e30f;
   for(uint32_t k=0;k<TRACKS;++k){auto& t=tracks[k];if(!t.used||t.present||t.spot!=l.spot)continue;float d2=0;for(int j=0;j<3;++j){float d=w[j]-t.world[j];d2+=d*d;}
    float reach=std::max(1.5f,.1f*l.data[3])+SPEED*dt;if(d2>reach*reach||chroma(t.ema,l.data.data()+8)<.9f)continue;
    if(l.spot&&t.dir[0]*l.data[4]+t.dir[1]*l.data[5]+t.dir[2]*l.data[6]<.966f)continue;if(d2<best_d){best_d=d2;best=int(k);}}
   if(best<0){best=allocate();if(best<0)continue;auto& t=tracks[best];t={};t.used=true;t.spot=l.spot;t.fade=1;for(int j=0;j<3;++j)t.ema[j]=l.data[8+j];}
   else{auto& t=tracks[best];for(int j=0;j<3;++j)t.ema[j]+=a*(l.data[8+j]-t.ema[j]);++t.age;}
   auto& t=tracks[best];t.present=true;t.last_seen=now;t.radius=l.data[3];for(int j=0;j<3;++j){t.world[j]=w[j];t.dir[j]=l.data[4+j];}l.track=uint16_t(best);
   float ema=std::max({t.ema[0],t.ema[1],t.ema[2]});t.score=c>0?l.score*ema/c:0;   // score from the smoothed colour
  }
  for(auto& t:tracks)if(t.used&&!t.present&&now-t.last_seen>DORMANT)t={};
  uint32_t incumbents=0;for(auto& l:lamps)if(l.track!=0xffff&&tracks[l.track].selected)++incumbents;
  for(auto& l:lamps){if(l.track==0xffff){l.score=0;continue;}auto& t=tracks[l.track];float s=t.score;
   if(t.selected)s*=KEEP;else if(incumbents>=budget)s/=MARGIN;if(!l.spot&&t.age<3)s*=YOUNG;l.score=s;}
  uint32_t n=std::min<uint32_t>(budget,uint32_t(lamps.size()));std::partial_sort(lamps.begin(),lamps.begin()+n,lamps.end(),before);
  // Fades: re-entry (known >= 3 frames, not selected last frame) rises 0->1 over FADE; a new light starts at 1;
  // a light that leaves the selection while the game still lists it fades out in a spare slot (cap - drawn);
  // a light the game dropped is cut, like on game surfaces. A pause or hitch snaps every fade.
  auto& drawn=scratch;drawn.assign(lamps.begin(),lamps.begin()+n);   // members keep their capacity: no steady-state allocation
  for(auto& l:drawn){auto& t=tracks[l.track];if(!t.selected&&!t.grace)t.fade=t.age>=3?0.f:1.f;t.fade=snap?1.f:std::min(1.f,t.fade+dt/float(FADE));t.selected=true;t.grace=false;scale(l,t.fade);}
  for(uint32_t i=n;i<lamps.size();++i){auto& l=lamps[i];if(l.track==0xffff)continue;auto& t=tracks[l.track];if(!t.selected&&!t.grace)continue;
   t.selected=false;t.fade=snap?0.f:std::max(0.f,t.fade-dt/float(FADE));t.grace=t.fade>0&&drawn.size()<cap;if(t.grace){drawn.push_back(l);scale(drawn.back(),t.fade);}}
  for(uint32_t k=0;k<TRACKS;++k)if(!tracks[k].present){tracks[k].selected=tracks[k].grace=false;}
  lamps.swap(drawn);
 }
 const LampTrack& track(uint32_t i)const{return tracks[i];}
private:
 std::array<LampTrack,TRACKS> tracks{};double last{-1};std::vector<Lamp> scratch;
 static float chroma(const float a[3],const float* b){float na=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]),nb=std::sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);return na<1e-6f||nb<1e-6f?1.f:(a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(na*nb);}
 static void scale(Lamp& l,float f){for(int j=8;j<11;++j)l.data[j]*=f;}
 int allocate(){int dormant=-1;double oldest=1e300;for(uint32_t k=0;k<TRACKS;++k){if(!tracks[k].used)return int(k);if(!tracks[k].present&&tracks[k].last_seen<oldest){oldest=tracks[k].last_seen;dormant=int(k);}}return dormant;}
};
}  // namespace lighting
