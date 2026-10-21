#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_controls_v3.h"
#include "anyapi_scene_lighting_v3.h"
#include "anyapi_scene_antialiasing_v2.h"
#include "anyapi_scene_timing_v1.h"
#include "anyapi_gpu_info_v1.h"
#include "anyapi_scene_effects_v1.h"
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
// AnyAPI 0.39.1 switches lamp beams off on AMD GPUs (they corrupt the picture there); hide their rows too.
static bool lamp_beams_blocked(){AnyGpuInfoV1 g;return gpu_info&&gpu_info->copy(&g)&&g.known&&!g.lamp_beams_supported;}
static const AnySceneEffectsV1* effects;
static helpersettings::State state;static std::mutex mutex;
static const AnyMenuV1* menu;static const AnyMenuV3* widgets;
static const AnyUiStateV1* ui;
// Photo mode and auto-pick are per-session: neither is saved with the settings.
static const ModControlsV1* controls;static uint64_t photo_action;static bool photo{},photo_key_down{},last_hide{};
static uint32_t photo_key(){return controls&&photo_action?controls->key(photo_action):VK_F1;}
// Auto-pick steps through Performance..Ultra until gameplay holds about 60 fps.
static int pick_tier{};static bool picking{},pick_went_down{};static double pick_seconds{};static uint64_t pick_frames{},pick_last{};static std::string pick_result;
static Values values=defaults();
static uint64_t tokens[Count]{},revision=UINT64_MAX,status_at{},retry_at{};
static bool configured{},last_on{},native_ready{};
static void log(const std::string& text){host.log(0,"anygraphics",text.c_str());}
static void read(){std::lock_guard lock(mutex);if(revision==state.revision)return;revision=state.revision;for(size_t i=0;i<Count;++i)values[i]=clean(i,state.settings[i].committed.number);configured=false;log("SETTINGS_APPLIED revision="+std::to_string(revision)+" native_only=1");}
static uint32_t dirty(void*){std::lock_guard lock(mutex);return state.dirty();}
static uint32_t defaults(void*){std::lock_guard lock(mutex);return state.defaults();}
static void event(uint32_t kind,void*){std::lock_guard lock(mutex);if(kind==ANY_MENU_OPEN)state.begin();else if(kind==ANY_MENU_RESET)state.reset();else if(kind==ANY_MENU_CANCEL)state.cancel();else if(kind==ANY_MENU_APPLY){if(!state.apply())log("SETTINGS_SAVE failed draft_retained=1");}}
// Stages every value of a preset into the menu draft.
static void stage_preset(int level){Values draft{};for(size_t j=0;j<Count;++j)draft[j]=float(state.settings[j].draft.number);auto v=profile(draft,level);for(size_t j=0;j<Count;++j){auto next=state.settings[j].draft;next.number=v[j];state.stage(j,next);}}
// Commits a preset outside the menu (auto-pick).
static void commit_preset(int level){std::lock_guard lock(mutex);state.begin();auto value=state.settings[Preset].draft;value.number=level;state.stage(Preset,value);stage_preset(level);if(!state.apply())log("SETTINGS_SAVE failed draft_retained=1");state.status.clear();}
static const char* tier_name(int t){static const char* names[]={"Off","Performance","Low","Medium","High","Ultra"};return names[std::clamp(t,0,5)];}
// One auto-pick step: measure 4 s of gameplay (after 1.5 s to settle), then move a tier or stop.
static void auto_pick(bool gameplay){if(!picking)return;LARGE_INTEGER now,rate;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&rate);
 if(!gameplay){pick_last=0;return;}if(int(values[Preset])!=pick_tier){commit_preset(pick_tier);pick_seconds=0;pick_frames=0;pick_last=0;return;}if(pick_last){double dt=double(now.QuadPart-pick_last)/rate.QuadPart;if(dt<.5){pick_seconds+=dt;if(pick_seconds>1.5)++pick_frames;}}pick_last=now.QuadPart;
 if(pick_seconds<5.5)return;double fps=pick_frames/(pick_seconds-1.5);char text[160];
 int next=pick_tier;if(fps<55&&pick_tier>1){next=pick_tier-1;pick_went_down=true;}else if(fps>=80&&!pick_went_down&&pick_tier<5)next=pick_tier+1;
 if(next!=pick_tier){snprintf(text,sizeof(text),"Auto-pick: %s ran at %.0f fps, trying %s.",tier_name(pick_tier),fps,tier_name(next));pick_tier=next;commit_preset(next);}
 else{snprintf(text,sizeof(text),"Auto-pick chose %s (%.0f fps). Natural, Clear Day, Vibrant and Moody cost about the same as High or Ultra.",tier_name(pick_tier),fps);picking=false;}
 pick_result=text;log(std::string("AUTO_PICK ")+text);pick_seconds=0;pick_frames=0;}
static const AnyMenuV4* buttons;
// Soft film and Soft & warm need AnyAPI 0.39.3; older APIs reject them, so they fall back to Clean and Vivid there.
static bool soft_grades=true;
static bool api_at_least(const char* version,int major,int minor,int patch){int a=0,b=0,c=0;if(sscanf_s(version,"%d.%d.%d",&a,&b,&c)<2)return false;return a!=major?a>major:b!=minor?b>minor:c>=patch;}
// Fine-tuning groups: each opens and closes from its header row. Open state lasts for the session.
static bool open_groups[GroupCount]{};
static bool group_header(int g){auto id="group."+std::to_string(g);bool open=open_groups[g];
 if(buttons){const char* text[]={open?"Hide":"Show"};return buttons->button_row(id.c_str(),group_names[g],text,1)==1;}
 uint32_t v=open;return widgets->toggle_row(id.c_str(),group_names[g],&v)&&bool(v)!=open;}
// Status lines that belong with a group, shown at its end while it is open.
static void group_labels(int g){
 if(g==AntiAliasing){
 if(aa&&state.settings[NativeAA].draft.number>=3){AnySceneAntialiasingStatusV1 a;bool active=aa&&aa->status(&a)&&a.ready;auto choice=state.settings[NativeAA].draft.number;widgets->tabs->label("aa_status",active?(choice>=6?"TAA extra smooth: scene only, before HUD.":choice>=5?"TAA: scene only, before HUD.":choice==4?"Enhanced SMAA: scene only, before HUD.":"SMAA: scene only, before HUD."):"Scene AA is waiting for the scene renderer; native AA stays available.");}
 }
 else if(g==Lighting){
 if(lighting&&(state.settings[Atmosphere].draft.number>0||state.settings[SunShafts].draft.number>0||state.settings[LocalBeams].draft.number>0)){AnySceneLightingStatusV1 light;bool connected=lighting->status(&light)&&light.ready;widgets->tabs->label("lighting_status",connected?"Volumetric lighting: native scene depth and sun shadows.":"Experimental lighting is waiting for a supported scene.");}
 if(lighting_v2&&lamp_beams_blocked())widgets->tabs->label("lamp_beams_amd","Lamp beams are off on AMD graphics cards: they break the picture there.");
 }
 else if(g==Shadows){
 if(effects&&state.settings[DetailedAO].draft.number>0&&state.settings[NativeSSAO].draft.number!=1){AnySceneEffectsStatusV1 e;bool connected=effects->status(&e)&&e.ao_ready;widgets->tabs->label("ao_status",connected?"Detailed shading: replaces the game's ambient occlusion before lighting.":"Detailed shading is waiting for a supported scene; the game's ambient occlusion stays.");}
 }
 else if(g==Finish){
 if(effects&&state.settings[ToneMapping].draft.number>0){AnySceneEffectsStatusV1 e;bool connected=effects->status(&e)&&e.ready;widgets->tabs->label("effects_status",connected?"Tone mapping and bloom: HDR scene, before the game's tone mapping and HUD.":"Scene effects are waiting for a supported scene.");}
 }
 else if(g==Camera){
  uint32_t p=photo;bool clicked=false;
  if(buttons){const char* text[]={photo?"Stop":"Start"};clicked=buttons->button_row("photo_mode","Photo mode (free camera, hide HUD)",text,1)==1;if(clicked)photo=!photo;}
  else if(widgets->toggle_row("photo_mode","Photo mode (free camera, hide HUD)",&p))photo=p;
  char key[32]="F1";if(controls&&photo_action)controls->key_name(photo_action,key,sizeof(key));
  std::string hint=std::string("Press ")+key+" in game to hide the HUD and fly the camera with WASD. The photo keys are listed in the corner. Menus always show.";widgets->tabs->label("photo_hint",hint.c_str());}
}
// The GPU cost line and save status, after everything else.
static void finish_draw(){
 if(timing&&state.settings[CostReadout].draft.number==1){AnySceneTimingV1 t;if(timing->copy(&t)){char lit[24]="off",scene_aa[24]="off",fps[24]="";
  if(t.lighting_ms>=0)snprintf(lit,sizeof(lit),"%.2f ms",t.lighting_ms);if(t.antialiasing_ms>=0)snprintf(scene_aa,sizeof(scene_aa),"%.2f ms",t.antialiasing_ms);if(t.frame_ms>0)snprintf(fps,sizeof(fps)," | %.0f fps",1000/t.frame_ms);
  char finish[24]="off",shade[24]="off",sky_cost[24]="off",contact_cost[24]="off",gi_cost[24]="off",lens_cost[24]="off";AnySceneEffectsStatusV1 e;if(effects&&effects->status(&e)){if(e.ready&&e.gpu_ms>=0)snprintf(finish,sizeof(finish),"%.2f ms",e.gpu_ms);if(e.ao_ready&&e.ao_ms>=0)snprintf(shade,sizeof(shade),"%.2f ms",e.ao_ms);if(e.sky_ready&&e.sky_ms>=0)snprintf(sky_cost,sizeof(sky_cost),"%.2f ms",e.sky_ms);if(e.contact_ready&&e.contact_ms>=0)snprintf(contact_cost,sizeof(contact_cost),"%.2f ms",e.contact_ms);if(e.gi_ready&&e.gi_ms>=0)snprintf(gi_cost,sizeof(gi_cost),"%.2f ms",e.gi_ms);if(e.lens_ready&&e.lens_ms>=0)snprintf(lens_cost,sizeof(lens_cost),"%.2f ms",e.lens_ms);}
  char text[360];snprintf(text,sizeof(text),"GPU cost: lighting %s, effects %s, shading %s, contact shadows %s, bounce light %s, night sky %s, lens %s, scene AA %s%s",lit,finish,shade,contact_cost,gi_cost,sky_cost,lens_cost,scene_aa,fps);widgets->tabs->label("gpu_cost",text);}}
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
  if(i==CostReadout&&!timing)return false;
  // Light colour and wind (0.32) need scene controls v3.
  if(i==LightColour||i==WindStrength)return scene_v3!=nullptr;
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
   static const char* levels[]={"Off","Low","Medium","High","Ultra"};static const char* bloom[]={"Game setting","Off (override)","Low (override)","Medium (override)","High (override)","Ultra (override)"};
   uint32_t tier=i==NativeBloom?(value.number==0?0:value.number==1?1:2+nearest(float(state.settings[BloomAmount].draft.number),bloom_levels)):
    i==FogDensity?nearest(float(value.number),fog_levels):nearest(float(value.number),light_levels);
   changed=widgets->choice_row(id.c_str(),row.label.c_str(),i==NativeBloom?bloom:levels,i==NativeBloom?6:5,&tier);
   if(changed){if(i==NativeBloom){value.number=tier<2?tier:2;if(tier>=2){auto amount=state.settings[BloomAmount].draft;amount.number=bloom_levels[tier-2];state.stage(BloomAmount,amount);auto threshold=state.settings[BloomThreshold].draft;threshold.number=.75;state.stage(BloomThreshold,threshold);}}
    else value.number=i==FogDensity?fog_levels[tier]:light_levels[tier];}
  }
  else if(row.kind==ANY_SETTING_BOOL){uint32_t v=uint32_t(value.number);changed=widgets->toggle_row(id.c_str(),row.label.c_str(),&v);value.number=v;}
  else if(row.kind==ANY_SETTING_CHOICE){std::vector<const char*> choices;for(auto& c:row.choices)choices.push_back(c.c_str());uint32_t v=uint32_t(value.number);changed=widgets->choice_row(id.c_str(),row.label.c_str(),choices.data(),uint32_t(choices.size()),&v);value.number=v;}
  else changed=widgets->number_row(id.c_str(),row.label.c_str(),&value.number,row.minimum,row.maximum,row.step,0);
  if(changed){state.stage(i,std::move(value));if(i==Preset&&state.settings[Preset].draft.number!=0)stage_preset(int(state.settings[Preset].draft.number));
   // Choosing Custom from the preset row opens every group, so all the settings are in view; any other preset closes them.
   if(i==Preset)std::fill(std::begin(open_groups),std::end(open_groups),state.settings[Preset].draft.number==CustomProfile);}
  return changed!=0;};

 // Top level: the preset and a few main choices. Off shows only the general rows.
 for(size_t i:order){if(group_of(Index(i))!=Top||i==BloomAmount||i==BloomThreshold||i==SceneExposure||i==Advanced)continue;
  if(preset==0&&i!=Enabled&&i!=GameplayOnly&&i!=Preset&&i!=CostReadout)continue;
  if(!available(i))continue;draw_row(i);
  if(i==Preset){
   // One-shot actions: native buttons with menu v4, otherwise rows that act and switch straight back off.
   if(preset>0&&!custom){if(buttons){const char* text[]={"Reset"};if(buttons->button_row("reset_preset","Reset preset",text,1)==1)stage_preset(preset);}
    else{uint32_t reset=0;if(widgets->toggle_row("reset_preset","Reset preset",&reset)&&reset)stage_preset(preset);}}
   uint32_t pick=picking;if(widgets->toggle_row("auto_pick","Auto-pick preset (measures frame rate)",&pick)&&pick!=uint32_t(picking)){picking=pick;pick_went_down=false;pick_seconds=0;pick_frames=0;pick_last=0;
    auto from=preset>=1&&preset<=5?preset:preset==6||preset==VibrantProfile?5:4;pick_tier=from;pick_result=picking?"Auto-pick: close the menu and play; it measures a few seconds per preset.":"";}
   if(!pick_result.empty())widgets->tabs->label("auto_pick_status",pick_result.c_str());
  }
 }
 if(preset==0)return finish_draw();
 // Fine tuning: groups that open and close. Changing a row in a group turns the preset into Custom, which keeps the
 // current values, so nothing else moves.
 for(int g=Lighting;g<GroupCount;++g){
  if(g==Lighting&&!lighting)continue;
  bool any=false;for(size_t i:order)if(group_of(Index(i))==g&&available(i)){any=true;break;}
  if(!any&&g!=Camera)continue;
  if(group_header(g))open_groups[g]=!open_groups[g];
  if(!open_groups[g])continue;
  for(size_t i:order){if(group_of(Index(i))!=g||!available(i))continue;
   if(draw_row(i)&&state.settings[Preset].draft.number!=CustomProfile){auto custom_value=state.settings[Preset].draft;custom_value.number=CustomProfile;state.stage(Preset,custom_value);}}
  group_labels(g);
 }
 finish_draw();
}
static void render(const AnyFrameV1* frame,AnyCanvasV1*,void*){
 if(state.settings.size()!=Count)return;read();if(!scene)return;bool on=values[Enabled]>.5f&&values[Preset]!=0;
 AnyUiSnapshotV1 snapshot;bool gameplay=ui&&ui->copy(&snapshot)&&snapshot.kind==ANY_UI_GAMEPLAY;if(on&&values[GameplayOnly]>.5f)on=gameplay;
 // Photo mode key: toggles only in gameplay with the game focused; the HUD comes back in every menu.
 bool focused=!frame||frame->focused;auto key=photo_key();bool down=key&&focused&&(GetAsyncKeyState(int(key))&0x8000);if(down&&!photo_key_down&&gameplay){photo=!photo;log(photo?"PHOTO_MODE on=1":"PHOTO_MODE on=0");}photo_key_down=down;
 bool hide=photo&&gameplay&&effects;auto_pick(gameplay&&focused);photocam::update(photo,gameplay);static uint32_t photo_revision{};auto tweaks=photocam::revision.load();
 AnySceneStatusV1 status;bool now_ready=scene->status(&status)&&status.ready;
 const auto now=frame?frame->tick:GetTickCount64();
 if((!configured&&now>=retry_at)||on!=last_on||now_ready!=native_ready||hide!=last_hide||(hide&&tweaks!=photo_revision)){photo_revision=tweaks;bool all_accepted=true;auto v=effective(values);AnySceneParametersV1 p;
  p.enabled=on;p.aa=v[NativeAA]>=3?0:uint32_t(v[NativeAA]);p.bloom=uint32_t(v[NativeBloom]);p.ssao=uint32_t(v[NativeSSAO]);p.shadows=uint32_t(v[NativeShadows]);p.fog_blur=uint32_t(v[NativeFogBlur]);
  p.bloom_threshold=v[BloomThreshold];p.bloom_intensity=v[BloomAmount];p.sun=v[SunLight];p.sky=v[SkyLight];p.ambient=v[AmbientLight];p.fog=v[FogDensity];p.light_exposure=v[SceneExposure];
  if(aa_v2){AnySceneAntialiasingParametersV2 a;a.enabled=on&&v[NativeAA]>=3;a.method=aa_method(v[NativeAA]);a.quality=aa_quality(v[NativeAA],v[AAQuality]);a.sharpening=sharpen_levels[size_t(v[Sharpening])];if(!aa_v2->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  // Older APIs have no TAA: fall back to SMAA rather than turning AA off.
  else if(aa){AnySceneAntialiasingParametersV1 a;a.enabled=on&&v[NativeAA]>=3;a.method=std::min(aa_method(v[NativeAA]),2u);a.quality=uint32_t(v[AAQuality]);if(!aa->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  if(lighting){AnySceneLightingParametersV1 light;light.enabled=on&&(v[Atmosphere]>0||v[SunShafts]>0);light.quality=uint32_t(v[LightingQuality]);static const float density[]={0,.001f,.002f,.004f,.008f},strength[]={0,.5f,1,2,3},height[]={0,.01f,.025f,.06f};light.fog_density=v[Atmosphere]>0?density[size_t(v[Atmosphere])]:.002f;light.sun_shafts=strength[size_t(v[SunShafts])]*v[ShaftIntensity];static const float focus[]={.2f,.35f,.55f,.7f};light.anisotropy=focus[size_t(v[BeamFocus])];light.enabled=on&&(v[Atmosphere]>0||light.sun_shafts>0);light.fog_strength=v[Atmosphere]>0?1.f:0.f;light.height_falloff=height[size_t(v[GroundFog])];bool accepted;if(lighting_v2){AnySceneLightingParametersV2 local;local.scene=light;local.local_strength=lamp_beams_blocked()?0.f:strength[size_t(v[LocalBeams])]*v[LocalIntensity];static const uint32_t budgets[]={2,4,6,8};local.local_budget=budgets[size_t(v[LocalBudget])];local.scene.enabled=on&&(light.enabled||local.local_strength>0);
   if(lighting_v3){AnySceneLightingParametersV3 shaped;shaped.lighting=local;shaped.clarity=clarity_levels[size_t(v[ShaftClarity])];shaped.beam_reach=reach_levels[size_t(v[BeamReach])];shaped.sun_response=uint32_t(v[SunResponse]);shaped.temporal=uint32_t(v[TemporalLighting]);accepted=lighting_v3->set(&shaped);}
   else accepted=lighting_v2->set(&local);}else accepted=lighting->set(&light);if(!accepted){all_accepted=false;log("SCENE_LIGHTING_UPDATE rejected=1 retry_pending=1");}}
  if(effects){AnySceneEffectsParametersV1 e;bool shading=v[DetailedAO]>0&&v[NativeSSAO]!=1,sky=v[NightSky]>0||v[Moonlight]>0,contact=v[ContactShadows]>0,bounce=v[BounceLight]>0,lens=v[DepthOfField]>0||v[MotionBlur]>0,reach=v[DrawDistance]>0,heavens=v[SunDisc]>0||v[CloudLayer]>0;e.enabled=on&&(v[ToneMapping]>0||shading||sky||contact||bounce||lens||reach||heavens);e.hide_ui=hide;e.sun_disc=sun_disc_levels[size_t(v[SunDisc])];e.clouds=cloud_levels[size_t(v[CloudLayer])];e.draw_distance=draw_distance_levels[size_t(v[DrawDistance])];e.bounce_light=bounce_levels[size_t(v[BounceLight])];e.depth_of_field=std::min(focus_blur_levels[size_t(v[DepthOfField])]*std::clamp(v[FocusBlur],.25f,3.f),4.f);e.motion_blur=motion_blur_levels[size_t(v[MotionBlur])];e.contact_shadows=contact;e.contact_length=contact_levels[size_t(v[ContactShadows])];e.night_sky=sky;e.stars=star_levels[size_t(v[NightSky])];e.moon=moon_levels[size_t(v[NightSky])];e.moonlight=moonlight_levels[size_t(v[Moonlight])];e.finish=v[ToneMapping]>0;e.ambient_occlusion=shading;e.ao_radius=ao_radius_levels[size_t(v[DetailedAO])];e.ao_strength=ao_strength_levels[size_t(v[DetailedAO])];e.tonemap=v[ToneMapping]>=4?(soft_grades?3:2):v[ToneMapping]>=3?2:v[ToneMapping]>=2?1:0;e.look=uint32_t(v[ColourLook]>=6&&!soft_grades?5:v[ColourLook]);e.look_strength=look_strength_levels[size_t(v[LookStrength])];e.eye_adaptation=uint32_t(v[EyeAdaptation]);e.exposure=v[Brightness];e.bloom=scene_bloom_levels[size_t(v[SceneBloom])];e.glare=glare_levels[size_t(v[SunGlare])];e.vignette=vignette_levels[size_t(v[Vignette])];
   // Photo mode still works with AnyGraphics off: everything else stays off.
   if(hide&&!e.enabled){e=AnySceneEffectsParametersV1{};e.enabled=1;e.finish=0;e.ambient_occlusion=0;e.night_sky=0;e.contact_shadows=0;e.bounce_light=0;e.hide_ui=1;}
   if(hide)photocam::apply(e);
   if(!effects->set(&e)){all_accepted=false;log("SCENE_EFFECTS_UPDATE rejected=1 retry_pending=1");}}
  bool accepted=false;if(scene_v3){AnySceneParametersV3 looked;looked.detailed.scene=p;looked.detailed.clouds=uint32_t(v[Clouds]);looked.detailed.grass=uint32_t(v[Grass]);looked.detailed.foliage=uint32_t(v[Foliage]);auto colour=size_t(v[LightColour]);for(int c=0;c<3;++c){looked.key_tint[c]=key_tints[colour][c];looked.shade_tint[c]=shade_tints[colour][c];}looked.wind_scale=wind_levels[size_t(v[WindStrength])];accepted=scene_v3->set(&looked);}else if(scene_v2){AnySceneParametersV2 detailed;detailed.scene=p;detailed.clouds=uint32_t(v[Clouds]);detailed.grass=uint32_t(v[Grass]);detailed.foliage=uint32_t(v[Foliage]);accepted=scene_v2->set(&detailed);}else accepted=scene->set(&p);
  if(!accepted){all_accepted=false;log("NATIVE_SCENE_UPDATE rejected=1 retry_pending=1");}configured=all_accepted;retry_at=all_accepted?0:now+500;last_on=on;last_hide=hide;native_ready=now_ready;
 }
 if(frame&&frame->tick-status_at>=10000){status_at=frame->tick;if(now_ready)log("NATIVE_SCENE ready=1 renderer_calls="+std::to_string(status.renderer_calls)+" scene_calls="+std::to_string(status.scene_calls)+" rejected="+std::to_string(status.rejected_frames));else log("NATIVE_SCENE ready=0 overrides_not_active=1");}
}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){
 if(!host||!callbacks||host->abi!=ANYAPI_MOD_ABI||host->struct_size!=sizeof(*host)||!host->log)return false;graphics::host=*host;callbacks->id="anygraphics";callbacks->render=graphics::render;graphics::photocam::host=*host;callbacks->input=[](const AnyInputV1* e,void*)->uint32_t{return graphics::photocam::input(e);};callbacks->shutdown=[](void*){graphics::photocam::shutdown();};graphics::log("DLL_READY settings=54 presets=13 native_only=1 post_fx_passes=0");return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace graphics;auto services=AnyAPI_Services();if(!services)return;photocam::ready(services);
 scene=(const AnySceneControlsV1*)services->query("anyapi.scene_controls",1);if(scene&&(scene->version!=1||scene->struct_size!=sizeof(*scene)||!scene->set||!scene->status))scene=nullptr;
 scene_v3=(const AnySceneControlsV3*)services->query("anyapi.scene_controls",3);if(scene_v3&&(scene_v3->version!=3||scene_v3->struct_size!=sizeof(*scene_v3)||!scene_v3->set||!scene_v3->status))scene_v3=nullptr;
 scene_v2=(const AnySceneControlsV2*)services->query("anyapi.scene_controls",2);if(scene_v2&&(scene_v2->version!=2||scene_v2->struct_size!=sizeof(*scene_v2)||!scene_v2->set||!scene_v2->status))scene_v2=nullptr;
 log(scene?"NATIVE_SCENE service_available=1":"NATIVE_SCENE unavailable requires_API_28=1 overrides_disabled=1");
 aa=(const AnySceneAntialiasingV1*)services->query("anyapi.scene_antialiasing",1);if(aa&&(aa->struct_size!=sizeof(*aa)||aa->version!=1||!aa->set||!aa->status))aa=nullptr;
 aa_v2=(const AnySceneAntialiasingV2*)services->query("anyapi.scene_antialiasing",2);if(aa_v2&&(aa_v2->struct_size!=sizeof(*aa_v2)||aa_v2->version!=2||!aa_v2->set||!aa_v2->status))aa_v2=nullptr;
 timing=(const AnySceneTimingServiceV1*)services->query("anyapi.scene_timing",1);if(timing&&(timing->struct_size!=sizeof(*timing)||timing->version!=1||!timing->copy))timing=nullptr;
 gpu_info=(const AnyGpuInfoServiceV1*)services->query("anyapi.gpu_info",1);if(gpu_info&&(gpu_info->struct_size!=sizeof(*gpu_info)||gpu_info->version!=1||!gpu_info->copy))gpu_info=nullptr;
 effects=(const AnySceneEffectsV1*)services->query("anyapi.scene_effects",1);if(effects&&(effects->struct_size!=sizeof(*effects)||effects->version!=1||!effects->set||!effects->status))effects=nullptr;
 lighting=(const AnySceneLightingV1*)services->query("anyapi.scene_lighting",1);if(lighting&&(lighting->struct_size!=sizeof(*lighting)||lighting->version!=1||!lighting->set||!lighting->status))lighting=nullptr;
 lighting_v2=(const AnySceneLightingV2*)services->query("anyapi.scene_lighting",2);if(lighting_v2&&(lighting_v2->struct_size!=sizeof(*lighting_v2)||lighting_v2->version!=2||!lighting_v2->set||!lighting_v2->status))lighting_v2=nullptr;
 lighting_v3=(const AnySceneLightingV3*)services->query("anyapi.scene_lighting",3);if(lighting_v3&&(lighting_v3->struct_size!=sizeof(*lighting_v3)||lighting_v3->version!=3||!lighting_v3->set||!lighting_v3->status||!lighting_v2))lighting_v3=nullptr;
 ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui&&(ui->version!=1||ui->struct_size!=sizeof(*ui)||!ui->copy))ui=nullptr;
 controls=(const ModControlsV1*)services->query("anyhelpers.controls",1);if(controls&&controls->struct_size==sizeof(*controls)&&controls->version==1&&controls->register_action&&controls->key&&controls->key_name){ModControlActionV1 d;d.mod_id="anygraphics";d.mod_name="AnyGraphics";d.action_id="photo_mode";d.label="Photo mode (free camera, hide HUD)";d.default_key=VK_F1;photo_action=controls->register_action(&d);}else controls=nullptr;
 menu=(const AnyMenuV1*)services->query("anyapi.menu",1);widgets=(const AnyMenuV3*)services->query("anyapi.menu",3);
 if(auto build=(const AnyBuildV1*)services->query("anyapi.build",1);build&&build->struct_size==sizeof(*build)&&build->version==1&&build->copy){AnyBuildInfoV1 info;if(build->copy(&info))soft_grades=api_at_least(info.api_version,0,39,3);}
 buttons=(const AnyMenuV4*)services->query("anyapi.menu",4);if(buttons&&(buttons->version!=4||buttons->struct_size!=sizeof(*buttons)||!buttons->button_row))buttons=nullptr;
 if(!menu||menu->version!=1||menu->struct_size!=sizeof(*menu)||!menu->add_section||!widgets||widgets->version!=3||widgets->struct_size!=sizeof(*widgets)){log("GRAPHICS_MENU unavailable=1");return;}
 auto own_file=std::filesystem::path(host.plugin_directory)/L"AnyGraphics"/L"settings.tsv";
 std::error_code missing;state.file=std::filesystem::exists(own_file,missing)?own_file:std::filesystem::path(host.plugin_directory)/L"AnyHelpers"/L"settings.tsv";state.load();bool imported=state.file!=own_file;state.file=own_file;
 // Keep only retained native controls when importing the old shared settings file.
 std::map<std::string,helpersettings::Saved> retained;
 for(auto& o:definitions){std::string group=o.group;std::transform(group.begin(),group.end(),group.begin(),[](unsigned char c){return char(std::tolower(c));});auto key="anygraphics."+group+"\t"+o.id;auto old=state.saved.find(key);if(old!=state.saved.end())retained.emplace(*old);}
 if(imported){auto b=retained.find("anygraphics.scene\tnative_bloom"),enabled=state.saved.find("anygraphics.bloom\tbloom");if(b!=retained.end()&&b->second.value.number==2&&enabled!=state.saved.end()&&enabled->second.value.number==0)b->second.value.number=1;}
 state.saved=std::move(retained);
 static std::array<std::string,Count> ids;
 static const char* choices[]={"Game setting","Off (override)","On (override)"},*aa[]={"Game setting","Off (override)","FXAA (override)","SMAA 1x (scene)","Enhanced SMAA (scene)","TAA (scene)","TAA extra smooth (scene)"},*aa_quality[]={"Low","Medium","High","Ultra"},*bloom[]={"Game setting","Off (override)","Custom (override)"},*looks[]={"Off","Performance","Low","Balanced","High","Ultra","Cinematic","Natural","Vibrant","Moody","Clear Day","Custom","Soft & warm"},*haze[]={"Clear","Light","Natural","Classic (original)"},*reach[]={"Near","Medium","Far","Unlimited"},*sharpness[]={"Off","Low","Medium","High"},*strengths[]={"Off","Low","Medium","High","Ultra"},*falloff[]={"Off","Low","Medium","High"},*focus[]={"Soft","Balanced","Focused","Strong"},*budgets[]={"2 lights","4 lights","6 lights","8 lights"},*tones[]={"Off (game look)","Game curve","Filmic","Clean","Soft film"},*grades[]={"None","Warm","Cool","Teal & orange","Moody","Vivid","Soft & warm"},*shading[]={"Off","Low","Medium","High"},*nights[]={"Off","Subtle","Normal","Bright"},*distances[]={"Game setting","Far (1.5x)","Very far (2x)","Extreme (3x)"},*suns[]={"Off","Soft","Normal","Bright"},*cloud_amounts[]={"Off","Few","Some","Many"},*amounts[]={"Subtle","Normal","Strong"},*glows[]={"Off","Subtle","Normal","Strong"},*corners[]={"Off","Light","Medium","Strong"},*light_colours[]={"Game","Subtle","Normal","Strong"},*winds[]={"Calm","Game","Breezy","Stormy"};
 for(size_t i=0;i<Count;++i){auto& o=definitions[i];ids[i]="anygraphics."+std::string(o.group);std::transform(ids[i].begin(),ids[i].end(),ids[i].begin(),[](unsigned char c){return char(std::tolower(c));});AnyModSettingV1 d;d.mod_id=ids[i].c_str();d.mod_name="AnyGraphics";d.setting_id=o.id;d.label=o.label;d.description=o.description;d.order=int32_t(i);d.kind=o.kind;d.default_number=o.initial;d.minimum=o.minimum;d.maximum=o.maximum;d.step=o.step;
  if(d.kind==ANY_SETTING_CHOICE){d.choices=i==LightColour?light_colours:i==WindStrength?winds:i==SunDisc?suns:i==CloudLayer?cloud_amounts:i==DrawDistance?distances:i==NightSky?nights:i==Moonlight||i==ContactShadows||i==BounceLight||i==DepthOfField||i==MotionBlur?shading:i==DetailedAO?shading:i==ToneMapping?tones:i==ColourLook?grades:i==LookStrength?amounts:i==SceneBloom||i==SunGlare?glows:i==Vignette?corners:i==ShaftClarity?haze:i==BeamReach?reach:i==Sharpening?sharpness:i==LocalBeams?strengths:i==LocalBudget?budgets:i==BeamFocus?focus:i==Atmosphere||i==SunShafts?strengths:i==LightingQuality?aa_quality:i==GroundFog?falloff:i==Preset?looks:i==NativeAA?aa:i==NativeBloom?bloom:i==AAQuality?aa_quality:choices;d.choice_count=i==LightColour||i==WindStrength||i==SunDisc||i==CloudLayer||i==DrawDistance||i==DepthOfField||i==MotionBlur||i==BounceLight||i==ContactShadows||i==NightSky||i==Moonlight||i==DetailedAO||i==SceneBloom||i==SunGlare||i==Vignette?4:i==ToneMapping?5:i==ColourLook?7:i==LookStrength?3:i==ShaftClarity||i==BeamReach||i==Sharpening?4:i==LocalBeams?5:i==LocalBudget?4:i==BeamFocus?4:i==Atmosphere||i==SunShafts?5:i==LightingQuality||i==GroundFog?4:i==Preset?13:i==AAQuality?4:i==NativeAA?(graphics::aa_v2?7:graphics::aa?5:3):3;}tokens[i]=state.add(&d);
 }
 if(state.settings.size()!=Count){log("GRAPHICS_MENU registration_failed=1");return;}
 // Migrate numeric tuning to the nearest named strength without changing native on/off choices.
 for(auto i:{SunLight,SkyLight,AmbientLight,FogDensity}){auto& r=state.settings[i];r.committed.number=i==FogDensity?fog_levels[nearest(float(r.committed.number),fog_levels)]:light_levels[nearest(float(r.committed.number),light_levels)];r.draft=r.committed;}
 auto& amount=state.settings[BloomAmount];amount.committed.number=bloom_levels[nearest(float(amount.committed.number),bloom_levels)];amount.draft=amount.committed;
 // Without the V2 AA service the TAA choices are not offered; show SMAA instead.
 if(!aa_v2){auto& r=state.settings[NativeAA];if(r.committed.number>4)r.committed.number=graphics::aa?3:2;r.draft=r.committed;}
 state.settings[SceneExposure].committed.number=0;state.settings[SceneExposure].draft=state.settings[SceneExposure].committed;
 AnyMenuSectionV1 section;section.id="anygraphics";section.title="AnyGraphics · Modded";section.location=ANY_MENU_SETTINGS_GRAPHICS;section.order=100;section.draw=draw;section.event=event;section.dirty=dirty;section.defaults=defaults;
 if(!menu->add_section(&section)){log("GRAPHICS_MENU registration_failed=1 requires_API_28=1");return;}
 log("SETTINGS_REGISTERED count=54 location=settings.graphics native_only=1 post_fx_passes=0 imported="+std::to_string(imported));
}
