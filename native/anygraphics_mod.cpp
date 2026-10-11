#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_lighting_v3.h"
#include "anyapi_scene_antialiasing_v2.h"
#include "anyapi_scene_timing_v1.h"
#include "anyhelpers_settings_v2.h"
#include "anygraphics_options.h"
#include "anyapi_menu_v3.h"
#include "anyhelpers_settings_state.h"
#include <mutex>
namespace graphics {
using namespace graphics_options;
static AnyModHostV1 host;
static const AnySceneControlsV1* scene;
static const AnySceneControlsV2* scene_v2;
static const AnySceneLightingV1* lighting;static const AnySceneLightingV2* lighting_v2;static const AnySceneLightingV3* lighting_v3;
static const AnySceneAntialiasingV1* aa;static const AnySceneAntialiasingV2* aa_v2;
static const AnySceneTimingServiceV1* timing;
static helpersettings::State state;static std::mutex mutex;
static const AnyMenuV1* menu;static const AnyMenuV3* widgets;
static const AnyUiStateV1* ui;
static Values values=defaults();
static uint64_t tokens[Count]{},revision=UINT64_MAX,status_at{},retry_at{};
static bool configured{},last_on{},native_ready{};
static void log(const std::string& text){host.log(0,"anygraphics",text.c_str());}
static void read(){std::lock_guard lock(mutex);if(revision==state.revision)return;revision=state.revision;for(size_t i=0;i<Count;++i)values[i]=clean(i,state.settings[i].committed.number);configured=false;log("SETTINGS_APPLIED revision="+std::to_string(revision)+" native_only=1");}
static uint32_t dirty(void*){std::lock_guard lock(mutex);return state.dirty();}
static uint32_t defaults(void*){std::lock_guard lock(mutex);return state.defaults();}
static void event(uint32_t kind,void*){std::lock_guard lock(mutex);if(kind==ANY_MENU_OPEN)state.begin();else if(kind==ANY_MENU_RESET)state.reset();else if(kind==ANY_MENU_CANCEL)state.cancel();else if(kind==ANY_MENU_APPLY){if(!state.apply())log("SETTINGS_SAVE failed draft_retained=1");}}
static void draw(const AnyMenuFrameV1*,void*){std::lock_guard lock(mutex);
 AnySceneStatusV1 status;bool ready=scene&&scene->status(&status)&&status.ready;
 widgets->tabs->label("source",ready?"Game controls above; AnyGraphics overrides below (Modded).":"Native rendering connection unavailable; overrides are not active.");
 bool advanced_on=state.settings[Advanced].draft.number==1;
 auto shown=[&](size_t i){return state.settings[i].draft.number>0;};
 for(size_t i:order){if(i==BloomAmount||i==BloomThreshold||i==SceneExposure)continue;
  // Rows added in 0.30 need the matching API services and only show with the effect they tune.
  if(i==Sharpening&&(!aa_v2||state.settings[NativeAA].draft.number<3))continue;
  if((i==ShaftClarity||i==BeamReach||i==SunResponse)&&(!lighting_v3||!shown(SunShafts)))continue;
  if(i==TemporalLighting&&(!lighting_v3||(!shown(Atmosphere)&&!shown(SunShafts)&&!shown(LocalBeams))))continue;
  if(i==CostReadout&&!timing)continue;
  if(i<ShaftClarity){if(i>=Clouds&&i<=Foliage&&!scene_v2)continue;if(i>=Atmosphere&&!lighting)continue;if(i>=LightingQuality&&i<LocalBeams&&state.settings[Atmosphere].draft.number==0&&state.settings[SunShafts].draft.number==0&&(!lighting_v2||state.settings[LocalBeams].draft.number==0))continue;if(i==ShaftIntensity&&state.settings[SunShafts].draft.number==0)continue;if(i==BeamFocus&&state.settings[SunShafts].draft.number==0&&(!lighting_v2||state.settings[LocalBeams].draft.number==0))continue;if(i>=LocalBeams&&!lighting_v2)continue;if(i>LocalBeams&&state.settings[LocalBeams].draft.number==0)continue;if(i==AAQuality&&state.settings[NativeAA].draft.number<3)continue;if(advanced(i)&&i!=FogDensity&&i<Atmosphere&&!advanced_on)continue;}
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
  if(changed){state.stage(i,std::move(value));if(i==Preset&&state.settings[Preset].draft.number!=0){Values draft{};for(size_t j=0;j<Count;++j)draft[j]=float(state.settings[j].draft.number);auto v=profile(draft,int(draft[Preset]));for(size_t j=0;j<Count;++j){auto next=state.settings[j].draft;next.number=v[j];state.stage(j,next);}}}
 }
 if(aa&&state.settings[NativeAA].draft.number>=3){AnySceneAntialiasingStatusV1 a;bool active=aa&&aa->status(&a)&&a.ready;auto choice=state.settings[NativeAA].draft.number;widgets->tabs->label("aa_status",active?(choice>=6?"TAA + jitter: scene only, before HUD.":choice>=5?"TAA: scene only, before HUD.":choice==4?"Enhanced SMAA: scene only, before HUD.":"SMAA: scene only, before HUD."):"Scene AA is waiting for the scene renderer; native AA stays available.");}
 if(lighting&&(state.settings[Atmosphere].draft.number>0||state.settings[SunShafts].draft.number>0||state.settings[LocalBeams].draft.number>0)){AnySceneLightingStatusV1 light;bool connected=lighting->status(&light)&&light.ready;widgets->tabs->label("lighting_status",connected?"Volumetric lighting: native scene depth and sun shadows.":"Experimental lighting is waiting for a supported scene.");}
 if(timing&&state.settings[CostReadout].draft.number==1){AnySceneTimingV1 t;if(timing->copy(&t)){char lit[24]="off",scene_aa[24]="off",fps[24]="";
  if(t.lighting_ms>=0)snprintf(lit,sizeof(lit),"%.2f ms",t.lighting_ms);if(t.antialiasing_ms>=0)snprintf(scene_aa,sizeof(scene_aa),"%.2f ms",t.antialiasing_ms);if(t.frame_ms>0)snprintf(fps,sizeof(fps)," | %.0f fps",1000/t.frame_ms);
  char text[128];snprintf(text,sizeof(text),"GPU cost: lighting %s, scene AA %s%s",lit,scene_aa,fps);widgets->tabs->label("gpu_cost",text);}}
 if(!state.status.empty())widgets->tabs->label("save_status",state.status.c_str());
}
static void render(const AnyFrameV1* frame,AnyCanvasV1*,void*){
 if(state.settings.size()!=Count)return;read();if(!scene)return;bool on=values[Enabled]>.5f&&values[Preset]!=0;
 if(on&&values[GameplayOnly]>.5f){AnyUiSnapshotV1 state;on=ui&&ui->copy(&state)&&state.kind==ANY_UI_GAMEPLAY;}
 AnySceneStatusV1 status;bool now_ready=scene->status(&status)&&status.ready;
 const auto now=frame?frame->tick:GetTickCount64();
 if((!configured&&now>=retry_at)||on!=last_on||now_ready!=native_ready){bool all_accepted=true;auto v=effective(values);AnySceneParametersV1 p;
  p.enabled=on;p.aa=v[NativeAA]>=3?0:uint32_t(v[NativeAA]);p.bloom=uint32_t(v[NativeBloom]);p.ssao=uint32_t(v[NativeSSAO]);p.shadows=uint32_t(v[NativeShadows]);p.fog_blur=uint32_t(v[NativeFogBlur]);
  p.bloom_threshold=v[BloomThreshold];p.bloom_intensity=v[BloomAmount];p.sun=v[SunLight];p.sky=v[SkyLight];p.ambient=v[AmbientLight];p.fog=v[FogDensity];p.light_exposure=v[SceneExposure];
  if(aa_v2){AnySceneAntialiasingParametersV2 a;a.enabled=on&&v[NativeAA]>=3;a.method=aa_method(v[NativeAA]);a.quality=uint32_t(v[AAQuality]);a.sharpening=sharpen_levels[size_t(v[Sharpening])];if(!aa_v2->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  // Older APIs have no TAA: fall back to SMAA rather than turning AA off.
  else if(aa){AnySceneAntialiasingParametersV1 a;a.enabled=on&&v[NativeAA]>=3;a.method=std::min(aa_method(v[NativeAA]),2u);a.quality=uint32_t(v[AAQuality]);if(!aa->set(&a)){all_accepted=false;log("SCENE_AA_UPDATE rejected=1 retry_pending=1");}}
  if(lighting){AnySceneLightingParametersV1 light;light.enabled=on&&(v[Atmosphere]>0||v[SunShafts]>0);light.quality=uint32_t(v[LightingQuality]);static const float density[]={0,.001f,.002f,.004f,.008f},strength[]={0,.5f,1,2,3},height[]={0,.01f,.025f,.06f};light.fog_density=v[Atmosphere]>0?density[size_t(v[Atmosphere])]:.002f;light.sun_shafts=strength[size_t(v[SunShafts])]*v[ShaftIntensity];static const float focus[]={.2f,.35f,.55f,.7f};light.anisotropy=focus[size_t(v[BeamFocus])];light.enabled=on&&(v[Atmosphere]>0||light.sun_shafts>0);light.fog_strength=v[Atmosphere]>0?1.f:0.f;light.height_falloff=height[size_t(v[GroundFog])];bool accepted;if(lighting_v2){AnySceneLightingParametersV2 local;local.scene=light;local.local_strength=strength[size_t(v[LocalBeams])]*v[LocalIntensity];static const uint32_t budgets[]={2,4,6,8};local.local_budget=budgets[size_t(v[LocalBudget])];local.scene.enabled=on&&(light.enabled||local.local_strength>0);
   if(lighting_v3){AnySceneLightingParametersV3 shaped;shaped.lighting=local;shaped.clarity=clarity_levels[size_t(v[ShaftClarity])];shaped.beam_reach=reach_levels[size_t(v[BeamReach])];shaped.sun_response=uint32_t(v[SunResponse]);shaped.temporal=uint32_t(v[TemporalLighting]);accepted=lighting_v3->set(&shaped);}
   else accepted=lighting_v2->set(&local);}else accepted=lighting->set(&light);if(!accepted){all_accepted=false;log("SCENE_LIGHTING_UPDATE rejected=1 retry_pending=1");}}
  bool accepted=false;if(scene_v2){AnySceneParametersV2 detailed;detailed.scene=p;detailed.clouds=uint32_t(v[Clouds]);detailed.grass=uint32_t(v[Grass]);detailed.foliage=uint32_t(v[Foliage]);accepted=scene_v2->set(&detailed);}else accepted=scene->set(&p);
  if(!accepted){all_accepted=false;log("NATIVE_SCENE_UPDATE rejected=1 retry_pending=1");}configured=all_accepted;retry_at=all_accepted?0:now+500;last_on=on;native_ready=now_ready;
 }
 if(frame&&frame->tick-status_at>=10000){status_at=frame->tick;if(now_ready)log("NATIVE_SCENE ready=1 renderer_calls="+std::to_string(status.renderer_calls)+" scene_calls="+std::to_string(status.scene_calls)+" rejected="+std::to_string(status.rejected_frames));else log("NATIVE_SCENE ready=0 overrides_not_active=1");}
}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){
 if(!host||!callbacks||host->abi!=ANYAPI_MOD_ABI||host->struct_size!=sizeof(*host)||!host->log)return false;graphics::host=*host;callbacks->id="anygraphics";callbacks->render=graphics::render;graphics::log("DLL_READY settings=35 presets=7 native_only=1 post_fx_passes=0");return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace graphics;auto services=AnyAPI_Services();if(!services)return;
 scene=(const AnySceneControlsV1*)services->query("anyapi.scene_controls",1);if(scene&&(scene->version!=1||scene->struct_size!=sizeof(*scene)||!scene->set||!scene->status))scene=nullptr;
 scene_v2=(const AnySceneControlsV2*)services->query("anyapi.scene_controls",2);if(scene_v2&&(scene_v2->version!=2||scene_v2->struct_size!=sizeof(*scene_v2)||!scene_v2->set||!scene_v2->status))scene_v2=nullptr;
 log(scene?"NATIVE_SCENE service_available=1":"NATIVE_SCENE unavailable requires_API_28=1 overrides_disabled=1");
 aa=(const AnySceneAntialiasingV1*)services->query("anyapi.scene_antialiasing",1);if(aa&&(aa->struct_size!=sizeof(*aa)||aa->version!=1||!aa->set||!aa->status))aa=nullptr;
 aa_v2=(const AnySceneAntialiasingV2*)services->query("anyapi.scene_antialiasing",2);if(aa_v2&&(aa_v2->struct_size!=sizeof(*aa_v2)||aa_v2->version!=2||!aa_v2->set||!aa_v2->status))aa_v2=nullptr;
 timing=(const AnySceneTimingServiceV1*)services->query("anyapi.scene_timing",1);if(timing&&(timing->struct_size!=sizeof(*timing)||timing->version!=1||!timing->copy))timing=nullptr;
 lighting=(const AnySceneLightingV1*)services->query("anyapi.scene_lighting",1);if(lighting&&(lighting->struct_size!=sizeof(*lighting)||lighting->version!=1||!lighting->set||!lighting->status))lighting=nullptr;
 lighting_v2=(const AnySceneLightingV2*)services->query("anyapi.scene_lighting",2);if(lighting_v2&&(lighting_v2->struct_size!=sizeof(*lighting_v2)||lighting_v2->version!=2||!lighting_v2->set||!lighting_v2->status))lighting_v2=nullptr;
 lighting_v3=(const AnySceneLightingV3*)services->query("anyapi.scene_lighting",3);if(lighting_v3&&(lighting_v3->struct_size!=sizeof(*lighting_v3)||lighting_v3->version!=3||!lighting_v3->set||!lighting_v3->status||!lighting_v2))lighting_v3=nullptr;
 ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui&&(ui->version!=1||ui->struct_size!=sizeof(*ui)||!ui->copy))ui=nullptr;
 menu=(const AnyMenuV1*)services->query("anyapi.menu",1);widgets=(const AnyMenuV3*)services->query("anyapi.menu",3);
 if(!menu||menu->version!=1||menu->struct_size!=sizeof(*menu)||!menu->add_section||!widgets||widgets->version!=3||widgets->struct_size!=sizeof(*widgets)){log("GRAPHICS_MENU unavailable=1");return;}
 auto own_file=std::filesystem::path(host.plugin_directory)/L"AnyGraphics"/L"settings.tsv";
 state.file=std::filesystem::exists(own_file)?own_file:std::filesystem::path(host.plugin_directory)/L"AnyHelpers"/L"settings.tsv";state.load();bool imported=state.file!=own_file;state.file=own_file;
 // Keep only retained native controls when importing the old shared settings file.
 std::map<std::string,helpersettings::Saved> retained;
 for(auto& o:definitions){std::string group=o.group;std::transform(group.begin(),group.end(),group.begin(),[](unsigned char c){return char(std::tolower(c));});auto key="anygraphics."+group+"\t"+o.id;auto old=state.saved.find(key);if(old!=state.saved.end())retained.emplace(*old);}
 if(imported){auto b=retained.find("anygraphics.scene\tnative_bloom"),enabled=state.saved.find("anygraphics.bloom\tbloom");if(b!=retained.end()&&b->second.value.number==2&&enabled!=state.saved.end()&&enabled->second.value.number==0)b->second.value.number=1;}
 state.saved=std::move(retained);
 static std::array<std::string,Count> ids;
 static const char* choices[]={"Game setting","Off (override)","On (override)"},*aa[]={"Game setting","Off (override)","FXAA (override)","SMAA 1x (scene)","Enhanced SMAA (scene)","TAA (scene)","TAA + jitter (scene, smoothest)"},*aa_quality[]={"Low","Medium","High","Ultra"},*bloom[]={"Game setting","Off (override)","Custom (override)"},*looks[]={"Off","Performance","Low","Medium","High","Ultra","Cinematic"},*haze[]={"Clear","Light","Natural","Classic (original)"},*reach[]={"Near","Medium","Far","Unlimited"},*sharpness[]={"Off","Low","Medium","High"},*strengths[]={"Off","Low","Medium","High","Ultra"},*falloff[]={"Off","Low","Medium","High"},*focus[]={"Soft","Balanced","Focused","Strong"},*budgets[]={"2 lights","4 lights","6 lights","8 lights"};
 for(size_t i=0;i<Count;++i){auto& o=definitions[i];ids[i]="anygraphics."+std::string(o.group);std::transform(ids[i].begin(),ids[i].end(),ids[i].begin(),[](unsigned char c){return char(std::tolower(c));});AnyModSettingV1 d;d.mod_id=ids[i].c_str();d.mod_name="AnyGraphics";d.setting_id=o.id;d.label=o.label;d.description=o.description;d.order=int32_t(i);d.kind=o.kind;d.default_number=o.initial;d.minimum=o.minimum;d.maximum=o.maximum;d.step=o.step;
  if(d.kind==ANY_SETTING_CHOICE){d.choices=i==ShaftClarity?haze:i==BeamReach?reach:i==Sharpening?sharpness:i==LocalBeams?strengths:i==LocalBudget?budgets:i==BeamFocus?focus:i==Atmosphere||i==SunShafts?strengths:i==LightingQuality?aa_quality:i==GroundFog?falloff:i==Preset?looks:i==NativeAA?aa:i==NativeBloom?bloom:i==AAQuality?aa_quality:choices;d.choice_count=i==ShaftClarity||i==BeamReach||i==Sharpening?4:i==LocalBeams?5:i==LocalBudget?4:i==BeamFocus?4:i==Atmosphere||i==SunShafts?5:i==LightingQuality||i==GroundFog?4:i==Preset?7:i==AAQuality?4:i==NativeAA?(graphics::aa_v2?7:graphics::aa?5:3):3;}tokens[i]=state.add(&d);
 }
 if(state.settings.size()!=Count){log("GRAPHICS_MENU registration_failed=1");return;}
 // Migrate numeric tuning to the nearest named strength without changing native on/off choices.
 for(auto i:{SunLight,SkyLight,AmbientLight,FogDensity}){auto& r=state.settings[i];r.committed.number=i==FogDensity?fog_levels[nearest(float(r.committed.number),fog_levels)]:light_levels[nearest(float(r.committed.number),light_levels)];r.draft=r.committed;}
 auto& amount=state.settings[BloomAmount];amount.committed.number=bloom_levels[nearest(float(amount.committed.number),bloom_levels)];amount.draft=amount.committed;
 // Without the V2 AA service the TAA choices are not offered; show SMAA instead.
 if(!aa_v2){auto& r=state.settings[NativeAA];if(r.committed.number>4)r.committed.number=aa?3:2;r.draft=r.committed;}
 state.settings[SceneExposure].committed.number=0;state.settings[SceneExposure].draft=state.settings[SceneExposure].committed;
 AnyMenuSectionV1 section;section.id="anygraphics";section.title="AnyGraphics · Modded";section.location=ANY_MENU_SETTINGS_GRAPHICS;section.order=100;section.draw=draw;section.event=event;section.dirty=dirty;section.defaults=defaults;
 if(!menu->add_section(&section)){log("GRAPHICS_MENU registration_failed=1 requires_API_28=1");return;}
 log("SETTINGS_REGISTERED count=35 location=settings.graphics native_only=1 post_fx_passes=0 imported="+std::to_string(imported));
}
