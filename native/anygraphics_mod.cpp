#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_controls_v3.h"
#include "anyapi_scene_controls_v4.h"
#include "anyapi_scene_lighting_v3.h"
#include "anyapi_scene_lighting_v4.h"
#include "anyapi_scene_antialiasing_v2.h"
#include "anyapi_scene_antialiasing_v3.h"
#include "anyapi_scene_timing_v1.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_gpu_info_v1.h"
#include "anyapi_gpu_info_v2.h"
#include "anyapi_scene_effects_v1.h"
#include "anyapi_scene_effects_v2.h"
#include "anyapi_scene_effects_v3.h"
#include "anyhelpers_settings_v2.h"
#include "anygraphics_options.h"
#include "anyapi_menu_v4.h"
#include "anyapi_build_v1.h"
#include "anyhelpers_settings_state.h"
#include "mod_controls_v1.h"
#include <mutex>
#include "anygraphics_photo.h"
namespace graphics {
using namespace graphics_options;
static AnyModHostV1 host;
static const AnySceneControlsV1* scene;
static const AnySceneControlsV2* scene_v2;
static const AnySceneControlsV3* scene_v3;
static const AnySceneLightingV1* lighting;static const AnySceneLightingV2* lighting_v2;static const AnySceneLightingV3* lighting_v3;
static const AnySceneAntialiasingV1* aa;static const AnySceneAntialiasingV2* aa_v2;
static const AnySceneTimingServiceV1* timing;static const AnyGpuInfoServiceV1* gpu_info;
// AnyAPI 0.40.2 versions. Each is null on an older API, which then gets the version below it; rows that need one hide.
static const AnySceneControlsV4* scene_v4;static const AnySceneLightingV4* lighting_v4;static const AnySceneAntialiasingV3* aa_v3;
static const AnySceneTimingServiceV2* timing_v2;static const AnyGpuInfoServiceV2* gpu_info_v2;static uint32_t lamp_budget_max=8;
// AnyAPI 0.39.1 switches lamp beams off on AMD GPUs (they corrupt the picture there); hide their rows too.
static bool lamp_beams_blocked(){AnyGpuInfoV1 g;return gpu_info&&gpu_info->copy(&g)&&g.known&&!g.lamp_beams_supported;}
// K11: from AnyAPI 0.39.2 lamp beams run on AMD without the game's spotlight shadow maps.
static bool lamp_shadows_missing(){if(gpu_info_v2){AnyGpuInfoV2 g;return gpu_info_v2->copy_v2(&g)&&g.v1.known&&(!g.lamp_shadows_supported||g.v1.vendor_id==0x1002);}
 AnyGpuInfoV1 g;return gpu_info&&gpu_info->copy(&g)&&g.known&&g.lamp_beams_supported&&g.vendor_id==0x1002;}
static const AnySceneEffectsV1* effects;
// Effects v2 (AnyAPI 0.40.2) adds ocean reflections and sun glint; without it those rows are hidden.
static const AnySceneEffectsV2* effects_v2;static const AnySceneEffectsV3* effects_v3;
static helpersettings::State state;static std::mutex mutex;
static const AnyMenuV1* menu;static const AnyMenuV3* widgets;
static const AnyUiStateV1* ui;
// Photo mode and auto-pick are per-session: neither is saved with the settings.
static const ModControlsV1* controls;static uint64_t photo_action;static bool photo{},photo_key_down{},last_hide{};static uint64_t photo_toggled_at{};
static uint32_t photo_key(){return controls&&photo_action?controls->key(photo_action):VK_F1;}
// Auto-pick (K35) steps through Performance..Ultra until gameplay holds about 60 fps, from the median and the slowest 5%
// of frame times and the GPU time of AnyAPI's passes. A look keeps its style at each cost level (the preset becomes Custom).
struct Pick {bool on{},went_down{},confirming{},commit_pending{},confirm_custom{};int tier{},start{},look{};double seconds{},next_sample{},pass_sum{},block_sum{};uint64_t last{};uint32_t count{},pass_samples{},block_samples{};std::array<float,2048> dts{};};
static Pick pick;static std::string pick_result;
// K34: the last gameplay readout and what was running, shown while Gameplay only bypasses AnyGraphics in a menu.
struct LastCost {bool have{},aa{},lighting{},ao{},water{},effects{};std::string gpu,frame;};
static LastCost last_cost;static uint64_t last_cost_at{};static std::atomic<bool> menu_bypass{};
// What the last configuration switched on, so the readout says "off" for a pass AnyGraphics turned off.
static std::atomic<bool> sent_lighting{},sent_aa{};static std::atomic<uint32_t> sent_lamps{};
static Values values=defaults();
static uint64_t tokens[Count]{},revision=UINT64_MAX,status_at{},retry_at{};
static bool configured{},last_on{},native_ready{};
static void log(const std::string& text){host.log(0,"anygraphics",text.c_str());}
static void log_once(bool& done,const char* text){if(!done){done=true;log(text);}}
static void read(){std::lock_guard lock(mutex);if(revision==state.revision)return;revision=state.revision;for(size_t i=0;i<Count;++i)values[i]=clean(i,state.settings[i].committed.number);configured=false;log("SETTINGS_APPLIED revision="+std::to_string(revision)+" native_only=1");}
static uint32_t dirty(void*){std::lock_guard lock(mutex);return state.dirty();}
static uint32_t defaults(void*){std::lock_guard lock(mutex);return state.defaults();}
static void event(uint32_t kind,void*){std::lock_guard lock(mutex);if(kind==ANY_MENU_OPEN)state.begin();else if(kind==ANY_MENU_RESET)state.reset();else if(kind==ANY_MENU_CANCEL)state.cancel();else if(kind==ANY_MENU_APPLY){if(!state.apply())log("SETTINGS_SAVE failed draft_retained=1");}}
// AG-16: an older API offers fewer choices than a preset can ask for. TAA becomes Enhanced SMAA with scene AA v1 and FXAA
// without scene AA; any other choice row takes its last choice.
static float fit(size_t j,float value,size_t choices){if(definitions[j].kind!=ANY_SETTING_CHOICE||value<float(choices))return value;return j==NativeAA?(aa?4.f:2.f):float(choices-1);}
static float fit(size_t j,float value){return fit(j,value,state.settings[j].choices.size());}
static void stage_values(const Values& v){for(size_t j=0;j<Count;++j){auto next=state.settings[j].draft;next.number=fit(j,v[j]);state.stage(j,next);}}
// Stages every value of a preset into the menu draft. The status line says so, not the last row's description (AG-15).
static void stage_preset(int level){Values draft{};for(size_t j=0;j<Count;++j)draft[j]=float(state.settings[j].draft.number);stage_values(profile(draft,level));state.status="Preset staged. Select Apply to save.";}
// Commits values outside the menu (auto-pick). False when the settings could not be saved; nothing changes then.
static bool commit_values(const Values& v){std::lock_guard lock(mutex);state.begin();stage_values(v);bool ok=state.apply();if(!ok){log("SETTINGS_SAVE failed draft_retained=0 source=auto_pick");state.begin();}state.status.clear();return ok;}
static Values committed(){std::lock_guard lock(mutex);Values c{};for(size_t j=0;j<Count;++j)c[j]=float(state.settings[j].committed.number);return c;}
static const char* tier_name(int t){static const char* names[]={"Off","Performance","Low","Balanced","High","Ultra"};return names[std::clamp(t,0,5)];}
static const char* look_name(int level){return level==NaturalProfile?"Natural":level==VibrantProfile?"Vibrant":level==MoodyProfile?"Moody":level==ClearDayProfile?"Clear Day":level==SoftWarmProfile?"Soft & warm":"Custom";}
// A tier's values: the preset itself, or the starting look on top of the tier's costs, as Custom.
static Values pick_values(int t){auto c=committed();if(!pick.look)return profile(c,t);auto v=tier_values(c,size_t(t-1));apply_style(pick.look,v);v[Preset]=float(CustomProfile);return v;}
static void pick_window(){pick.seconds=0;pick.next_sample=0;pick.pass_sum=0;pick.block_sum=0;pick.last=0;pick.count=0;pick.pass_samples=0;pick.block_samples=0;}
static bool pick_commit(int t){if(commit_values(pick_values(t))){pick_window();return true;}pick.on=false;pick_result="Auto-pick stopped: the settings could not be saved.";log("AUTO_PICK stopped=1 reason=save_failed");return false;}
// GPU ms of AnyAPI's scene passes now. The overlay is left out: it is CPU time the frame times already hold. -1 unknown.
static float pass_ms_now(){
 if(timing_v2){AnySceneTimingV2 t;if(timing_v2->copy_v2(&t)){float sum=0;for(uint32_t i=0;i<=ANY_PASS_AA;++i)if(t.pass_ms[i]>0)sum+=t.pass_ms[i];return sum;}}
 float sum=0;bool known=false;auto add=[&](bool ready,float ms){if(ready&&ms>0)sum+=ms;};
 if(timing){AnySceneTimingV1 t;if(timing->copy(&t)){known=true;add(sent_lighting,t.lighting_ms);add(sent_aa,t.antialiasing_ms);}}
 if(effects){AnySceneEffectsStatusV1 e;if(effects->status(&e)){known=true;add(e.ready,e.gpu_ms);add(e.ao_ready,e.ao_ms);add(e.sky_ready,e.sky_ms);add(e.contact_ready,e.contact_ms);add(e.gi_ready,e.gi_ms);add(e.lens_ready,e.lens_ms);}}
 if(effects_v2){AnySceneEffectsStatusV2 w;if(effects_v2->status_v2(&w))add(w.water_ready,w.water_ms);}
 return known?sum:-1;}
// Scene timing v2: median ms inside the game's Present; -1 without timing v2 or before it is measured.
static float present_block_now(){AnySceneTimingV2 t;return timing_v2&&timing_v2->copy_v2(&t)&&t.present_block_ms>=0?t.present_block_ms:-1.f;}
// One auto-pick step: measure 4 s of gameplay (after 1.5 s to settle), then move a tier or stop. It measures only while
// AnyGraphics is on and connected to the renderer (AG-5) and stops when a save fails (AG-13, CPU-8).
static void auto_pick(bool gameplay,bool active,bool configured_now){if(!pick.on)return;
 if(!gameplay){pick.last=0;return;}
 if(pick.commit_pending){pick.commit_pending=false;pick_commit(pick.tier);return;}
 if(!active){pick.last=0;pick_result="Auto-pick paused: AnyGraphics is off or not connected to the game's renderer.";return;}
 if(!configured_now){pick.last=0;return;}
 LARGE_INTEGER now,rate;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&rate);
 if(pick.last){double dt=double(now.QuadPart-pick.last)/rate.QuadPart;if(dt<.5){pick.seconds+=dt;if(pick.seconds>1.5){if(pick.count<pick.dts.size())pick.dts[pick.count++]=float(dt*1000);
  if(pick.seconds>=pick.next_sample){pick.next_sample=pick.seconds+.25;float ms=pass_ms_now();if(ms>=0){pick.pass_sum+=ms;++pick.pass_samples;}float block=present_block_now();if(block>=0){pick.block_sum+=block;++pick.block_samples;}}}}}
 pick.last=now.QuadPart;
 if(pick.seconds<5.5||pick.count<8)return;
 PickWindow w;std::sort(pick.dts.begin(),pick.dts.begin()+pick.count);w.median_ms=pick.dts[pick.count/2];w.p95_ms=pick.dts[std::min(pick.count-1,uint32_t(pick.count*.95f))];w.pass_ms=pick.pass_samples?float(pick.pass_sum/pick.pass_samples):-1;w.present_block_ms=pick.block_samples?float(pick.block_sum/pick.block_samples):-1;
 const auto d=pick_decide(pick.tier,pick.start,pick.went_down,pick.confirming,w);const float fps=1000/w.median_ms;char text[320];
 snprintf(text,sizeof(text),"AUTO_PICK tier=%s look=%d median_ms=%.2f p95_ms=%.2f pass_ms=%.2f present_block_ms=%.2f cap_ms=%.2f next=%s done=%d reason=%u",tier_name(pick.tier),pick.look,w.median_ms,w.p95_ms,w.pass_ms,w.present_block_ms,pick_cap(w),tier_name(d.tier),int(d.done),unsigned(d.reason));log(text);
 pick.confirming=d.reason==PickConfirm;
 if(!d.done){
  if(d.tier==pick.tier){pick_window();return;} // slow once: measure again before stepping down
  if(d.tier<pick.tier)pick.went_down=true;
  snprintf(text,sizeof(text),"Auto-pick: %s ran at %.0f fps, trying %s.",tier_name(pick.tier),fps,tier_name(d.tier));pick_result=text;pick.tier=d.tier;pick_commit(d.tier);return;}
 if(d.tier!=pick.tier){pick.tier=d.tier;if(!pick_commit(d.tier))return;}
 std::string chosen=pick.look?std::string(look_name(pick.look))+" at "+tier_name(pick.tier)+" cost":tier_name(pick.tier);
 const char* why=d.reason==PickCpuBound?" The game or CPU sets the frame rate here, so presets barely change it.":d.reason==PickCapFull?" The frame rate is capped, and more would leave too little of each frame.":"";
 snprintf(text,sizeof(text),"Auto-pick chose %s (%.0f fps).%s%s",chosen.c_str(),fps,why,pick.look?"":" Natural, Clear Day, Vibrant, Moody and Soft & warm cost about the same as High or Ultra.");
 pick_result=text;log(std::string("AUTO_PICK ")+text);pick.on=false;}
static const AnyMenuV4* buttons;
// Soft film and Soft & warm need AnyAPI 0.40.1; older APIs reject them, so they fall back to Clean and Vivid there.
static bool soft_grades=true;
static bool api_at_least(const char* version,int major,int minor,int patch){int a=0,b=0,c=0;if(sscanf_s(version,"%d.%d.%d",&a,&b,&c)<2)return false;return a!=major?a>major:b!=minor?b>minor:c>=patch;}
// Fine-tuning groups: each opens and closes from its header row. Open state lasts for the session.
static bool open_groups[GroupCount]{};
static bool group_header(int g){auto id="group."+std::to_string(g);bool open=open_groups[g];
 if(buttons){const char* text[]={open?"Hide":"Show"};return buttons->button_row(id.c_str(),group_names[g],text,1)==1;}
 uint32_t v=open;return widgets->toggle_row(id.c_str(),group_names[g],&v)&&bool(v)!=open;}
// A group's status line. While Gameplay only bypasses AnyGraphics in this menu it says so, and whether the pass ran in
// the last gameplay frames, instead of "waiting" (K34).
static void status_label(const char* id,bool live,bool seen,const char* name,const char* ready_text,const char* waiting){
 if(menu_bypass){std::string t=std::string(name)+(seen?": on in gameplay; Gameplay only bypasses it while menus are open.":": starts in gameplay; Gameplay only bypasses it while menus are open.");widgets->tabs->label(id,t.c_str());return;}
 widgets->tabs->label(id,live?ready_text:waiting);}
// Status lines that belong with a group, shown at its end while it is open.
static void group_labels(int g){
 auto draft=[](Index i){return state.settings[i].draft.number;};
 if(g==AntiAliasing){
 if(aa&&draft(NativeAA)>=3){AnySceneAntialiasingStatusV1 a;bool active=aa->status(&a)&&a.ready;auto choice=draft(NativeAA);status_label("aa_status",active,last_cost.aa,"Scene AA",choice>=6?"TAA extra smooth: scene only, before HUD.":choice>=5?"TAA: scene only, before HUD.":choice==4?"Enhanced SMAA: scene only, before HUD.":"SMAA: scene only, before HUD.","Scene AA is waiting for the scene renderer; native AA stays available.");}
 }
 else if(g==Lighting){
 if(lighting&&(draft(Atmosphere)>0||draft(SunShafts)>0||draft(LocalBeams)>0)){AnySceneLightingStatusV1 light;bool connected=lighting->status(&light)&&light.ready;status_label("lighting_status",connected,last_cost.lighting,"Volumetric lighting","Volumetric lighting: native scene depth and sun shadows.","Experimental lighting is waiting for a supported scene.");}
 // K25: sun shafts are the game's sun shadows seen in the air; with the game's shadows forced off there are none.
 if(lighting&&draft(SunShafts)>0&&draft(NativeShadows)==1)widgets->tabs->label("shafts_shadows","Sun shafts need the game's shadows: with Shadows off they only brighten the haze.");
 if(lighting_v2&&lamp_beams_blocked())widgets->tabs->label("lamp_beams_amd","Lamp beams are off on AMD graphics cards: they break the picture there.");
 else if(lighting_v2&&draft(LocalBeams)>0&&lamp_shadows_missing())widgets->tabs->label("lamp_shadows_amd","AMD graphics: lamp beams have no shadows yet, so they can shine through walls and vehicles.");
 }
 else if(g==Shadows){
 if(effects&&draft(DetailedAO)>0&&draft(NativeSSAO)!=1){AnySceneEffectsStatusV1 e;bool connected=effects->status(&e)&&e.ao_ready;status_label("ao_status",connected,last_cost.ao,"Detailed shading","Detailed shading: replaces the game's ambient occlusion before lighting.","Detailed shading is waiting for a supported scene; the game's ambient occlusion stays.");}
 }
 else if(g==Sky){
 if(effects_v2&&(draft(WaterReflections)>0||draft(SunGlint)>0)){AnySceneEffectsStatusV2 e;bool connected=effects_v2->status_v2(&e)&&e.water_ready;
  // Effects v3: a quiet frame (pass error -1: under water, in a bunker, no ocean drawn) is idle by design, not waiting.
  AnySceneEffectsStatusV3 s3;const bool idle=!connected&&effects_v3&&effects_v3->status_v3(&s3)&&s3.pass_error[ANY_PASS_WATER]==-1;
  status_label("water_status",connected,last_cost.water,"Water reflections and glint","Water reflections and glint: ocean only, before fog and sun beams.",idle?"Water: idle here (under water, in a bunker or no ocean in view).":"Water reflections are waiting for the ocean (it must be on in the game's settings).");}
 }
 else if(g==Finish){
 if(effects&&draft(ToneMapping)>0){AnySceneEffectsStatusV1 e;bool connected=effects->status(&e)&&e.ready;status_label("effects_status",connected,last_cost.effects,"Tone mapping and bloom","Tone mapping and bloom: HDR scene, before the game's tone mapping and HUD.","Scene effects are waiting for a supported scene.");}
 }
 else if(g==Camera){
  uint32_t p=photo;bool clicked=false;
  if(buttons){const char* text[]={photo?"Stop":"Start"};clicked=buttons->button_row("photo_mode","Photo mode (free camera, hide HUD)",text,1)==1;if(clicked)photo=!photo;}
  else if(widgets->toggle_row("photo_mode","Photo mode (free camera, hide HUD)",&p))photo=p;
  char key[32]="F1";if(controls&&photo_action)controls->key_name(photo_action,key,sizeof(key));
  std::string hint=std::string("Press ")+key+" in game to hide the HUD and fly the camera with WASD. The photo keys are listed in the corner. Menus always show.";widgets->tabs->label("photo_hint",hint.c_str());}
}
static std::string ms_text(float ms){if(!(ms>=0))return "off";char b[24];snprintf(b,sizeof(b),"%.2f ms",ms);return b;}
// The readout. Scene timing v2: GPU ms per pass, then the frame median and slowest 5%, AnyAPI's main-thread time, native
// reads, the overlay and the lamps. A pass AnyGraphics turned off reads "off" (K34). Each line stays under 400 bytes.
static void cost_lines(std::string& gpu,std::string& frame){gpu.clear();frame.clear();char b[160];
 if(timing_v2){AnySceneTimingV2 t;if(timing_v2->copy_v2(&t)){static const char* names[]={"shading","contact shadows","bounce light","water","lighting","sky","lens","effects","scene AA"};float total=0;gpu="GPU cost:";
  for(uint32_t i=0;i<=ANY_PASS_AA;++i){float ms=t.pass_ms[i];if((i==ANY_PASS_VOLUMETRIC&&!sent_lighting)||(i==ANY_PASS_AA&&!sent_aa))ms=-1;if(ms>0)total+=ms;gpu+=std::string(i?", ":" ")+names[i]+" "+ms_text(ms);}
  snprintf(b,sizeof(b)," (total %.2f ms)",total);gpu+=b;
  if(t.frame_ms_p50>0)snprintf(b,sizeof(b),"Frame: %.1f ms median, %.1f ms slowest 5%% (%.0f fps)",t.frame_ms_p50,t.frame_ms_p95,1000/t.frame_ms_p50);else snprintf(b,sizeof(b),"Frame: measuring");frame=b;
  if(t.anyapi_cpu_ms>=0){snprintf(b,sizeof(b)," | AnyAPI CPU %.2f ms",t.anyapi_cpu_ms);frame+=b;}
  snprintf(b,sizeof(b)," | native reads %u",t.native_reads);frame+=b;
  if(t.pass_ms[ANY_PASS_OVERLAY]>=0){snprintf(b,sizeof(b)," | overlay %.2f ms CPU",t.pass_ms[ANY_PASS_OVERLAY]);frame+=b;}
  if(lighting_v4&&sent_lighting&&sent_lamps){AnySceneLightingStatusV2 l;if(lighting_v4->status_v2(&l)){snprintf(b,sizeof(b)," | lamps %u of %u",l.lamps_drawn,unsigned(sent_lamps));frame+=b;
   if(l.amd_path_active){snprintf(b,sizeof(b)," (AMD path %u)",l.amd_path_active);frame+=b;}if(l.bypass_reason){snprintf(b,sizeof(b)," | lighting skipped (%u)",l.bypass_reason);frame+=b;}}}
  return;}}
 if(!timing)return;AnySceneTimingV1 t;if(!timing->copy(&t))return;char lit[24]="off",scene_aa[24]="off",fps[24]="";
 if(sent_lighting&&t.lighting_ms>=0)snprintf(lit,sizeof(lit),"%.2f ms",t.lighting_ms);if(sent_aa&&t.antialiasing_ms>=0)snprintf(scene_aa,sizeof(scene_aa),"%.2f ms",t.antialiasing_ms);if(t.frame_ms>0)snprintf(fps,sizeof(fps)," | %.0f fps",1000/t.frame_ms);
 char finish[24]="off",shade[24]="off",sky_cost[24]="off",contact_cost[24]="off",gi_cost[24]="off",lens_cost[24]="off",water_cost[40]="";AnySceneEffectsStatusV1 e;if(effects&&effects->status(&e)){if(e.ready&&e.gpu_ms>=0)snprintf(finish,sizeof(finish),"%.2f ms",e.gpu_ms);if(e.ao_ready&&e.ao_ms>=0)snprintf(shade,sizeof(shade),"%.2f ms",e.ao_ms);if(e.sky_ready&&e.sky_ms>=0)snprintf(sky_cost,sizeof(sky_cost),"%.2f ms",e.sky_ms);if(e.contact_ready&&e.contact_ms>=0)snprintf(contact_cost,sizeof(contact_cost),"%.2f ms",e.contact_ms);if(e.gi_ready&&e.gi_ms>=0)snprintf(gi_cost,sizeof(gi_cost),"%.2f ms",e.gi_ms);if(e.lens_ready&&e.lens_ms>=0)snprintf(lens_cost,sizeof(lens_cost),"%.2f ms",e.lens_ms);}
 AnySceneEffectsStatusV2 water;if(effects_v2&&effects_v2->status_v2(&water)){if(water.water_ready&&water.water_ms>=0)snprintf(water_cost,sizeof(water_cost),", water %.2f ms",water.water_ms);else snprintf(water_cost,sizeof(water_cost),", water off");}
 char text[400];snprintf(text,sizeof(text),"GPU cost: lighting %s, effects %s, shading %s, contact shadows %s, bounce light %s%s, sky %s, lens %s, scene AA %s%s",lit,finish,shade,contact_cost,gi_cost,water_cost,sky_cost,lens_cost,scene_aa,fps);gpu=text;}
// The GPU cost lines and save status, after everything else.
static void finish_draw(){
 if((timing||timing_v2)&&state.settings[CostReadout].draft.number==1){
  if(menu_bypass){if(last_cost.have){widgets->tabs->label("gpu_cost",("In gameplay: "+last_cost.gpu).c_str());if(!last_cost.frame.empty())widgets->tabs->label("frame_cost",("In gameplay: "+last_cost.frame).c_str());}
   else widgets->tabs->label("gpu_cost","GPU cost: play for a few seconds to measure it (Gameplay only bypasses AnyGraphics in menus).");}
  else{std::string gpu,frame;cost_lines(gpu,frame);if(!gpu.empty())widgets->tabs->label("gpu_cost",gpu.c_str());if(!frame.empty())widgets->tabs->label("frame_cost",frame.c_str());}}
 if(!state.status.empty())widgets->tabs->label("save_status",state.status.c_str());
}
static void draw(const AnyMenuFrameV1*,void*){std::lock_guard lock(mutex);
 AnySceneStatusV1 status;bool ready=scene&&scene->status(&status)&&status.ready;
 widgets->tabs->label("source",ready?"Game controls above; AnyGraphics overrides below (Modded).":"Native rendering connection unavailable; overrides are not active.");
 auto preset=int(state.settings[Preset].draft.number);bool custom=preset==CustomProfile;
 auto shown=[&](size_t i){return state.settings[i].draft.number>0;};
 bool lamps=lighting_v2&&!lamp_beams_blocked();auto beams_on=[&]{return lamps&&shown(LocalBeams);};

 // Which rows exist with the services and effects they need (any group, any preset).
 auto available=[&](size_t i){
  // Rows added in 0.30 need the matching API services and only show with the effect they tune.
  if(i==Sharpening&&(!aa_v2||state.settings[NativeAA].draft.number<3))return false;
  if((i==ShaftClarity||i==BeamReach||i==SunResponse)&&(!lighting_v3||!shown(SunShafts)))return false;
  if(i==TemporalLighting&&(!lighting_v3||(!shown(Atmosphere)&&!shown(SunShafts)&&!beams_on())))return false;
  if(i==CostReadout&&!timing&&!timing_v2)return false;
  // Light colour and wind (0.32) need scene controls v3.
  if(i==LightColour||i==WindStrength)return scene_v3!=nullptr;
  // Water reflections and sun glint (0.33) need scene effects v2.
  if(i==WaterReflections||i==SunGlint)return effects_v2!=nullptr;
  // 0.33.0 rows need AnyAPI 0.40.2: the moon in the game's light (scene controls v4) only with Moonlight on, the time of
  // day only with a Light colour, and the weather with any service that responds to it.
  if(i==MoonSource)return scene_v4&&effects&&shown(Moonlight);
  if(i==LightFollowsTime)return scene_v4&&shown(LightColour);
  if(i==WeatherResponse)return scene_v4||effects_v3||lighting_v4;
  // Finishing rows (0.31) need the scene effects service; the rest show only with tone mapping on.
  bool finishing=effects&&state.settings[ToneMapping].draft.number>0;
  // Detailed shading (0.31) replaces the game's SSAO output, so it needs SSAO not forced off.
  if(i==FocusBlur){if(!effects||state.settings[DepthOfField].draft.number==0)return false;}
  else if(i==DetailedAO){if(!effects||state.settings[NativeSSAO].draft.number==1)return false;}
  else if(i==NightSky||i==Moonlight||i==ContactShadows||i==BounceLight||i==DepthOfField||i==MotionBlur||i==DrawDistance||i==SunDisc||i==CloudLayer){if(!effects)return false;}
  else if(i>=ToneMapping&&(!effects||(i!=ToneMapping&&!finishing)||(i==LookStrength&&state.settings[ColourLook].draft.number==0)))return false;
  if(i==NativeBloom&&finishing)return false;
  if(i<ShaftClarity){if(i>=Clouds&&i<=Foliage&&!scene_v2)return false;if(i>=Atmosphere&&!lighting)return false;if(i>=LightingQuality&&i<LocalBeams&&state.settings[Atmosphere].draft.number==0&&state.settings[SunShafts].draft.number==0&&!beams_on())return false;if(i==ShaftIntensity&&state.settings[SunShafts].draft.number==0)return false;if(i==BeamFocus&&state.settings[SunShafts].draft.number==0&&!beams_on())return false;if(i>=LocalBeams&&!lamps)return false;if(i>LocalBeams&&state.settings[LocalBeams].draft.number==0)return false;if(i==AAQuality&&state.settings[NativeAA].draft.number<3)return false;}
  return true;};
 // Draws one row; returns true when the user changed it.
 auto draw_row=[&](size_t i)->bool{
  auto& row=state.settings[i];auto value=row.draft;auto id="setting."+std::to_string(i+1);uint32_t changed{};
  if(i==NativeBloom||i==FogDensity||i==SunLight||i==SkyLight||i==AmbientLight){
   // AG-9: the level that keeps the game's own value is called Game. The stored values are unchanged.
   static const char* fog_names[]={"Off","Light","Reduced","Game","Thick"},*light_names[]={"Off","Dim","Game","Bright","Brighter"};static const char* bloom[]={"Game setting","Off (override)","Low (override)","Medium (override)","High (override)","Ultra (override)"};
   uint32_t tier=i==NativeBloom?(value.number==0?0:value.number==1?1:2+nearest(float(state.settings[BloomAmount].draft.number),bloom_levels)):
    i==FogDensity?nearest(float(value.number),fog_levels):nearest(float(value.number),light_levels);
   changed=widgets->choice_row(id.c_str(),row.label.c_str(),i==NativeBloom?bloom:i==FogDensity?fog_names:light_names,i==NativeBloom?6:5,&tier);
   if(changed){if(i==NativeBloom){value.number=tier<2?tier:2;if(tier>=2){auto amount=state.settings[BloomAmount].draft;amount.number=bloom_levels[tier-2];state.stage(BloomAmount,amount);auto threshold=state.settings[BloomThreshold].draft;threshold.number=.75;state.stage(BloomThreshold,threshold);}}
    else value.number=i==FogDensity?fog_levels[tier]:light_levels[tier];}
  }
  else if(row.kind==ANY_SETTING_BOOL){uint32_t v=uint32_t(value.number);changed=widgets->toggle_row(id.c_str(),row.label.c_str(),&v);value.number=v;}
  else if(row.kind==ANY_SETTING_CHOICE){std::vector<const char*> choices;for(auto& c:row.choices)choices.push_back(c.c_str());uint32_t v=uint32_t(value.number);changed=widgets->choice_row(id.c_str(),row.label.c_str(),choices.data(),uint32_t(choices.size()),&v);value.number=v;}
  else changed=widgets->number_row(id.c_str(),row.label.c_str(),&value.number,row.minimum,row.maximum,row.step,0);
  if(changed){state.stage(i,std::move(value));if(i==Preset&&state.settings[Preset].draft.number!=0)stage_preset(int(state.settings[Preset].draft.number));
   // A preset chosen by hand ends auto-pick instead of being replaced by it (AG-13).
   if(i==Preset&&pick.on){pick.on=false;pick_result="Auto-pick stopped: you chose a preset.";}
   // Choosing Custom from the preset row opens every group, so all the settings are in view; any other preset closes them.
   if(i==Preset)std::fill(std::begin(open_groups),std::end(open_groups),state.settings[Preset].draft.number==CustomProfile);}
  return changed!=0;};

 // Top level: the preset and a few main choices. Off shows only the general rows.
 for(size_t i:order){if(group_of(Index(i))!=Top||hidden(Index(i)))continue;
  if(preset==0&&i!=Enabled&&i!=GameplayOnly&&i!=Preset&&i!=CostReadout)continue;
  if(!available(i))continue;draw_row(i);
  if(i==Preset){
   // One-shot actions: native buttons with menu v4, otherwise rows that act and switch straight back off.
   if(preset>0&&!custom){if(buttons){const char* text[]={"Reset"};if(buttons->button_row("reset_preset","Reset preset",text,1)==1)stage_preset(preset);}
    else{uint32_t reset=0;if(widgets->toggle_row("reset_preset","Reset preset",&reset)&&reset)stage_preset(preset);}}
   uint32_t want=pick.on;if(widgets->toggle_row("auto_pick","Auto-pick preset (measures frame rate)",&want)&&want!=uint32_t(pick.on)){
    if(!want){pick.on=false;pick_result="Auto-pick stopped.";}
    else if(state.settings[Enabled].committed.number<.5)pick_result="Auto-pick: turn on Enable AnyGraphics and select Apply first.";
    // Custom has no preset to come back to: ask once before replacing it (AG-6).
    else if(preset==CustomProfile&&!pick.confirm_custom){pick.confirm_custom=true;pick_result="Auto-pick replaces your Custom settings with a preset. Turn it on again to go ahead.";}
    else{auto from=preset>=1&&preset<=5?preset:preset==6||preset==VibrantProfile?5:4;pick=Pick{};pick.on=pick.commit_pending=true;pick.tier=pick.start=from;pick.look=is_look(preset)?preset:0;
     pick_result=pick.look?std::string("Auto-pick: close the menu and play; it keeps ")+look_name(preset)+" and measures a few seconds per cost level.":"Auto-pick: close the menu and play; it measures a few seconds per preset.";}}
   if(!pick_result.empty())widgets->tabs->label("auto_pick_status",pick_result.c_str());
   // AG-2: a fresh install starts on Balanced, which can be heavy for a laptop or integrated graphics.
   else if((preset>=1&&preset<=10)||preset==SoftWarmProfile)widgets->tabs->label("auto_pick_hint","On a laptop or integrated graphics, Auto-pick finds the preset your PC holds at about 60 fps.");
  }
 }
 if(preset==0)return finish_draw();
 // Fine tuning: groups that open and close. Changing a row in a group turns the preset into Custom, which keeps the
 // current values, so nothing else moves. The 0.33.0 behaviour rows are not preset values and keep the preset.
 // AG-14: the Lighting group shows without scene lighting too (Sunlight, Sky light, Ambient light, Light colour).
 for(int g=Lighting;g<GroupCount;++g){
  bool any=false;for(size_t i:order)if(group_of(Index(i))==g&&!hidden(Index(i))&&available(i)){any=true;break;}
  if(!any&&g!=Camera)continue;
  if(group_header(g))open_groups[g]=!open_groups[g];
  if(!open_groups[g])continue;
  for(size_t i:order){if(group_of(Index(i))!=g||hidden(Index(i))||!available(i))continue;
   if(draw_row(i)&&!preset_keeps(Index(i))&&state.settings[Preset].draft.number!=CustomProfile){auto kept=state.status;auto custom_value=state.settings[Preset].draft;custom_value.number=CustomProfile;state.stage(Preset,custom_value);state.status=kept;}}
  group_labels(g);
 }
 finish_draw();
}
static bool aa_v3_rejected,lighting_v4_rejected,effects_v3_rejected,controls_v4_rejected;
static void render(const AnyFrameV1* frame,AnyCanvasV1*,void*){
 if(state.settings.size()!=Count)return;read();if(!scene)return;bool on=values[Enabled]>.5f&&values[Preset]!=0;
 AnyUiSnapshotV1 snapshot;bool gameplay=ui&&ui->copy(&snapshot)&&snapshot.kind==ANY_UI_GAMEPLAY;const bool enabled=on;if(on&&values[GameplayOnly]>.5f)on=gameplay;
 menu_bypass=enabled&&!on;
 const auto now=frame?frame->tick:GetTickCount64();
 // Photo mode key: toggles only in gameplay with the game focused; the HUD comes back in every menu. The input callback
 // latches each press (SC-18), so a tap between two slow frames still counts; polling is the fallback for a host that
 // sends no key events (CPU-14: no GetAsyncKeyState per frame otherwise).
 bool focused=!frame||frame->focused;auto key=photo_key();photocam::toggle_key=key;bool pressed=photocam::toggle_requested.exchange(false);
 if(!photocam::key_events){bool down=key&&focused&&(GetAsyncKeyState(int(key))&0x8000);pressed=down&&!photo_key_down;photo_key_down=down;}
 if(pressed&&gameplay&&focused&&now-photo_toggled_at>=150){photo_toggled_at=now;photo=!photo;log(photo?"PHOTO_MODE on=1":"PHOTO_MODE on=0");}
 bool hide=photo&&gameplay&&effects;photocam::update(photo,gameplay);static uint32_t photo_revision{};auto tweaks=photocam::revision.load();
 AnySceneStatusV1 status;bool now_ready=scene->status(&status)&&status.ready;
 if((!configured&&now>=retry_at)||on!=last_on||now_ready!=native_ready||hide!=last_hide||(hide&&tweaks!=photo_revision)){photo_revision=tweaks;bool all_accepted=true;auto v=effective(values);AnySceneParametersV1 p;
  p.enabled=on;p.aa=v[NativeAA]>=3?0:uint32_t(v[NativeAA]);p.bloom=uint32_t(v[NativeBloom]);p.ssao=uint32_t(v[NativeSSAO]);p.shadows=uint32_t(v[NativeShadows]);p.fog_blur=uint32_t(v[NativeFogBlur]);
  p.bloom_threshold=v[BloomThreshold];p.bloom_intensity=v[BloomAmount];p.sun=v[SunLight];p.sky=v[SkyLight];p.ambient=v[AmbientLight];p.fog=v[FogDensity];p.light_exposure=v[SceneExposure];
  // Moonlight source Game light (K49): the moon lights the scene through the game's night light (scene controls v4), so
  // the screen-space moonlight is off and the moon disc follows that light (scene effects v3).
  const bool moon_game=scene_v4&&v[MoonSource]>0&&v[Moonlight]>0,weather=v[WeatherResponse]>0;
  const bool aa_on=on&&v[NativeAA]>=3;sent_aa=aa_on;
  // Scene AA v3 (K47): Extra smooth adds one TAA history level and keeps the chosen quality; v2 raises the quality instead.
  if(aa_v3){AnySceneAntialiasingParametersV3 a;a.base.enabled=aa_on;a.base.method=aa_method(v[NativeAA]);a.base.quality=uint32_t(v[AAQuality]);a.base.sharpening=sharpen_levels[size_t(v[Sharpening])];a.history_bias=aa_history_bias(v[NativeAA]);
   a.taa_flags=(v[TaaStillV2]>0?ANY_TAA_CATMULL_ROM|ANY_TAA_STILL_V2:0u)|(v[TaaThinRescue]>0?ANY_TAA_THIN_RESCUE:0u);
   // Hidden testing rows: AA after the overlays, TAA ocean on the depth buffer, no output dither.
   a.flags=(v[AaBeforeOverlays]>0?0u:ANY_AA_AFTER_OVERLAYS)|(v[TaaOcean]>0?0u:ANY_AA_OCEAN_DEPTH);a.dither=v[OutputDither]>0?1u:0u;
   bool accepted=aa_v3->set(&a);if(!accepted){log_once(aa_v3_rejected,"SCENE_AA_UPDATE v3_rejected=1 fallback=v2");a.base.quality=aa_quality(v[NativeAA],v[AAQuality]);accepted=aa_v2->set(&a.base);}
   if(!accepted){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  else if(aa_v2){AnySceneAntialiasingParametersV2 a;a.enabled=aa_on;a.method=aa_method(v[NativeAA]);a.quality=aa_quality(v[NativeAA],v[AAQuality]);a.sharpening=sharpen_levels[size_t(v[Sharpening])];if(!aa_v2->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  // Older APIs have no TAA: fall back to SMAA rather than turning AA off.
  else if(aa){AnySceneAntialiasingParametersV1 a;a.enabled=aa_on;a.method=std::min(aa_method(v[NativeAA]),2u);a.quality=uint32_t(v[AAQuality]);if(!aa->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  if(lighting){AnySceneLightingParametersV1 light;light.enabled=on&&(v[Atmosphere]>0||v[SunShafts]>0);light.quality=uint32_t(v[LightingQuality]);static const float density[]={0,.001f,.002f,.004f,.008f},strength[]={0,.5f,1,2,3},height[]={0,.01f,.025f,.06f};light.fog_density=v[Atmosphere]>0?density[size_t(v[Atmosphere])]:.002f;light.sun_shafts=strength[size_t(v[SunShafts])]*v[ShaftIntensity];static const float focus[]={.2f,.35f,.55f,.7f};light.anisotropy=focus[size_t(v[BeamFocus])];light.enabled=on&&(v[Atmosphere]>0||light.sun_shafts>0);light.fog_strength=v[Atmosphere]>0?1.f:0.f;light.height_falloff=height[size_t(v[GroundFog])];bool accepted,lit=light.enabled;uint32_t lamps_sent=0;
   if(lighting_v2){AnySceneLightingParametersV2 local;local.scene=light;local.local_strength=lamp_beams_blocked()?0.f:strength[size_t(v[LocalBeams])]*v[LocalIntensity];static const uint32_t budgets[]={2,4,6,8};local.local_budget=budgets[size_t(v[LocalBudget])];local.scene.enabled=on&&(light.enabled||local.local_strength>0);lit=local.scene.enabled;if(lit&&local.local_strength>0)lamps_sent=local.local_budget;
    if(lighting_v3){AnySceneLightingParametersV3 shaped;shaped.lighting=local;shaped.clarity=clarity_levels[size_t(v[ShaftClarity])];shaped.beam_reach=reach_levels[size_t(v[BeamReach])];shaped.sun_response=uint32_t(v[SunResponse]);shaped.temporal=uint32_t(v[TemporalLighting]);
     // Lighting v4: the default lamp flags (tracking, fades, underground lamps, ocean clamp), the sea plane always,
     // weather density and lightning glow with Weather response, and the sun phase cap with Classic shaft clarity.
     if(lighting_v4){AnySceneLightingParametersV4 next;next.shaped=shaped;next.lamp_budget=std::min(local.local_budget,lamp_budget_max);next.weather_response=weather?1.f:0.f;next.amd_path=uint32_t(v[AmdLampPath]);
      next.atmosphere_flags=ANY_ATMOS_SEA_PLANE|(weather?ANY_ATMOS_WEATHER_DENSITY|ANY_ATMOS_FLASH_GLOW:0u)|(v[ShaftClarity]>=3?ANY_ATMOS_PHASE_CAP:0u);if(lamps_sent)lamps_sent=next.lamp_budget;
      accepted=lighting_v4->set(&next);if(!accepted){log_once(lighting_v4_rejected,"SCENE_LIGHTING_UPDATE v4_rejected=1 fallback=v3");accepted=lighting_v3->set(&shaped);}}
     else accepted=lighting_v3->set(&shaped);}
    else accepted=lighting_v2->set(&local);}else accepted=lighting->set(&light);if(!accepted){all_accepted=false;log("SCENE_LIGHTING_UPDATE rejected=1 retry_pending=1");}
   sent_lighting=accepted&&lit;sent_lamps=accepted?lamps_sent:0;}
  if(effects){AnySceneEffectsParametersV1 e;bool shading=v[DetailedAO]>0&&v[NativeSSAO]!=1,sky=v[NightSky]>0||(v[Moonlight]>0&&!moon_game),contact=v[ContactShadows]>0,bounce=v[BounceLight]>0,lens=v[DepthOfField]>0||v[MotionBlur]>0,reach=v[DrawDistance]>0,heavens=v[SunDisc]>0||v[CloudLayer]>0,water=effects_v2&&(v[WaterReflections]>0||v[SunGlint]>0);e.enabled=on&&(v[ToneMapping]>0||shading||sky||contact||bounce||lens||reach||heavens||water);e.hide_ui=hide;e.sun_disc=sun_disc_levels[size_t(v[SunDisc])];e.clouds=cloud_levels[size_t(v[CloudLayer])];e.draw_distance=draw_distance_levels[size_t(v[DrawDistance])];e.bounce_light=bounce_levels[size_t(v[BounceLight])];e.depth_of_field=std::min(focus_blur_levels[size_t(v[DepthOfField])]*std::clamp(v[FocusBlur],.25f,3.f),4.f);e.motion_blur=motion_blur_levels[size_t(v[MotionBlur])];e.contact_shadows=contact;e.contact_length=contact_levels[size_t(v[ContactShadows])];e.night_sky=sky;e.stars=star_levels[size_t(v[NightSky])];e.moon=moon_levels[size_t(v[NightSky])];e.moonlight=moon_game?0.f:moonlight_levels[size_t(v[Moonlight])];e.finish=v[ToneMapping]>0;e.ambient_occlusion=shading;e.ao_radius=ao_radius_levels[size_t(v[DetailedAO])];e.ao_strength=ao_strength_levels[size_t(v[DetailedAO])];e.tonemap=v[ToneMapping]>=4?(soft_grades?3:2):v[ToneMapping]>=3?2:v[ToneMapping]>=2?1:0;e.look=uint32_t(v[ColourLook]>=6&&!soft_grades?5:v[ColourLook]);e.look_strength=look_strength_levels[size_t(v[LookStrength])];e.eye_adaptation=uint32_t(v[EyeAdaptation]);e.exposure=v[Brightness];e.bloom=scene_bloom_levels[size_t(v[SceneBloom])];e.glare=glare_levels[size_t(v[SunGlare])];e.vignette=vignette_levels[size_t(v[Vignette])];
   // Photo mode still works with AnyGraphics off: everything else stays off.
   if(hide&&!e.enabled){e=AnySceneEffectsParametersV1{};e.enabled=1;e.finish=0;e.ambient_occlusion=0;e.night_sky=0;e.contact_shadows=0;e.bounce_light=0;e.hide_ui=1;}
   // K31: a photo tweak that turns the finish pass on replaces the game's bloom, so it brings a low bloom only where
   // the game's bloom was on, and no sun glare.
   if(hide)photocam::apply(e,!on||v[NativeBloom]!=1);
   // Low, Medium and High water reflections are ray qualities 0-2 at full strength.
   bool accepted;AnySceneEffectsParametersV2 wet;wet.effects=e;wet.water_reflections=on&&v[WaterReflections]>0?1.f:0.f;wet.sun_glint=on&&v[SunGlint]>0?1.f:0.f;wet.reflection_quality=v[WaterReflections]>0?uint32_t(v[WaterReflections])-1:1;
   // Scene effects v3: clouds v2, the shading revision, the night sky on the game clock and the dither are on; the hidden rows revert them.
   if(effects_v3){AnySceneEffectsParametersV3 next;next.water=wet;next.sky_model=uint32_t(v[SkyModel]);next.dither=v[OutputDither]>0?1u:0u;next.cloud_model=uint32_t(v[CloudModel]);next.weather=weather;next.moon_disc_follows_light=moon_game;next.shading_revision=uint32_t(v[ShadingRevision]);next.tone_revision=uint32_t(v[ToneRevision]);next.bloom_falloff=v[BloomFalloff];next.flags=v[KeepNativeSsao]>0?ANY_FX_KEEP_NATIVE_SSAO:0u;
    accepted=effects_v3->set(&next);if(!accepted){log_once(effects_v3_rejected,"SCENE_EFFECTS_UPDATE v3_rejected=1 fallback=v2");accepted=effects_v2->set(&wet);}}
   else if(effects_v2)accepted=effects_v2->set(&wet);else accepted=effects->set(&e);
   if(!accepted){all_accepted=false;log("SCENE_EFFECTS_UPDATE rejected=1 retry_pending=1");}}
  bool accepted=false;if(scene_v3){AnySceneParametersV3 looked;looked.detailed.scene=p;looked.detailed.clouds=uint32_t(v[Clouds]);looked.detailed.grass=uint32_t(v[Grass]);looked.detailed.foliage=uint32_t(v[Foliage]);auto colour=size_t(v[LightColour]);for(int c=0;c<3;++c){looked.key_tint[c]=key_tints[colour][c];looked.shade_tint[c]=shade_tints[colour][c];}looked.wind_scale=wind_levels[size_t(v[WindStrength])];
   // Scene controls v4 (K17, K49, K18): the tints land on the sun and sky lights, the time-of-day palettes, the moon as
   // the game's night light, and storms keeping the game's fog.
   if(scene_v4){AnySceneParametersV4 next;next.looked=looked;next.tint_targets=1;next.time_shaped=v[LightFollowsTime]>0&&v[LightColour]>0;
    float* palettes[PaletteCount]={next.noon_key,next.noon_shade,next.golden_key,next.golden_shade,next.blue_key,next.blue_shade,next.moon_key,next.moon_shade};
    for(uint32_t q=0;q<PaletteCount;++q)for(int c=0;c<3;++c)palettes[q][c]=palette(Palette(q),c,v[LightColour]);
    next.moon_mode=moon_game?1:0;next.moonlight=moon_game?moonlight_levels[size_t(v[Moonlight])]*moon_target:0.f;next.weather_fog_keep=weather;
    accepted=scene_v4->set(&next);if(!accepted){log_once(controls_v4_rejected,"NATIVE_SCENE_UPDATE v4_rejected=1 fallback=v3");accepted=scene_v3->set(&looked);}}
   else accepted=scene_v3->set(&looked);}
  else if(scene_v2){AnySceneParametersV2 detailed;detailed.scene=p;detailed.clouds=uint32_t(v[Clouds]);detailed.grass=uint32_t(v[Grass]);detailed.foliage=uint32_t(v[Foliage]);accepted=scene_v2->set(&detailed);}else accepted=scene->set(&p);
  if(!accepted){all_accepted=false;log("NATIVE_SCENE_UPDATE rejected=1 retry_pending=1");}configured=all_accepted;retry_at=all_accepted?0:now+500;last_on=on;last_hide=hide;native_ready=now_ready;
 }
 auto_pick(gameplay&&focused,on&&now_ready,configured);
 // K34: keep the gameplay readout and which passes ran, for the menus where Gameplay only bypasses AnyGraphics.
 if(gameplay&&on&&now_ready&&now-last_cost_at>=500){last_cost_at=now;LastCost c;c.have=true;cost_lines(c.gpu,c.frame);
  AnySceneAntialiasingStatusV1 a;c.aa=aa&&aa->status(&a)&&a.ready;AnySceneLightingStatusV1 l;c.lighting=lighting&&lighting->status(&l)&&l.ready;
  AnySceneEffectsStatusV1 e;if(effects&&effects->status(&e)){c.ao=e.ao_ready;c.effects=e.ready;}AnySceneEffectsStatusV2 w;c.water=effects_v2&&effects_v2->status_v2(&w)&&w.water_ready;
  std::lock_guard lock(mutex);last_cost=std::move(c);}
 if(frame&&frame->tick-status_at>=10000){status_at=frame->tick;if(now_ready)log("NATIVE_SCENE ready=1 renderer_calls="+std::to_string(status.renderer_calls)+" scene_calls="+std::to_string(status.scene_calls)+" rejected="+std::to_string(status.rejected_frames));else log("NATIVE_SCENE ready=0 overrides_not_active=1");}
}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){
 if(!host||!callbacks||host->abi!=ANYAPI_MOD_ABI||host->struct_size!=sizeof(*host)||!host->log)return false;graphics::host=*host;callbacks->id="anygraphics";callbacks->render=graphics::render;graphics::photocam::host=*host;callbacks->input=[](const AnyInputV1* e,void*)->uint32_t{return graphics::photocam::input(e);};callbacks->shutdown=[](void*){graphics::photocam::shutdown();};graphics::log("DLL_READY settings="+std::to_string(graphics::Count)+" presets=13 native_only=1 post_fx_passes=0");return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace graphics;auto services=AnyAPI_Services();if(!services)return;photocam::ready(services);
 scene=(const AnySceneControlsV1*)services->query("anyapi.scene_controls",1);if(scene&&(scene->version!=1||scene->struct_size!=sizeof(*scene)||!scene->set||!scene->status))scene=nullptr;
 scene_v3=(const AnySceneControlsV3*)services->query("anyapi.scene_controls",3);if(scene_v3&&(scene_v3->version!=3||scene_v3->struct_size!=sizeof(*scene_v3)||!scene_v3->set||!scene_v3->status))scene_v3=nullptr;
 scene_v4=(const AnySceneControlsV4*)services->query("anyapi.scene_controls",4);if(scene_v4&&(scene_v4->version!=4||scene_v4->struct_size!=sizeof(*scene_v4)||!scene_v4->set||!scene_v4->status||!scene_v4->status_v2||!scene_v3))scene_v4=nullptr;
 scene_v2=(const AnySceneControlsV2*)services->query("anyapi.scene_controls",2);if(scene_v2&&(scene_v2->version!=2||scene_v2->struct_size!=sizeof(*scene_v2)||!scene_v2->set||!scene_v2->status))scene_v2=nullptr;
 log(scene?"NATIVE_SCENE service_available=1":"NATIVE_SCENE unavailable requires_API_28=1 overrides_disabled=1");
 aa=(const AnySceneAntialiasingV1*)services->query("anyapi.scene_antialiasing",1);if(aa&&(aa->struct_size!=sizeof(*aa)||aa->version!=1||!aa->set||!aa->status))aa=nullptr;
 aa_v2=(const AnySceneAntialiasingV2*)services->query("anyapi.scene_antialiasing",2);if(aa_v2&&(aa_v2->struct_size!=sizeof(*aa_v2)||aa_v2->version!=2||!aa_v2->set||!aa_v2->status))aa_v2=nullptr;
 aa_v3=(const AnySceneAntialiasingV3*)services->query("anyapi.scene_antialiasing",3);if(aa_v3&&(aa_v3->struct_size!=sizeof(*aa_v3)||aa_v3->version!=3||!aa_v3->set||!aa_v3->status||!aa_v3->status_v2||!aa_v2))aa_v3=nullptr;
 timing=(const AnySceneTimingServiceV1*)services->query("anyapi.scene_timing",1);if(timing&&(timing->struct_size!=sizeof(*timing)||timing->version!=1||!timing->copy))timing=nullptr;
 timing_v2=(const AnySceneTimingServiceV2*)services->query("anyapi.scene_timing",2);if(timing_v2&&(timing_v2->struct_size!=sizeof(*timing_v2)||timing_v2->version!=2||!timing_v2->copy||!timing_v2->copy_v2))timing_v2=nullptr;
 gpu_info=(const AnyGpuInfoServiceV1*)services->query("anyapi.gpu_info",1);if(gpu_info&&(gpu_info->struct_size!=sizeof(*gpu_info)||gpu_info->version!=1||!gpu_info->copy))gpu_info=nullptr;
 gpu_info_v2=(const AnyGpuInfoServiceV2*)services->query("anyapi.gpu_info",2);if(gpu_info_v2&&(gpu_info_v2->struct_size!=sizeof(*gpu_info_v2)||gpu_info_v2->version!=2||!gpu_info_v2->copy||!gpu_info_v2->copy_v2))gpu_info_v2=nullptr;
 effects=(const AnySceneEffectsV1*)services->query("anyapi.scene_effects",1);if(effects&&(effects->struct_size!=sizeof(*effects)||effects->version!=1||!effects->set||!effects->status))effects=nullptr;
 effects_v2=(const AnySceneEffectsV2*)services->query("anyapi.scene_effects",2);if(effects_v2&&(effects_v2->struct_size!=sizeof(*effects_v2)||effects_v2->version!=2||!effects_v2->set||!effects_v2->status||!effects_v2->status_v2||!effects))effects_v2=nullptr;
 effects_v3=(const AnySceneEffectsV3*)services->query("anyapi.scene_effects",3);if(effects_v3&&(effects_v3->struct_size!=sizeof(*effects_v3)||effects_v3->version!=3||!effects_v3->set||!effects_v3->status||!effects_v3->status_v2||!effects_v3->status_v3||!effects_v2))effects_v3=nullptr;
 lighting=(const AnySceneLightingV1*)services->query("anyapi.scene_lighting",1);if(lighting&&(lighting->struct_size!=sizeof(*lighting)||lighting->version!=1||!lighting->set||!lighting->status))lighting=nullptr;
 lighting_v2=(const AnySceneLightingV2*)services->query("anyapi.scene_lighting",2);if(lighting_v2&&(lighting_v2->struct_size!=sizeof(*lighting_v2)||lighting_v2->version!=2||!lighting_v2->set||!lighting_v2->status))lighting_v2=nullptr;
 lighting_v3=(const AnySceneLightingV3*)services->query("anyapi.scene_lighting",3);if(lighting_v3&&(lighting_v3->struct_size!=sizeof(*lighting_v3)||lighting_v3->version!=3||!lighting_v3->set||!lighting_v3->status||!lighting_v2))lighting_v3=nullptr;
 lighting_v4=(const AnySceneLightingV4*)services->query("anyapi.scene_lighting",4);if(lighting_v4&&(lighting_v4->struct_size!=sizeof(*lighting_v4)||lighting_v4->version!=4||!lighting_v4->set||!lighting_v4->status||!lighting_v4->status_v2||!lighting_v3))lighting_v4=nullptr;
 if(lighting_v4){AnySceneLightingStatusV2 l;if(lighting_v4->status_v2(&l)&&l.lamp_budget_max)lamp_budget_max=l.lamp_budget_max;}
 log("SERVICES controls="+std::to_string(scene_v4?4:scene_v3?3:scene_v2?2:scene?1:0)+" lighting="+std::to_string(lighting_v4?4:lighting_v3?3:lighting_v2?2:lighting?1:0)+" effects="+std::to_string(effects_v3?3:effects_v2?2:effects?1:0)+" aa="+std::to_string(aa_v3?3:aa_v2?2:aa?1:0)+" timing="+std::to_string(timing_v2?2:timing?1:0)+" gpu_info="+std::to_string(gpu_info_v2?2:gpu_info?1:0)+" lamp_budget_max="+std::to_string(lamp_budget_max));
 ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui&&(ui->version!=1||ui->struct_size!=sizeof(*ui)||!ui->copy))ui=nullptr;
 controls=(const ModControlsV1*)services->query("anyhelpers.controls",1);if(controls&&controls->struct_size==sizeof(*controls)&&controls->version==1&&controls->register_action&&controls->key&&controls->key_name){ModControlActionV1 d;d.mod_id="anygraphics";d.mod_name="AnyGraphics";d.action_id="photo_mode";d.label="Photo mode (free camera, hide HUD)";d.default_key=VK_F1;photo_action=controls->register_action(&d);}else controls=nullptr;
 menu=(const AnyMenuV1*)services->query("anyapi.menu",1);widgets=(const AnyMenuV3*)services->query("anyapi.menu",3);
 if(auto build=(const AnyBuildV1*)services->query("anyapi.build",1);build&&build->struct_size==sizeof(*build)&&build->version==1&&build->copy){AnyBuildInfoV1 info;if(build->copy(&info))soft_grades=api_at_least(info.api_version,0,40,1);}
 photocam::soft_grades=soft_grades;
 buttons=(const AnyMenuV4*)services->query("anyapi.menu",4);if(buttons&&(buttons->version!=4||buttons->struct_size!=sizeof(*buttons)||!buttons->button_row))buttons=nullptr;
 if(!menu||menu->version!=1||menu->struct_size!=sizeof(*menu)||!menu->add_section||!widgets||widgets->version!=3||widgets->struct_size!=sizeof(*widgets)){log("GRAPHICS_MENU unavailable=1");return;}
 auto own_file=std::filesystem::path(host.plugin_directory)/L"AnyGraphics"/L"settings.tsv";
 std::error_code missing;state.file=std::filesystem::exists(own_file,missing)?own_file:std::filesystem::path(host.plugin_directory)/L"AnyHelpers"/L"settings.tsv";state.load();bool imported=state.file!=own_file;state.file=own_file;
 // Keep only retained native controls when importing the old shared settings file.
 std::map<std::string,helpersettings::Saved> retained;
 for(auto& o:definitions){std::string group=o.group;std::transform(group.begin(),group.end(),group.begin(),[](unsigned char c){return char(std::tolower(c));});auto key="anygraphics."+group+"\t"+o.id;auto old=state.saved.find(key);if(old!=state.saved.end())retained.emplace(*old);}
 if(imported){auto b=retained.find("anygraphics.scene\tnative_bloom"),enabled=state.saved.find("anygraphics.bloom\tbloom");if(b!=retained.end()&&b->second.value.number==2&&enabled!=state.saved.end()&&enabled->second.value.number==0)b->second.value.number=1;}
 state.saved=std::move(retained);
 // K37: settings saved before preset_revision existed come from the old preset tables.
 const bool had_settings=!state.saved.empty(),had_revision=state.saved.count("anygraphics.hidden\tpreset_revision")>0;
 // AG-16: without the V2 AA service the TAA choices are not offered. A saved TAA becomes Enhanced SMAA (FXAA without scene
 // AA) here, before add() would reject it back to the default.
 const uint32_t aa_choices=graphics::aa_v2?7:graphics::aa?5:3;
 if(auto r=state.saved.find("anygraphics.scene\tnative_aa");r!=state.saved.end()&&r->second.value.number>=aa_choices)r->second.value.number=fit(NativeAA,float(r->second.value.number),aa_choices);
 // AG-2: a fresh install and Reset get the Balanced preset's values, not each row's own default.
 const auto base=balanced();
 static std::array<std::string,Count> ids;
 static const char* choices[]={"Game setting","Off (override)","On (override)"},*aa[]={"Game setting","Off (override)","FXAA (override)","SMAA 1x (scene)","Enhanced SMAA (scene)","TAA (scene)","TAA extra smooth (scene)"},*aa_quality[]={"Low","Medium","High","Ultra"},*bloom[]={"Game setting","Off (override)","Custom (override)"},*looks[]={"Off","Performance","Low","Balanced","High","Ultra","Cinematic","Natural","Vibrant","Moody","Clear Day","Custom","Soft & warm"},*haze[]={"Clear","Light","Natural","Classic (original)"},*reach[]={"Near","Medium","Far","Unlimited"},*sharpness[]={"Off","Low","Medium","High"},*strengths[]={"Off","Low","Medium","High","Ultra"},*falloff[]={"Off","Low","Medium","High"},*focus[]={"Soft","Balanced","Focused","Strong"},*budgets[]={"2 lights","4 lights","6 lights","8 lights"},*tones[]={"Off (game look)","Game curve","Filmic","Clean","Soft film"},*grades[]={"None","Warm","Cool","Teal & orange","Moody","Vivid","Soft & warm"},*shading[]={"Off","Low","Medium","High"},*nights[]={"Off","Subtle","Normal","Bright"},*distances[]={"Game setting","Far (1.5x)","Very far (2x)","Extreme (3x)"},*suns[]={"Off","Soft","Normal","Bright"},*cloud_amounts[]={"Off","Few","Some","Many"},*amounts[]={"Subtle","Normal","Strong"},*glows[]={"Off","Subtle","Normal","Strong"},*corners[]={"Off","Light","Medium","Strong"},*light_colours[]={"Game","Subtle","Normal","Strong"},*winds[]={"Calm","Game","Breezy","Stormy"},*switches[]={"Off","On"},*moon_sources[]={"Sky glow","Game light"},*amd_paths[]={"Automatic","Constant buffer","Uniform branch","Single 8-lamp shader"};
 for(size_t i=0;i<Count;++i){auto& o=definitions[i];ids[i]="anygraphics."+std::string(o.group);std::transform(ids[i].begin(),ids[i].end(),ids[i].begin(),[](unsigned char c){return char(std::tolower(c));});AnyModSettingV1 d;d.mod_id=ids[i].c_str();d.mod_name="AnyGraphics";d.setting_id=o.id;d.label=o.label;d.description=o.description;d.order=int32_t(i);d.kind=o.kind;d.default_number=o.initial;d.minimum=o.minimum;d.maximum=o.maximum;d.step=o.step;
  if(d.kind==ANY_SETTING_CHOICE){d.choices=i==MoonSource?moon_sources:i==AmdLampPath?amd_paths:i==SunGlint?switches:i==WaterReflections?shading:i==LightColour?light_colours:i==WindStrength?winds:i==SunDisc?suns:i==CloudLayer?cloud_amounts:i==DrawDistance?distances:i==NightSky?nights:i==Moonlight||i==ContactShadows||i==BounceLight||i==DepthOfField||i==MotionBlur?shading:i==DetailedAO?shading:i==ToneMapping?tones:i==ColourLook?grades:i==LookStrength?amounts:i==SceneBloom||i==SunGlare?glows:i==Vignette?corners:i==ShaftClarity?haze:i==BeamReach?reach:i==Sharpening?sharpness:i==LocalBeams?strengths:i==LocalBudget?budgets:i==BeamFocus?focus:i==Atmosphere||i==SunShafts?strengths:i==LightingQuality?aa_quality:i==GroundFog?falloff:i==Preset?looks:i==NativeAA?aa:i==NativeBloom?bloom:i==AAQuality?aa_quality:choices;d.choice_count=i==MoonSource?2:i==AmdLampPath?4:i==SunGlint?2:i==WaterReflections||i==LightColour||i==WindStrength||i==SunDisc||i==CloudLayer||i==DrawDistance||i==DepthOfField||i==MotionBlur||i==BounceLight||i==ContactShadows||i==NightSky||i==Moonlight||i==DetailedAO||i==SceneBloom||i==SunGlare||i==Vignette?4:i==ToneMapping?5:i==ColourLook?7:i==LookStrength?3:i==ShaftClarity||i==BeamReach||i==Sharpening?4:i==LocalBeams?5:i==LocalBudget?4:i==BeamFocus?4:i==Atmosphere||i==SunShafts?5:i==LightingQuality||i==GroundFog?4:i==Preset?13:i==AAQuality?4:i==NativeAA?aa_choices:3;}
  d.default_number=fit(i,base[i],d.kind==ANY_SETTING_CHOICE?d.choice_count:0);tokens[i]=state.add(&d);
 }
 if(state.settings.size()!=Count){log("GRAPHICS_MENU registration_failed=1");return;}
 // Migrate numeric tuning to the nearest named strength without changing native on/off choices.
 for(auto i:{SunLight,SkyLight,AmbientLight,FogDensity}){auto& r=state.settings[i];r.committed.number=i==FogDensity?fog_levels[nearest(float(r.committed.number),fog_levels)]:light_levels[nearest(float(r.committed.number),light_levels)];r.draft=r.committed;}
 auto& amount=state.settings[BloomAmount];amount.committed.number=bloom_levels[nearest(float(amount.committed.number),bloom_levels)];amount.draft=amount.committed;
 state.settings[SceneExposure].committed.number=0;state.settings[SceneExposure].draft=state.settings[SceneExposure].committed;
 // K37: bring a committed preset (1-10, 12) to the current tables once. Only the rows the preset owns whose table changed
 // move; Custom and the top-level rows stay. A save failure keeps the old values and tries again next launch.
 if(const int from=had_revision?int(state.settings[PresetRevision].committed.number):had_settings?0:preset_revision;from<preset_revision){
  Values c{};for(size_t j=0;j<Count;++j)c[j]=float(state.settings[j].committed.number);auto next=c;const auto rows=restage(next,from);next[PresetRevision]=float(preset_revision);
  state.begin();for(size_t j=0;j<Count;++j)if(next[j]!=c[j]){auto value=state.settings[j].draft;value.number=fit(j,next[j]);state.stage(j,value);}
  const bool saved=state.apply();if(!saved)state.begin();state.status.clear();
  log("PRESET_REVISION from="+std::to_string(from)+" to="+std::to_string(preset_revision)+" preset="+std::to_string(int(c[Preset]))+" rows_changed="+std::to_string(rows)+" saved="+std::to_string(saved));}
 AnyMenuSectionV1 section;section.id="anygraphics";section.title="AnyGraphics · Modded";section.location=ANY_MENU_SETTINGS_GRAPHICS;section.order=100;section.draw=draw;section.event=event;section.dirty=dirty;section.defaults=defaults;
 if(!menu->add_section(&section)){log("GRAPHICS_MENU registration_failed=1 requires_API_28=1");return;}
 // OVL-9: the section is registered, but the game's Settings screen shows it only once AnyAPI's menu bridge is in place.
 if(menu->available&&!menu->available())log("GRAPHICS_MENU bridge_unready=1 (the section appears once AnyAPI finds the game's Settings screen)");
 log("SETTINGS_REGISTERED count="+std::to_string(Count)+" location=settings.graphics native_only=1 post_fx_passes=0 imported="+std::to_string(imported));
}
