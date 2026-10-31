#include "runtime_call_cell.h"
namespace scene_controls {
static anyapi_runtime::CallCellHook renderer_cell,scene_cell,pre_hud_cell,lighting_cell,ssao_cell,composite_cell;
// The renderer and scene_renderer of the composite pass being recorded. The SSAO
// call inside it receives a pipeline object, not the renderer.
static thread_local void* composite_renderer{};static thread_local void* composite_scene{};
static bool writable(void* pointer,size_t bytes){MEMORY_BASIC_INFORMATION m{};return pointer&&VirtualQuery(pointer,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(m.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE))&&uintptr_t(pointer)+bytes>=uintptr_t(pointer)&&uintptr_t(pointer)+bytes<=uintptr_t(m.BaseAddress)+m.RegionSize;}
struct RendererSaved {unsigned char flags[9];double threshold,intensity;};
static bool renderer_apply(void* object,const AnySceneParametersV1* p,RendererSaved* old,const uint32_t* details=nullptr){
 if(!writable(object,0x688))return false;auto b=(unsigned char*)object;
 memcpy(old->flags,b+0x638,9);memcpy(&old->threshold,b+0x648,8);memcpy(&old->intensity,b+0x650,8);
 for(auto v:old->flags)if(v>1)return false;
 if(!std::isfinite(old->threshold)||!std::isfinite(old->intensity))return false;
 auto choice=[&](size_t off,uint32_t v){if(v)b[off]=v==2;};
 if(details)for(size_t i=0;i<3;++i)choice(0x63e+i,details[i]);
 choice(0x638,p->aa);choice(0x639,p->ssao);choice(0x63a,p->bloom);choice(0x63b,p->shadows);choice(0x63c,p->fog_blur);
 if(p->bloom==2){double threshold=p->bloom_threshold,intensity=p->bloom_intensity;memcpy(b+0x648,&threshold,8);memcpy(b+0x650,&intensity,8);}return true;
}
static void renderer_restore(void* object,const RendererSaved* old,const AnySceneParametersV1* p,const uint32_t* details=nullptr){auto b=(unsigned char*)object;const uint32_t choices[]={p->aa,p->ssao,p->bloom,p->shadows,p->fog_blur};for(size_t i=0;i<5;++i)if(choices[i])b[0x638+i]=old->flags[i];if(details)for(size_t i=0;i<3;++i)if(details[i])b[0x63e+i]=old->flags[6+i];if(p->bloom==2){memcpy(b+0x648,&old->threshold,8);memcpy(b+0x650,&old->intensity,8);}}
using RenderFn=void(*)(void*);
static void scoped_render(void* self,const AnySceneParametersV1* p,const uint32_t* details=nullptr){RendererSaved old{};bool changed=false;
 __try{changed=renderer_apply(self,p,&old,details);if(!changed)++rejected_frames;((RenderFn)renderer_cell.original())(self);}
 __finally{if(changed)renderer_restore(self,&old,p,details);}
}
// K04: the effects finish replaces the game's bloom only while it rendered within the last FINISH_GRACE frames.
// Effects stores board.finish_since_ok=0 on a rendered finish; renderer_hook counts every frame, saturating at
// UINT32_MAX (also the start value). An effects FIN-12 identity frame stores nothing, so the game's bloom returns.
// wanted: this frame's finish is on and not an identity. When it becomes wanted (start-up, Gameplay only leaving a
// menu, an identity finish getting work) the grace starts at once, so that first frame does not add the game's bloom
// to the finish's own; a finish that then fails gives the bloom back after FINISH_GRACE frames as before.
static constexpr uint32_t FINISH_GRACE=15;static bool finish_was_wanted{};
static uint32_t count_finish_frame(bool wanted=false){auto& c=scene_frame::board.finish_since_ok;
 if(wanted&&!finish_was_wanted)c.store(0,std::memory_order_relaxed);finish_was_wanted=wanted;uint32_t v=c.load(std::memory_order_relaxed);
 while(v<UINT32_MAX&&!c.compare_exchange_weak(v,v+1,std::memory_order_relaxed)){}return v<UINT32_MAX?v+1:v;}
static bool finish_replaces_bloom(uint32_t since_ok){return since_ok<FINISH_GRACE;}
static void renderer_hook(void* self){++renderer_calls;Policy policy;AnySceneParametersV1 p{};bool owned{};
 {scene_frame::CpuScope cpu;scene_frame::board.frame.fetch_add(1,std::memory_order_relaxed);
  policy=snapshot();p=policy.scene;owned=p.enabled!=0;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
  if(scene_antialiasing::begin()){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.aa=1;}
  // Scene effects replace the game's bloom; their own bloom runs before tone mapping.
  auto fx=scene_effects::copy();scene_effects::update_photo(fx);
  const uint32_t since_ok=count_finish_frame(fx.enabled&&fx.finish&&!scene_effects::finish_identity(fx,scene_effects::copy_extras()));
  // SC-16: the draw distance reaches this frame's culling, also when the lighting cell did not install.
  scene_effects::update_distance();
  if(fx.enabled&&fx.finish&&finish_replaces_bloom(since_ok)){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.bloom=1;}
  // The replacement ambient occlusion writes into the game's SSAO target, so the composite must bind it (m_is_ssao).
  if(fx.enabled&&fx.ambient_occlusion&&scene_effects::ao_available){if(!p.enabled){p=AnySceneParametersV1{};p.enabled=1;}p.ssao=2;}
#else
  count_finish_frame();
#endif
 }
 // SC-3: a substituted default policy (owner off or absent) applies no clouds/grass/foliage overrides.
 if(p.enabled)scoped_render(self,&p,owned?policy.details.data():nullptr);else ((RenderFn)renderer_cell.original())(self);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 {scene_frame::CpuScope cpu;scene_antialiasing::finish();}
#endif
}
// What scene_apply writes on top of the v1 scaling. plan_apply builds it; a V1-V3 policy gives today's output.
struct Apply{float key[3]{1,1,1},shade[3]{1,1,1};uint32_t targets{0};double moon_add{0},fog_scale{1};};
// Tint targets. 0 (V3): key on view up (0), light up (2) and main (4); shade on view down (1), light down (3), sky (5)
// and back (6). 1 (K17): key on main (4) and back (6, sun bounce); shade on light down (3) and sky (5); 0-2 untouched.
static int tint_side(uint32_t targets,size_t i){if(targets==1)return i==4||i==6?1:i==3||i==5?2:0;return i==0||i==2||i==4?1:2;}
static constexpr double MOON_RGB[3]{.55,.65,.9};
static double luma(const double c[3]){return .2126*c[0]+.7152*c[1]+.0722*c[2];}
// scene_renderer is transient frame data. Mutate its copied lighting before
// _build_scene creates constants; subsequent native passes consume the same data.
static bool scene_apply(void* object,bool object_writable,const AnySceneParametersV1& p,const Apply& a={}){if(!object_writable)return false;auto b=(unsigned char*)object;double colors[7][3]{},fog{};memcpy(colors,b+0xa90,sizeof(colors));memcpy(&fog,b+0xb60,8);
 for(auto& c:colors)for(auto value:c)if(!std::isfinite(value)||value<0||value>10000)return false;
 if(!std::isfinite(fog)||fog<0||fog>10000)return false;
 double exposure=std::exp2(p.light_exposure);for(size_t i=0;i<7;++i){double scale=i==4?p.sun:i==5?p.sky:p.ambient;for(auto& value:colors[i])value=std::min(10000.,value*scale*exposure);}fog=std::min(10000.,fog*a.fog_scale);
 // K49 Stage A: moonlight in the game's parked night main light, so the game's cascades, fog, SSAO and ocean see it.
 if(a.moon_add>0)for(int c=0;c<3;++c)colors[4][c]=std::min(10000.,colors[4][c]+a.moon_add*MOON_RGB[c]/luma(MOON_RGB));
 // Each light keeps its luminance, so only its colour changes.
 for(size_t i=0;i<7;++i){int side=tint_side(a.targets,i);if(!side)continue;const float* tint=side==1?a.key:a.shade;if(tint[0]==1&&tint[1]==1&&tint[2]==1)continue;
  double before=.2126*colors[i][0]+.7152*colors[i][1]+.0722*colors[i][2],after=.2126*colors[i][0]*tint[0]+.7152*colors[i][1]*tint[1]+.0722*colors[i][2]*tint[2];
  double k=after>1e-12?before/after:1;for(int c=0;c<3;++c)colors[i][c]=std::min(10000.,colors[i][c]*tint[c]*k);}
 memcpy(b+0xa90,colors,sizeof(colors));memcpy(b+0xb60,&fog,8);return true;
}
using SceneFn=void(*)(void*,void*);
// Wind for tree and grass sway: scene_renderer.m_wind_vector (vec2 of doubles), read by _build_scene for the
// vegetation and grass constants. Scaled only around the original call and put back afterwards.
static bool wind_scale(void* scene,bool scene_writable,float scale,double* saved){if(scale==1||!scene_writable)return false;auto b=(unsigned char*)scene+0xb78;memcpy(saved,b,16);
 if(!std::isfinite(saved[0])||!std::isfinite(saved[1])||std::abs(saved[0])>200||std::abs(saved[1])>200)return false;double scaled[2]{saved[0]*scale,saved[1]*scale};memcpy(b,scaled,16);return true;}
// ---- Frame publishing: board.sample, clock, weather (before scene_apply) and moon (after it), once per scene_hook.
// scene_renderer (render_scene, 0.1.27 game code): +0xa78 m_main_light_direction (points away from the sun; parked at the noon
// direction while t<0.2||t>0.8), +0xa90..+0xb37 the seven colours, +0xb38 m_planet_shadow_height, +0xb60 m_base_fog_factor,
// +0xb70 m_is_weather_particles, +0xb71 m_is_ocean, +0xb78 m_wind_vector, +0xb88 m_weather_texture_a_offset,
// +0xba8 m_time_of_day_factor (written by client_scene.build_scene_render). One in-process read of [0xa78,0xbb0).
static constexpr uintptr_t BLOCK_BEGIN=0xa78,BLOCK_END=0xbb0;
static constexpr double UNSET=std::numeric_limits<double>::quiet_NaN();
struct Native{scene_frame::Sample sample{};double planet_shadow{UNSET},weather_offset[2]{UNSET,UNSET};};
static bool finite_range(const double* v,size_t n,double lo,double hi){for(size_t i=0;i<n;++i)if(!std::isfinite(v[i])||v[i]<lo||v[i]>hi)return false;return true;}
static Native read_native(uintptr_t scene,uint64_t frame){using namespace scene_frame;Native n{};auto& s=n.sample;s.frame=frame;unsigned char raw[BLOCK_END-BLOCK_BEGIN];
 if(scene&&read(scene+BLOCK_BEGIN,raw,sizeof(raw))){auto get=[&](uintptr_t at,void* to,size_t bytes){memcpy(to,raw+(at-BLOCK_BEGIN),bytes);};
  get(0xba8,&s.cycle,8);get(0xb60,&s.native_fog,8);get(0xaf0,s.light_main,24);get(0xb08,s.light_sky,24);get(0xa78,s.sun_dir,24);get(0xb78,s.wind,16);
  get(0xb70,&s.weather_particles,1);get(0xb71,&s.ocean,1);get(0xb38,&n.planet_shadow,8);get(0xb88,n.weather_offset,16);
  if(std::isfinite(s.cycle)&&s.cycle>=0&&s.cycle<=1)s.valid|=SAMPLE_CYCLE;
  if(finite_range(&s.native_fog,1,0,10000))s.valid|=SAMPLE_FOG;
  if(finite_range(s.light_main,3,0,10000)&&finite_range(s.light_sky,3,0,10000))s.valid|=SAMPLE_LIGHTS;
  if(finite_range(s.sun_dir,3,-100,100)&&celestial::dot(s.sun_dir,s.sun_dir)>=.01)s.valid|=SAMPLE_SUN;
  if(finite_range(s.wind,2,-200,200))s.valid|=SAMPLE_WIND;
  if(s.weather_particles<=1&&s.ocean<=1)s.valid|=SAMPLE_FLAGS;}
 s.view=scene?view_flags(scene):VIEW_UNREADABLE;if(s.view!=VIEW_UNREADABLE)s.valid|=SAMPLE_VIEW;return n;}
// K16 clock: celestial::evaluate on 0xba8. Cross-check: more than 1e-3 from the world_time getter sample for 60 frames
// in a row makes it invalid until they agree again. Fallback: the parked detector (source 2), holding the last moon and stars.
static constexpr double CROSS_CHECK=1e-3;static constexpr uint32_t CROSS_FRAMES=60;
struct ClockState{celestial::Calibration cal;uint32_t mismatch{},day_frames{};bool held{};double moon[3]{},star_angle{};};
static double cycle_delta(double a,double b){double d=a-b;return d-std::floor(d+.5);}
static scene_frame::Clock make_clock(const scene_frame::Sample& s,const world_time::Observed* o,ClockState& st){using namespace scene_frame;Clock c{};c.frame=s.frame;c.t=-1;
 const int64_t tick=o?o->tick:-1;
 if(s.valid&SAMPLE_CYCLE){
  if(o)st.mismatch=std::abs(cycle_delta(o->cycle,s.cycle))>CROSS_CHECK?std::min<uint32_t>(st.mismatch+1,1u<<30):0;
  if(!st.cal.done&&!celestial::parked(s.cycle))++st.day_frames;
  auto r=celestial::evaluate(s.cycle,(s.valid&SAMPLE_SUN)?s.sun_dir:nullptr,tick,st.cal);
  if(r.valid&&st.mismatch<CROSS_FRAMES){c.source=1;c.parked=r.parked;c.t=s.cycle;for(int i=0;i<3;++i){c.sun[i]=r.sun[i];c.moon[i]=st.moon[i]=r.moon[i];}
   c.sun_y=r.sun_y;c.night=r.night;c.star_angle=st.star_angle=r.star_angle;c.day_index=r.day_index;c.phase=r.phase;c.lit_fraction=r.lit_fraction;st.held=true;return c;}}
 if((s.valid&(SAMPLE_SUN|SAMPLE_LIGHTS))==(SAMPLE_SUN|SAMPLE_LIGHTS)&&celestial::parked_detector(s.sun_dir,s.light_main)){
  c.source=2;c.parked=1;c.night=1;celestial::toward(s.sun_dir,c.sun);c.sun_y=c.sun[1];
  for(int i=0;i<3;++i)c.moon[i]=st.held?st.moon[i]:c.sun[i];c.star_angle=st.held?st.star_angle:0;
  celestial::Result phase{};celestial::phase_of(tick,phase);c.day_index=phase.day_index;c.phase=phase.phase;c.lit_fraction=phase.lit_fraction;}
 return c;}
// K18 weather v1 ramps (dt from QPC, clamped 0..0.1 s): rain_fx/snow_fx tau 8 s; wet tau 40 s up, 480 s down;
// cover=.6*max(rain_fx,snow_fx); fog_ratio=clamp(native fog/0.01,.5,4) on every frame (0.01 = the game's default
// m_base_fog_factor). A new client_scene (epoch) or a weather gap restarts the ramps at the raw values. The base fog
// never serves as a weather signal (FIN-21).
struct WeatherState{float rain_fx{},snow_fx{},wet{};uint32_t epoch{};bool primed{};};
static void ramp(float& value,float target,double dt,double tau_up,double tau_down){value+=float((target-value)*(1-std::exp(-dt/(target>value?tau_up:tau_down))));}
static scene_frame::Weather make_weather(const scene_frame::Sample& s,const world_time::Observed& o,double dt,WeatherState& st){using namespace scene_frame;Weather w{};w.frame=s.frame;
 w.fog_ratio=float(std::clamp(((s.valid&SAMPLE_FOG)?s.native_fog:.01)/.01,.5,4.));
 if(!o.weather_valid){st.primed=false;return w;}
 w.source=1;w.rain=float(o.rain);w.snow=float(o.snow);
 if(!st.primed||st.epoch!=o.epoch){st.rain_fx=w.rain;st.snow_fx=w.snow;st.wet=w.rain;st.epoch=o.epoch;st.primed=true;}
 else{ramp(st.rain_fx,w.rain,dt,8,8);ramp(st.snow_fx,w.snow,dt,8,8);ramp(st.wet,w.rain,dt,40,480);}
 w.rain_fx=st.rain_fx;w.snow_fx=st.snow_fx;w.wet=st.wet;w.cover=.6f*std::max(st.rain_fx,st.snow_fx);return w;}
// K49 Stage A weight from the clock: 1 in the game's parked window, 0 by day, smooth over 0.03 of a day at each edge,
// so the switch to the parked direction at t=0.2/0.8 happens with no added light.
static double moon_weight(double t){if(t>.8)return celestial::smoothstep(.8,.83,t);if(t<.2)return 1-celestial::smoothstep(.17,.2,t);return 0;}
static constexpr float MOON_SLEW=.03f;   // the applied weight moves at most this much per frame (clock source changes, time jumps)
// K17 time-shaped palettes. Weights sum to 1: noon=c2, golden=c1(1-c2), blue=(1-c1)(1-m), moon=(1-c1)m with
// c1=smoothstep(-.06,.02,e), c2=smoothstep(.10,.30,e), e = the clock's sun height and m = moon_weight(t).
static void palette_weights(double e,double m,double k[4]){double c1=celestial::smoothstep(-.06,.02,e),c2=celestial::smoothstep(.10,.30,e);k[0]=c2;k[1]=c1*(1-c2);k[2]=(1-c1)*(1-m);k[3]=(1-c1)*m;}
static Apply plan_apply(const Policy& policy,const scene_frame::Sample& s,const scene_frame::Clock& c,const scene_frame::Weather& w,float& moon_w,scene_frame::MoonState& moon){
 using namespace scene_frame;const auto& p=policy.scene;const auto& x=policy.extras;Apply a{};a.targets=x.tint_targets;a.fog_scale=p.fog;
 for(int i=0;i<3;++i){a.key[i]=policy.look.key[i];a.shade[i]=policy.look.shade[i];}
 if(x.time_shaped&&c.source==1){double k[4];palette_weights(c.sun_y,moon_weight(c.t),k);
  for(int i=0;i<3;++i){a.key[i]=float(k[0]*x.palette[NOON_KEY][i]+k[1]*x.palette[GOLDEN_KEY][i]+k[2]*x.palette[BLUE_KEY][i]+k[3]*x.palette[MOON_KEY][i]);
   a.shade[i]=float(k[0]*x.palette[NOON_SHADE][i]+k[1]*x.palette[GOLDEN_SHADE][i]+k[2]*x.palette[BLUE_SHADE][i]+k[3]*x.palette[MOON_SHADE][i]);}}
 // AG-9 storm part: a fog scale below 1 gives way to the game's fog as a storm thickens it.
 if(x.weather_fog_keep&&w.source==1&&p.fog<1){double keep=std::clamp((w.fog_ratio-1)/3.,0.,1.)*celestial::smoothstep(0,.3,std::max(w.rain_fx,w.snow_fx));a.fog_scale=p.fog+(1-p.fog)*keep;}
 moon=MoonState{};moon.frame=s.frame;
 const bool on=p.enabled&&x.moon_mode==1&&x.moonlight>0&&(s.valid&SAMPLE_LIGHTS)&&(s.valid&SAMPLE_SUN);
 if(!on)moon_w=0;else{float want=c.source==1?float(moon_weight(c.t)):c.source==2?1.f:0.f;moon_w+=std::clamp(want-moon_w,-MOON_SLEW,MOON_SLEW);}
 // Storm cover dims the moonlight by the moon disc's rule: only while the sky's weather is on (scene effects v3
 // weather 1, AnyGraphics Weather response). With it off neither the light nor the disc reacts to the weather.
 scene_frame::SkyPolicy sky{};const bool storms=scene_frame::board.sky_policy.load(sky)&&sky.weather==1&&w.source==1;
 if(moon_w>0){double lit=std::max<double>(x.moon_phase_floor,c.lit_fraction),target=x.moonlight*lit*(1-.7*(storms?w.cover:0));
  double current=luma(s.light_main)*p.sun*std::exp2(p.light_exposure);a.moon_add=moon_w*std::max(0.,target-current);
  if(a.moon_add>0){moon.active=1;moon.added_luminance=float(a.moon_add);celestial::toward(s.sun_dir,moon.direction);}}
 return a;}
// GL-11: renderer+0x664 m_build_cloud_lighting_counter (s32); renderer.rebuild_cloud_lighting stores 2 there.
// _build_scene runs before _build_cloud_particles in _build_renderables, so a write here relights this frame's clouds.
static constexpr double CLOUD_JUMP=.003;   // of a day: 648 ticks, about 11 s of real time
struct CloudWatch{double cycle{-1};int clouds{-1};uint64_t relights{};};
static CloudWatch cloud_watch;
static bool relight_wanted(CloudWatch& cw,const scene_frame::Sample& s,int clouds){bool want=false;
 if(s.valid&scene_frame::SAMPLE_CYCLE){if(cw.cycle>=0&&std::abs(cycle_delta(s.cycle,cw.cycle))>CLOUD_JUMP)want=true;cw.cycle=s.cycle;}
 if(clouds==0||clouds==1){if(cw.clouds==0&&clouds==1)want=true;cw.clouds=clouds;}return want;}
static void rebuild_cloud_lighting(void* renderer,const scene_frame::Sample& s){unsigned char clouds=0xff;if(renderer&&!scene_frame::read_value(uintptr_t(renderer)+0x63e,clouds))clouds=0xff;
 if(!relight_wanted(cloud_watch,s,clouds<=1?int(clouds):-1)||!writable(renderer,0x688))return;
 int32_t counter{};auto at=(unsigned char*)renderer+0x664;memcpy(&counter,at,4);if(counter<0||counter>=2)return;counter=2;memcpy(at,&counter,4);
 auto n=++cloud_watch.relights;if(n<=8||n%100==0)log_line(ANY_LOG_INFO,"framework",("SCENE_CLOUD_RELIGHT count="+std::to_string(n)+" cycle="+std::to_string(s.cycle)+" clouds="+std::to_string(clouds)).c_str());}
// P0 probes: each line is logged once (source changes: the first 16); they read and never write.
struct Probes{bool cycle{},calibrated{},uncalibrated{},mismatch{},planet{},weather{},storm[2]{},drift{},rain_depth{};uint32_t sources{},last_source{UINT32_MAX},rain_tries{};double offset[2]{};uint64_t offset_ms{};};
static constexpr uint32_t RAIN_DEPTH_TRIES=3600;   // frames to wait for a rain-depth target and projection before logging what there is
static Probes probes;
static void probe_frame(void* renderer,uintptr_t scene,const Native& n,const scene_frame::Clock& c,const scene_frame::Weather& w,const world_time::Observed& o,bool cycle_fresh,const ClockState& st){
 using namespace scene_frame;const auto& s=n.sample;char line[512];auto out=[&](auto level){log_line(level,"framework",line);};
 if(!probes.cycle&&(s.valid&SAMPLE_CYCLE)&&cycle_fresh){probes.cycle=true;double tick_cycle=o.tick>=0?double((o.tick+celestial::START_OFFSET)%celestial::DAY_TICKS)/double(celestial::DAY_TICKS):-1;
  snprintf(line,sizeof(line),"SCENE_CLOCK probe=cycle ba8=%.9f world_time=%.9f delta=%.3g tick=%lld tick_cycle=%.9f",s.cycle,o.cycle,cycle_delta(o.cycle,s.cycle),(long long)o.tick,tick_cycle);out(ANY_LOG_INFO);}
 if(!probes.calibrated&&st.cal.done){probes.calibrated=true;snprintf(line,sizeof(line),"SCENE_CLOCK calibrated=1 sx=%d sz=%d t=%.6f day_frames=%u",st.cal.sx,st.cal.sz,s.cycle,st.day_frames);out(ANY_LOG_INFO);}
 if(!probes.uncalibrated&&!st.cal.done&&st.day_frames>=1800){probes.uncalibrated=true;snprintf(line,sizeof(line),"SCENE_CLOCK calibrated=0 day_frames=%u t=%.6f sun_dir=(%.6f,%.6f,%.6f)",st.day_frames,s.cycle,s.sun_dir[0],s.sun_dir[1],s.sun_dir[2]);out(ANY_LOG_WARN);}
 if(!probes.mismatch&&st.mismatch>=CROSS_FRAMES){probes.mismatch=true;snprintf(line,sizeof(line),"SCENE_CLOCK cross_check=failed frames=%u ba8=%.9f world_time=%.9f",st.mismatch,s.cycle,o.cycle);out(ANY_LOG_WARN);}
 if(c.source!=probes.last_source&&probes.sources<16){++probes.sources;probes.last_source=c.source;
  snprintf(line,sizeof(line),"SCENE_CLOCK source=%u t=%.5f parked=%u sun_y=%.4f night=%.3f moon_y=%.4f day=%u phase=%.3f lit=%.3f",c.source,c.t,c.parked,c.sun_y,c.night,c.moon[1],c.day_index,c.phase,c.lit_fraction);out(ANY_LOG_INFO);}
 if(!probes.planet&&c.source==1&&std::abs(c.t-.9)<.01){probes.planet=true;snprintf(line,sizeof(line),"SCENE_CLOCK probe=planet_shadow t=%.4f value=%.3f expect=-4096",c.t,n.planet_shadow);out(ANY_LOG_INFO);}
 if(!probes.weather&&w.source==1){probes.weather=true;
  snprintf(line,sizeof(line),"WEATHER_PROBE sampled=1 rain=%.4f snow=%.4f particles=%u scene_wind=(%.4f,%.4f) particle_wind=(%.4f,%.4f) fog=%.5f",w.rain,w.snow,unsigned(s.weather_particles),s.wind[0],s.wind[1],o.wind[0],o.wind[1],s.native_fog);out(ANY_LOG_INFO);}
 if(w.source==1&&std::max(w.rain,w.snow)>.05f&&(s.valid&SAMPLE_FLAGS)&&!probes.storm[s.weather_particles?1:0]){probes.storm[s.weather_particles?1:0]=true;
  snprintf(line,sizeof(line),"WEATHER_PROBE storm=1 particles=%u rain=%.4f snow=%.4f fog=%.5f fog_ratio=%.3f cover=%.3f wet=%.3f",unsigned(s.weather_particles),w.rain,w.snow,s.native_fog,w.fog_ratio,w.cover,w.wet);out(ANY_LOG_INFO);}
 if(!probes.drift&&finite_range(n.weather_offset,2,-1e9,1e9)){uint64_t now=GetTickCount64();
  if(!probes.offset_ms){probes.offset_ms=now;probes.offset[0]=n.weather_offset[0];probes.offset[1]=n.weather_offset[1];}
  else if(now-probes.offset_ms>=10000){probes.drift=true;double sec=double(now-probes.offset_ms)/1000;
   snprintf(line,sizeof(line),"SCENE_PROBE weather_offset_drift=(%.6g,%.6g)/s seconds=%.1f wind=(%.3f,%.3f)",(n.weather_offset[0]-probes.offset[0])/sec,(n.weather_offset[1]-probes.offset[1])/sec,sec,s.wind[0],s.wind[1]);out(ANY_LOG_INFO);}}
 if(!probes.rain_depth&&renderer&&scene){
  // renderer+0xa8 m_target_rain_depth (ref: object at +0xb0; width +0x34, height +0x38, SRV format +0x50); scene+0x2f0 m_rain_camera
  // (camera layout as the main camera at +0x20: position +0x108, projection +0x1a0, 16 doubles).
  uintptr_t target{};uint32_t size[2]{},format{};bool t=read_value(uintptr_t(renderer)+0xb0,target)&&target&&read(target+0x34,size,8)&&read_value(target+0x50,format);
  double m[16]{},rain[3]{},main[3]{};bool cam=read(scene+0x2f0+0x1a0,m,sizeof(m))&&read(scene+0x2f0+0x108,rain,24)&&read(scene+0x20+0x108,main,24);
  const bool complete=t&&size[0]&&size[1]&&cam&&std::isfinite(m[0])&&m[0]!=0;
  if(complete||++probes.rain_tries>=RAIN_DEPTH_TRIES){probes.rain_depth=true;
  snprintf(line,sizeof(line),"RAIN_DEPTH_PROBE target=%d width=%u height=%u format=%u camera=%d m0=%.6g m5=%.6g m10=%.6g m14=%.6g m15=%.6g extent=(%.3f,%.3f) offset=(%.2f,%.2f,%.2f)",
   t,size[0],size[1],format,cam,m[0],m[5],m[10],m[14],m[15],m[0]?2/std::abs(m[0]):0.,m[5]?2/std::abs(m[5]):0.,rain[0]-main[0],rain[1]-main[1],rain[2]-main[2]);out(complete?ANY_LOG_INFO:ANY_LOG_WARN);}}
}
static double qpc_seconds(int64_t ticks){static const double hz=[]{LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart?double(f.QuadPart):1.;}();return double(ticks)/hz;}
struct FrameState{ClockState clock;WeatherState weather;float moon_w{};int64_t qpc{};};
static FrameState frame_state;   // render thread only
static void scene_hook(void* renderer,void* scene){double wind[2]{};bool windy=false;
 {scene_frame::CpuScope cpu;++scene_calls;auto policy=snapshot();const auto& p=policy.scene;auto& st=frame_state;
  const bool scene_ok=writable(scene,0x2bf8);   // K24: one VirtualQuery for scene_apply, wind_scale and the jitter
  LARGE_INTEGER now{};QueryPerformanceCounter(&now);const double dt=st.qpc?std::clamp(qpc_seconds(now.QuadPart-st.qpc),0.,.1):0;st.qpc=now.QuadPart;
  auto n=read_native(uintptr_t(scene),scene_frame::board.frame.load(std::memory_order_relaxed));
  world_time::Observed obs;const bool cycle_fresh=world_time::shared.observed(obs,GetTickCount64());
  auto clock=make_clock(n.sample,cycle_fresh?&obs:nullptr,st.clock);auto weather=make_weather(n.sample,obs,dt,st.weather);
  scene_frame::board.sample.store(n.sample);scene_frame::board.clock.store(clock);scene_frame::board.weather.store(weather);
  scene_frame::MoonState moon{};auto apply=plan_apply(policy,n.sample,clock,weather,st.moon_w,moon);
  const bool applied=p.enabled&&scene_apply(scene,scene_ok,p,apply);if(p.enabled&&!applied)++rejected_frames;
  if(!applied){moon.active=0;moon.added_luminance=0;}scene_frame::board.moon.store(moon);
  tint_targets_active.store(p.enabled?policy.extras.tint_targets:0,std::memory_order_relaxed);
  windy=p.enabled&&wind_scale(scene,scene_ok,policy.look.wind,wind);
  if(p.enabled)rebuild_cloud_lighting(renderer,n.sample);   // GL-11 only under an enabled owner (SC-3)
  probe_frame(renderer,uintptr_t(scene),n,clock,weather,obs,cycle_fresh,st.clock);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
  if(scene_ok){scene_antialiasing::apply_jitter(renderer,scene);scene_effects::photo_scene(scene);}   // SC-13 (effects package)
#endif
 }
 ((SceneFn)scene_cell.original())(renderer,scene);if(windy)memcpy((unsigned char*)scene+0xb78,wind,16);}
// Native scene/UI boundary: the AA replacement records on the native command
// list after scene composition, before either native UI renderer.
static std::atomic<uint64_t> before_hud_calls{};
static bool pre_hud_ready{};
static void pre_hud_hook(void* renderer,void* scene){
 ((SceneFn)pre_hud_cell.original())(renderer,scene);scene_frame::CpuScope cpu;auto calls=++before_hud_calls;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 anyapi_creation_balance_render_camera(scene);
 scene_antialiasing::record(renderer,scene);
#endif
 if(calls==1||calls%600==0){uintptr_t target{};int32_t dimensions[2]{};
  bool valid=safe_read_memory(uintptr_t(renderer)+0x3c0,&target,8)&&target&&safe_read_memory(target+0x34,dimensions,8)&&dimensions[0]>0&&dimensions[0]<=16384&&dimensions[1]>0&&dimensions[1]<=16384;
  log_line(ANY_LOG_INFO,"framework",("SCENE_AA_STAGE before_hud_calls="+std::to_string(calls)+" source_target_valid="+std::to_string(valid)+" width="+std::to_string(valid?dimensions[0]:0)+" height="+std::to_string(valid?dimensions[1]:0)+" diagnostic_scene_target=1").c_str());
 }
}
static void lighting_hook(void* renderer,void* scene){((SceneFn)lighting_cell.original())(renderer,scene);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_frame::CpuScope cpu;
 scene_effects::record_contact(renderer,scene);
 scene_effects::record_gi(renderer,scene);
 scene_effects::record_water(renderer,scene);
 scene_lighting::record(renderer,scene);
 scene_effects::record_sky(renderer,scene);
 scene_effects::record_lens(renderer,scene);
 scene_effects::record(renderer,scene);
#endif
}
using SsaoFn=void(*)(void*,void*);
// K12: AnyAPI AO overwrites the game's SSAO target without reading it. With the effects seam the native SSAO runs
// only when AnyAPI AO did not render this frame, so any bypass falls back in the same frame. After a failed replace,
// record_ao retries once after the native SSAO (effects skips that retry unless the failure was 201, bindings).
// No seam: today's order.
static void ssao_hook(void* self,void* camera){
 if(auto replace=scene_frame::seams.replace_ssao){bool replaced;{scene_frame::CpuScope cpu;replaced=replace(composite_renderer,composite_scene);}if(replaced)return;}
 ((SsaoFn)ssao_cell.original())(self,camera);
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_frame::CpuScope cpu;scene_effects::record_ao(composite_renderer,composite_scene);
#endif
}
static void composite_hook(void* renderer,void* scene){composite_renderer=renderer;composite_scene=scene;((SceneFn)composite_cell.original())(renderer,scene);composite_renderer=composite_scene=nullptr;}
static const unsigned char COMPOSITE[]={0x53,0x55,0x56,0x48,0x81,0xec,0x20,0x02,0,0,0x48,0x89,0x8c,0x24,0xc0,0x01,0,0,0x48,0x89,0x94,0x24,0xc8,0x01,0,0,0x48,0x8b,0x05,0xaf,0x0c,0};
static constexpr size_t COMPOSITE_END=3280;
static const unsigned char SSAO[]={0x53,0x55,0x48,0x81,0xec,0xc8,0,0,0,0x48,0x89,0x4c,0x24,0x60,0x48,0x89,0x54,0x24,0x68,0x48,0x8d,0x44,0x24,0x70,0x48,0x8d,0x19,0x48,0x8b,0x2d,0xde,0x01};
static const unsigned char ADDITIVE[]={0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x81,0xec,0x50,0x01,0,0,0x48,0x89,0x8c,0x24,0xe8,0,0,0,0x48,0x89,0x94,0x24,0xf0,0,0,0,0x48,0x8b,0x05};
static const unsigned char RECORD_COMMANDS[]={0x53,0x55,0x56,0x48,0x81,0xec,0x10,0x02,0,0,0x48,0x89,0x8c,0x24,0xb0,0x01,0,0,0x48,0x89,0x94,0x24,0xb8,0x01,0,0,0x48,0x8b,0x05,0x07,0x0a,0};
static const unsigned char POST_COMPOSITE[]={0x53,0x55,0x56,0x48,0x81,0xec,0x80,0x04,0,0,0x48,0x89,0x8c,0x24,0x28,0x04,0,0,0x48,0x89,0x94,0x24,0x30,0x04,0,0,0x48,0x8b,0x05,0x17,0x17,0};
static const unsigned char ON_RENDER[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x15,0,0,0,0x48,0x8d,0x08,0x48,0x8b,0x05,0x13,0,0,0,0xff,0x10,0x48,0x83,0xc4,0x28,0xc3};
static const unsigned char BUILD_RENDERABLES[]={0x53,0x55,0x48,0x83,0xec,0x58,0x48,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x28,0x48,0x8b,0x05,0x51,0x03,0,0,0x48,0x83,0x78,0x08,0};
static const unsigned char RENDER_TARGET[]={0x53,0x48,0x81,0xec,0x30,0x2c,0,0,0x48,0x89,0x4c,0x24,0x20,0x8a,0x81,0x90,0,0,0,0x3c,0,0x0f,0x84,0x30,0,0,0,0x8a,0x05,0x17,0x01,0};
static const unsigned char SCENE_TARGET[]={0x53,0x55,0x56,0x57,0x48,0x81,0xec,0x68,0x05,0,0,0x48,0x89,0x8c,0x24,0x10,0x04,0,0,0x48,0x89,0x94,0x24,0x18,0x04,0,0,0x4c,0x8d,0x8c,0x24,0x20};
static void** resolve_cell(uintptr_t caller,size_t end,size_t slot,const unsigned char* target,size_t bytes){uintptr_t cell{},body{};unsigned char prefix[32]{};
 if(!safe_read_memory(caller+end+slot*8,&cell,8)||!cell||!safe_read_memory(cell,&body,8)||!body||!safe_read_memory(body,prefix,bytes)||memcmp(prefix,target,bytes))return nullptr;
 return (void**)cell;
}
// on_render's short body is shared by hundreds of unrelated wrappers. Select
// by the reviewed callee and require one distinct call cell, not one wrapper.
static void** resolve_unique_cell(const std::vector<uintptr_t>& callers,size_t end,size_t slot,const unsigned char* target,size_t bytes){void** selected=nullptr;
 for(auto caller:callers){auto cell=resolve_cell(caller,end,slot,target,bytes);if(!cell)continue;if(selected&&selected!=cell)return nullptr;selected=cell;}return selected;
}
// P0 LITERAL_PROBE (K44/K45): verify the game literals a later release may patch. Worker thread, once; read only.
// Offsets from the 0.1.27 game code (render_util and client_scene_tile): each user site is a
// RIP-relative load whose displacement lands on the literal (user + size + disp == literal).
struct LiteralSite{const char* name;uint32_t user;unsigned char bytes[8];uint32_t size;uint32_t literal;double expected;};
static const unsigned char SHADOWS_PROLOGUE[]={0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x78,0x0f,0,0}; // render_util.build_camera_shadows
static const unsigned char TILE_PROLOGUE[]={0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x48,0x81,0xec,0xf8,0x09,0,0,0x48,0x8b,0xb4,0x24,0x50,0x0a,0,0}; // client_scene.environment_tile.render
static const LiteralSite SHADOW_SITES[]={{"shadow_near",0x102,{0xf2,0x0f,0x10,0x05,0x96,0x2d,0,0},8,0x2ea0,.1},{"shadow_far",0x113,{0xf2,0x0f,0x10,0x05,0x8d,0x2d,0,0},8,0x2ea8,1000},
 {"cascade_split",0x124,{0xf2,0x0f,0x10,0x05,0x84,0x2d,0,0},8,0x2eb0,.98}};
static const LiteralSite TILE_SITES[]={{"tile_fade",0x1db4,{0xf2,0x0f,0x59,0x05,0x7c,0x11,0,0},8,0x2f38,UNSET},{"tree_near",0x1df8,{0x48,0x8d,0x0d,0x41,0x11,0,0},7,0x2f40,200},
 {"tree_far",0x1dff,{0x48,0x8d,0x15,0x42,0x11,0,0},7,0x2f48,240},{"grass_near",0x27e6,{0x48,0x8d,0x0d,0x63,0x07,0,0},7,0x2f50,10},{"grass_far",0x27ed,{0x48,0x8d,0x15,0x64,0x07,0,0},7,0x2f58,15}};
// The body among candidates whose every user site matches; 0 unless exactly one does.
static uintptr_t literal_body(const std::vector<uintptr_t>& candidates,const LiteralSite* sites,size_t count,uint32_t& verified){uintptr_t body{};verified=0;
 for(auto at:candidates){bool all=true;for(size_t i=0;i<count&&all;++i){unsigned char b[8]{};all=scene_frame::read(at+sites[i].user,b,sites[i].size)&&!memcmp(b,sites[i].bytes,sites[i].size);}if(all){body=at;++verified;}}
 return verified==1?body:0;}
static std::string literal_report(const char* function,size_t candidates,uintptr_t body,uint32_t verified,const LiteralSite* sites,size_t count){char item[96];
 std::string line="LITERAL_PROBE function="+std::string(function)+" candidates="+std::to_string(candidates)+" verified="+std::to_string(verified)+" written=0";
 for(size_t i=0;body&&i<count;++i){double v{};bool ok=scene_frame::read_value(body+sites[i].literal,v);
  snprintf(item,sizeof(item)," %s=%s%.9g%s",sites[i].name,ok?"":"unreadable:",ok?v:0.,!ok||std::isnan(sites[i].expected)?"":v==sites[i].expected?"(ok)":"(differs)");line+=item;}
 return line;}
static void literal_probe(){
 auto run=[](const char* function,const unsigned char* prologue,size_t bytes,const LiteralSite* sites,size_t count){auto hits=scan_exact(prologue,bytes);uint32_t verified{};auto body=literal_body(hits,sites,count,verified);
  log_line(body?ANY_LOG_INFO:ANY_LOG_WARN,"framework",literal_report(function,hits.size(),body,verified,sites,count).c_str());};
 run("render_util.build_camera_shadows",SHADOWS_PROLOGUE,sizeof(SHADOWS_PROLOGUE),SHADOW_SITES,std::size(SHADOW_SITES));
 run("client_scene.environment_tile.render",TILE_PROLOGUE,sizeof(TILE_PROLOGUE),TILE_SITES,std::size(TILE_SITES));}
static DWORD WINAPI worker(void*){for(unsigned attempt=0;attempt<120&&!ready;++attempt){auto render=scan_exact(ON_RENDER,sizeof(ON_RENDER)),scene=scan_exact(BUILD_RENDERABLES,sizeof(BUILD_RENDERABLES));
 {auto a=resolve_unique_cell(render,32,1,RENDER_TARGET,sizeof(RENDER_TARGET)),b=resolve_unique_cell(scene,872,8,SCENE_TARGET,sizeof(SCENE_TARGET));
  if(a&&b&&renderer_cell.install(a,(void*)renderer_hook)){if(scene_cell.install(b,(void*)scene_hook)){auto record=scan_exact(RECORD_COMMANDS,sizeof(RECORD_COMMANDS));auto boundary=resolve_unique_cell(record,2600,37,POST_COMPOSITE,sizeof(POST_COMPOSITE));
 if(boundary&&pre_hud_cell.install(boundary,(void*)pre_hud_hook)){pre_hud_ready=true;
#ifndef ANYAPI_SCENE_CONTROLS_TEST
 scene_antialiasing::available=true;
 // AA-1: AA records inside _record_commands_postprocess, before the editor overlay; slot 37 stays its fallback.
 if(scene_frame::seams.install_postprocess)scene_frame::seams.install_postprocess(record);
 auto light=resolve_unique_cell(record,2600,33,ADDITIVE,sizeof(ADDITIVE));if(light&&lighting_cell.install(light,(void*)lighting_hook)){scene_lighting::available=true;scene_effects::available=true;log_line(ANY_LOG_INFO,"framework","SCENE_LIGHTING resolved=1 location=HDR_AFTER_ADDITIVE_BEFORE_BLOOM_TONEMAP_HUD experimental=1");}
 // _record_commands_composite runs the game's SSAO from its own call cell.
 auto composite=resolve_unique_cell(record,2600,28,COMPOSITE,sizeof(COMPOSITE));uintptr_t composite_body{};
 if(composite&&safe_read_memory(uintptr_t(composite),&composite_body,8)&&composite_body){auto ssao=resolve_cell(composite_body,COMPOSITE_END,6,SSAO,sizeof(SSAO));
  if(ssao&&composite_cell.install(composite,(void*)composite_hook)){if(ssao_cell.install(ssao,(void*)ssao_hook)){scene_effects::ao_available=true;log_line(ANY_LOG_INFO,"framework",scene_frame::seams.replace_ssao?"SCENE_AO resolved=1 location=COMPOSITE_SSAO replaces_native_ssao=1":"SCENE_AO resolved=1 location=COMPOSITE_AFTER_NATIVE_SSAO replaces_native_ssao=0");}else composite_cell.remove();}}
 if(!scene_effects::ao_available)log_line(ANY_LOG_WARN,"framework","SCENE_AO unavailable=1 native_ssao_kept=1");
 scene_effects::install_distance(resolve_cell,scan_exact);scene_effects::install_photo(record);
#endif
 log_line(ANY_LOG_INFO,"framework","SCENE_AA_STAGE resolved=1 location=AFTER_SCENE_COMPOSITION_BEFORE_HUD scene_replacement_available=1");}
 else log_line(ANY_LOG_WARN,"framework","SCENE_AA_STAGE unavailable=1 replacement_AA_not_exposed=1");
 ready=true;log_line(ANY_LOG_INFO,"framework","SCENE_CONTROLS ready=1 phase=BEFORE_NATIVE_SCENE_BUILD native_AA_bloom_lighting_fog=1 shared_call_cells=1");break;}renderer_cell.remove();}
 }Sleep(1000);}if(!ready)log_line(ANY_LOG_WARN,"framework","SCENE_CONTROLS unavailable native_settings_preserved=1");literal_probe();return 0;}
}
