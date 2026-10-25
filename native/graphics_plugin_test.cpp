#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "anyapi_services_v1.h"
#include "anyapi_menu_v2.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_controls_v3.h"
#include "anyapi_scene_lighting_v1.h"
#include "anyapi_scene_lighting_v2.h"
#include "anyapi_scene_lighting_v3.h"
#include "anyapi_scene_antialiasing_v2.h"
#include "anyapi_scene_timing_v1.h"
#include "anyapi_scene_effects_v1.h"
#include "anyapi_scene_effects_v2.h"
#include "anyapi_scene_antialiasing_v1.h"
#include "anyapi_post_process_v1.h"
#include "anyapi_scene_controls_v4.h"
#include "anyapi_scene_lighting_v4.h"
#include "anyapi_scene_antialiasing_v3.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_scene_effects_v3.h"
#include "anyapi_gpu_info_v2.h"
#include "anygraphics_options.h"
#undef assert
#define assert(condition) do { if(!(condition)){std::cerr<<"Check failed at line "<<__LINE__<<": "<<#condition<<std::endl;ExitProcess(2);} } while(0)
static AnySceneAntialiasingParametersV1 aa_policy;static unsigned aa_submissions{};
static bool aa_set(const AnySceneAntialiasingParametersV1* p){aa_policy=*p;++aa_submissions;return true;}
static bool aa_status(AnySceneAntialiasingStatusV1* s){s->available=s->ready=1;return true;}
static const AnySceneAntialiasingV1 aa_api{sizeof(AnySceneAntialiasingV1),1,aa_set,aa_status};
static AnySceneLightingParametersV1 lighting_policy;static unsigned lighting_submissions{};
static bool lighting_set(const AnySceneLightingParametersV1* p){lighting_policy=*p;++lighting_submissions;return true;}
static bool lighting_status(AnySceneLightingStatusV1* s){s->available=s->ready=1;return true;}
static const AnySceneLightingV1 lighting_api{sizeof(AnySceneLightingV1),1,lighting_set,lighting_status};
static AnySceneLightingParametersV2 local_policy;
static bool lighting_set_v2(const AnySceneLightingParametersV2* p){local_policy=*p;return lighting_set(&p->scene);}
static const AnySceneLightingV2 local_api{sizeof(AnySceneLightingV2),2,lighting_set_v2,lighting_status};
static AnySceneLightingParametersV3 shaped_policy;
static bool lighting_set_v3(const AnySceneLightingParametersV3* p){shaped_policy=*p;return lighting_set_v2(&p->lighting);}
static const AnySceneLightingV3 shaped_api{sizeof(AnySceneLightingV3),3,lighting_set_v3,lighting_status};
static AnySceneAntialiasingParametersV2 aa2_policy;
static bool aa_set_v2(const AnySceneAntialiasingParametersV2* p){aa2_policy=*p;return true;}
static const AnySceneAntialiasingV2 aa2_api{sizeof(AnySceneAntialiasingV2),2,aa_set_v2,aa_status};
static unsigned timing_reads{};
static bool timing_copy(AnySceneTimingV1* t){++timing_reads;t->lighting_ms=1.25f;t->antialiasing_ms=.5f;t->frame_ms=16.6f;return true;}
static const AnySceneTimingServiceV1 timing_api{sizeof(AnySceneTimingServiceV1),1,timing_copy};
static AnySceneEffectsParametersV1 effects_policy;static unsigned effects_reads{};
static unsigned effects_sets{};static bool effects_set(const AnySceneEffectsParametersV1* p){effects_policy=*p;++effects_sets;return true;}
static bool effects_status(AnySceneEffectsStatusV1* s){++effects_reads;s->ready=1;s->gpu_ms=.4f;return true;}
static const AnySceneEffectsV1 effects_api{sizeof(AnySceneEffectsV1),1,effects_set,effects_status};
static AnySceneEffectsParametersV2 water_policy;static unsigned water_sets{},water_reads{};
static bool effects_set_v2(const AnySceneEffectsParametersV2* p){water_policy=*p;++water_sets;return effects_set(&p->effects);}
static bool effects_status_v2(AnySceneEffectsStatusV2* s){++water_reads;s->water_ready=1;s->water_ms=.3f;return effects_status(&s->effects);}
static const AnySceneEffectsV2 effects2_api{sizeof(AnySceneEffectsV2),2,effects_set_v2,effects_status,effects_status_v2};
static AnySceneParametersV1 policy;static bool ready=true;static unsigned submissions{},post_calls{};
static bool reject_scene=false;
static bool set(const AnySceneParametersV1* p){++submissions;if(reject_scene)return false;policy=*p;return true;}
static AnySceneParametersV2 detail_policy;
static bool set_v2(const AnySceneParametersV2* p){detail_policy=*p;return set(&p->scene);}
static bool status(AnySceneStatusV1* s){s->ready=ready;return true;}
static uint64_t forbidden(const AnyPostPassV1*){++post_calls;assert(false&&"AnyGraphics must never register post FX");return 0;}
static const AnySceneControlsV1 scene{sizeof(AnySceneControlsV1),1,set,status};
static const AnySceneControlsV2 scene_v2{sizeof(AnySceneControlsV2),2,set_v2,status};
static AnySceneParametersV3 look_policy;static unsigned look_sets{};
static bool set_v3(const AnySceneParametersV3* p){look_policy=*p;++look_sets;return set_v2(&p->detailed);}
static const AnySceneControlsV3 scene_v3{sizeof(AnySceneControlsV3),3,set_v3,status};
static const AnyPostProcessV1 post{sizeof(AnyPostProcessV1),1,forbidden,nullptr,nullptr};
// AnyAPI 0.40.2 services: each records what it got and forwards to the version below, so older checks keep working.
static AnySceneParametersV4 controls4_policy;static unsigned controls4_sets{};
static bool set_v4(const AnySceneParametersV4* p){controls4_policy=*p;++controls4_sets;return set_v3(&p->looked);}
static bool status_v2(AnySceneStatusV2* s){return status(&s->base);}
static const AnySceneControlsV4 controls4_api{sizeof(AnySceneControlsV4),4,set_v4,status,status_v2};
static AnySceneLightingParametersV4 lamps_policy;static unsigned lamps_sets{};static bool reject_v4=false;
static bool lighting_set_v4(const AnySceneLightingParametersV4* p){if(reject_v4)return false;lamps_policy=*p;++lamps_sets;return lighting_set_v3(&p->shaped);}
static bool lighting_status_v2(AnySceneLightingStatusV2* s){lighting_status(&s->base);s->lamps_drawn=3;s->lamp_budget_max=8;return true;}
static const AnySceneLightingV4 lamps_api{sizeof(AnySceneLightingV4),4,lighting_set_v4,lighting_status,lighting_status_v2};
static AnySceneAntialiasingParametersV3 aa3_policy;
static bool aa_set_v3(const AnySceneAntialiasingParametersV3* p){aa3_policy=*p;return aa_set_v2(&p->base);}
static bool aa_status_v2(AnySceneAntialiasingStatusV2* s){return aa_status(&s->base);}
static const AnySceneAntialiasingV3 aa3_api{sizeof(AnySceneAntialiasingV3),3,aa_set_v3,aa_status,aa_status_v2};
static unsigned timing2_reads{};
static bool timing_copy_v2(AnySceneTimingV2* t){++timing2_reads;timing_copy(&t->v1);t->frame_ms_p50=16.6f;t->frame_ms_p95=20;for(auto& ms:t->pass_ms)ms=-1;t->pass_ms[ANY_PASS_AO]=.5f;t->pass_ms[ANY_PASS_VOLUMETRIC]=1.25f;t->pass_ms[ANY_PASS_AA]=.5f;t->pass_ms[ANY_PASS_OVERLAY]=.2f;t->anyapi_cpu_ms=.3f;t->native_reads=2;return true;}
static const AnySceneTimingServiceV2 timing2_api{sizeof(AnySceneTimingServiceV2),2,timing_copy,timing_copy_v2};
static AnySceneEffectsParametersV3 fx3_policy;static unsigned fx3_sets{};
static bool effects_set_v3(const AnySceneEffectsParametersV3* p){fx3_policy=*p;++fx3_sets;return effects_set_v2(&p->water);}
static bool effects_status_v3(AnySceneEffectsStatusV3* s){return effects_status_v2(&s->base);}
static const AnySceneEffectsV3 effects3_api{sizeof(AnySceneEffectsV3),3,effects_set_v3,effects_status,effects_status_v2,effects_status_v3};
static uint32_t gpu_vendor=0x10de,gpu_beams=1,gpu_shadows=1;
static bool gpu_copy(AnyGpuInfoV1* g){g->known=1;g->vendor_id=gpu_vendor;g->lamp_beams_supported=gpu_beams;return true;}
static bool gpu_copy_v2(AnyGpuInfoV2* g){gpu_copy(&g->v1);g->lamp_shadows_supported=gpu_shadows;return true;}
static const AnyGpuInfoServiceV1 gpu1_api{sizeof(AnyGpuInfoServiceV1),1,gpu_copy};
static const AnyGpuInfoServiceV2 gpu2_api{sizeof(AnyGpuInfoServiceV2),2,gpu_copy,gpu_copy_v2};
static std::string logs;
static void log(uint32_t,const char*,const char* text){std::cout<<text<<'\n';logs+=text;logs+='\n';}
static bool has(const char* text){return logs.find(text)!=std::string::npos;}
static size_t count(const char* text){size_t n=0;for(auto at=logs.find(text);at!=std::string::npos;at=logs.find(text,at+1))++n;return n;}
static std::string hex(const std::string& text){static const char* digits="0123456789abcdef";std::string out;for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;}
static std::string read_file(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});}

// The settings file keys, kinds, initial values and ranges (group, id, kind, initial, minimum, maximum). Renaming a key
// or changing its kind loses every user's saved value, so this list only grows.
static const char* const golden[]={
 "general\tenabled\t1\t1\t0\t1","general\tgameplay_only\t1\t1\t0\t1","general\tquality_profile\t4\t3\t0\t12","general\tadvanced\t1\t0\t0\t1",
 "scene\tnative_aa\t4\t2\t0\t6","scene\tnative_bloom\t4\t2\t0\t2","bloom\tbloom_amount\t3\t0.1\t0\t2","bloom\tbloom_threshold\t3\t0.75\t0\t4",
 "scene\tnative_ssao\t4\t2\t0\t2","scene\tnative_shadows\t4\t2\t0\t2","scene\tnative_fog_blur\t4\t2\t0\t2","scene\tsun_light\t3\t1\t0\t3",
 "scene\tsky_light\t3\t1\t0\t3","scene\tambient_light\t3\t1\t0\t3","scene\tfog_density\t3\t0.65\t0\t3","scene\tscene_exposure\t3\t0\t-2\t2",
 "scene\taa_quality\t4\t2\t0\t3","scene\tclouds\t4\t0\t0\t2","scene\tgrass\t4\t0\t0\t2","scene\tfoliage\t4\t0\t0\t2",
 "lighting\tatmosphere\t4\t0\t0\t4","lighting\tsun_shafts\t4\t0\t0\t4","lighting\tlighting_quality\t4\t1\t0\t3","lighting\tground_fog\t4\t0\t0\t3",
 "lighting\tshaft_intensity\t3\t1\t0\t4","lighting\tbeam_focus\t4\t1\t0\t3","lighting\tlocal_beams\t4\t2\t0\t4","lighting\tlocal_intensity\t3\t1\t0\t4",
 "lighting\tlocal_budget\t4\t1\t0\t3","lighting\tshaft_clarity\t4\t1\t0\t3","lighting\tbeam_reach\t4\t1\t0\t3","lighting\tsun_response\t1\t1\t0\t1",
 "lighting\ttemporal_lighting\t1\t1\t0\t1","scene\tcrispness\t4\t0\t0\t3","general\tcost_readout\t1\t1\t0\t1","finish\ttone_mapping\t4\t0\t0\t4",
 "finish\tcolour_look\t4\t0\t0\t6","finish\tlook_strength\t4\t1\t0\t2","finish\teye_adaptation\t1\t1\t0\t1","finish\tbrightness\t3\t0\t-2\t2",
 "finish\tscene_bloom\t4\t2\t0\t3","finish\tsun_glare\t4\t2\t0\t3","finish\tvignette\t4\t0\t0\t3","scene\tdetailed_ao\t4\t0\t0\t3",
 "sky\tnight_sky\t4\t0\t0\t3","sky\tmoonlight\t4\t0\t0\t3","scene\tcontact_shadows\t4\t0\t0\t3","scene\tbounce_light\t4\t0\t0\t3",
 "scene\tdepth_of_field\t4\t0\t0\t3","scene\tmotion_blur\t4\t0\t0\t3","scene\tdraw_distance\t4\t0\t0\t3","sky\tsun_disc\t4\t0\t0\t3",
 "sky\tcloud_layer\t4\t0\t0\t3","scene\tfocus_blur\t3\t1\t0.25\t3","lighting\tlight_colour\t4\t0\t0\t3","sky\twind_strength\t4\t1\t0\t3",
 "sky\twater_reflections\t4\t0\t0\t3","sky\tsun_glint\t4\t0\t0\t1",
 // 0.33.0
 "sky\tmoon_source\t4\t0\t0\t1","lighting\tlight_follows_time\t1\t0\t0\t1","sky\tweather_response\t1\t0\t0\t1",
 "hidden\tamd_lamp_path\t4\t0\t0\t3","hidden\tcloud_model\t1\t1\t0\t1","hidden\tshading_revision\t1\t1\t0\t1","hidden\ttone_revision\t1\t0\t0\t1",
 "hidden\tbloom_falloff\t3\t1\t0.5\t1","hidden\ttaa_still_v2\t1\t1\t0\t1","hidden\ttaa_thin_rescue\t1\t0\t0\t1","hidden\tkeep_native_ssao\t1\t0\t0\t1",
 "hidden\tpreset_revision\t2\t1\t0\t1000","hidden\tsky_model\t1\t1\t0\t1","hidden\taa_before_overlays\t1\t1\t0\t1","hidden\ttaa_ocean\t1\t1\t0\t1",
 "hidden\toutput_dither\t1\t1\t0\t1"};

// K38: the preset table in docs/mods/graphics.md, generated from profile(). The test prints it and, when the docs are
// next to the source, requires the docs to hold exactly this text between the preset-table markers.
static std::string preset_table(){using namespace graphics_options;
 struct Row {Index i;const char* name;std::vector<const char*> labels;};
 const std::vector<const char*> off4={"Off","Low","Medium","High"},off5={"Off","Low","Medium","High","Ultra"};
 const std::vector<Row> rows={
  {NativeAA,"Antialiasing",{"Game","Off","FXAA","SMAA","Enhanced SMAA","TAA","TAA extra smooth"}},{AAQuality,"AA quality",{"Low","Medium","High","Ultra"}},{Sharpening,"Sharpening",off4},
  {NativeSSAO,"Game SSAO",{"Game","Off","On"}},{DetailedAO,"Detailed shading",off4},{ContactShadows,"Contact shadows",off4},{BounceLight,"Bounce light",off4},
  {Atmosphere,"Volumetric fog",off5},{SunShafts,"Sun shafts",off5},{ShaftClarity,"Shaft clarity",{"Clear","Light","Natural","Classic"}},{LightingQuality,"Volumetric quality",{"Low","Medium","High","Ultra"}},
  {LocalBeams,"Lamp beams",off5},{LocalBudget,"Lamp budget",{"2","4","6","8"}},{FogDensity,"Base fog",{"Off","Light","Reduced","Game","Thick"}},
  {ToneMapping,"Tone mapping",{"Off","Game curve","Filmic","Clean","Soft film"}},{ColourLook,"Colour look",{"None","Warm","Cool","Teal & orange","Moody","Vivid","Soft & warm"}},
  {LookStrength,"Look strength",{"Subtle","Normal","Strong"}},{LightColour,"Light colour",{"Game","Subtle","Normal","Strong"}},
  {SceneBloom,"Bloom",{"Off","Subtle","Normal","Strong"}},{SunGlare,"Sun glare",{"Off","Subtle","Normal","Strong"}},{Vignette,"Vignette",{"Off","Light","Medium","Strong"}},
  {NightSky,"Night sky",{"Off","Subtle","Normal","Bright"}},{Moonlight,"Moonlight",off4},{SunDisc,"Sun disc",{"Off","Soft","Normal","Bright"}},{CloudLayer,"Cloud layer",{"Off","Few","Some","Many"}},
  {WaterReflections,"Water reflections",off4},{SunGlint,"Sun glint",{"Off","On"}},{DrawDistance,"Object draw distance",{"Game","1.5x","2x","3x"}},
  {DepthOfField,"Depth of field",off4},{MotionBlur,"Motion blur",off4}};
 const int levels[]={1,2,3,4,5,6,NaturalProfile,VibrantProfile,MoodyProfile,ClearDayProfile,SoftWarmProfile};
 const char* names[]={"Performance","Low","Balanced","High","Ultra","Cinematic","Natural","Vibrant","Moody","Clear Day","Soft & warm"};
 std::ostringstream out;out<<"| Setting |";for(auto n:names)out<<' '<<n<<" |";out<<"\n| --- |";for(size_t c=0;c<std::size(names);++c)out<<" --- |";out<<'\n';
 for(auto& r:rows){out<<"| "<<r.name<<" |";for(int level:levels){auto v=profile(defaults(),level);size_t at=r.i==FogDensity?nearest(v[r.i],fog_levels):size_t(v[r.i]);assert(at<r.labels.size());out<<' '<<r.labels[at]<<" |";}out<<'\n';}
 return out.str();}

// Option-table checks that need no host: auto-pick decisions, the preset revision, preference rows and the palettes.
static void pure_tests(){using namespace graphics_options;
 auto window=[](float median,float p95,float pass){PickWindow w;w.median_ms=median;w.p95_ms=p95;w.pass_ms=pass;return w;};
 // Slow with AnyAPI's passes under 15% of the frame: the game or CPU sets the frame rate. Stop, never below Balanced.
 auto d=pick_decide(3,3,false,false,window(20,25,1.5f));assert(d.done&&d.tier==3&&d.reason==PickCpuBound);
 d=pick_decide(2,4,true,true,window(20,25,1));assert(d.done&&d.tier==3&&d.reason==PickCpuBound);
 d=pick_decide(1,1,false,false,window(30,40,1));assert(d.done&&d.tier==1&&d.reason==PickCpuBound);
 // Slow and GPU-bound: confirm once, then step down; Performance is the floor.
 d=pick_decide(4,4,false,false,window(20,25,8));assert(!d.done&&d.tier==4&&d.reason==PickConfirm);
 d=pick_decide(4,4,false,true,window(20,25,8));assert(!d.done&&d.tier==3&&d.reason==PickSlow);
 d=pick_decide(4,4,false,false,window(15,30,8));assert(d.reason==PickConfirm); // slowest 5% under 40 fps counts too
 d=pick_decide(1,4,true,true,window(30,40,8));assert(d.done&&d.tier==1&&d.reason==PickBottom);
 d=pick_decide(4,4,false,false,window(20,25,-1));assert(d.reason==PickConfirm); // unknown pass time is never CPU-bound
 {auto w=window(30,40,1);w.present_block_ms=.5f;d=pick_decide(3,3,false,false,w);assert(d.done&&d.reason==PickCpuBound); // timing v2: the game hardly waits in Present
  w.present_block_ms=12;d=pick_decide(3,3,false,false,w);assert(!d.done&&d.tier==3&&d.reason==PickConfirm); // GPU-bound (low-end iGPU): it waits for the GPU
  d=pick_decide(3,3,false,true,w);assert(!d.done&&d.tier==2&&d.reason==PickSlow);w.present_block_ms=-1;assert(pick_decide(3,3,false,false,w).reason==PickCpuBound);}
 // Fast: climb at 80 fps, hold below it, hold after stepping down, stop at Ultra.
 d=pick_decide(3,3,false,false,window(10,12,3));assert(!d.done&&d.tier==4&&d.reason==PickFast);
 d=pick_decide(3,3,false,false,window(14,16,3));assert(d.done&&d.tier==3&&d.reason==PickHolds);
 d=pick_decide(3,4,true,false,window(10,12,3));assert(d.done&&d.tier==3&&d.reason==PickHolds);
 d=pick_decide(5,5,false,false,window(10,12,3));assert(d.done&&d.tier==5&&d.reason==PickTop);
 // A 60 Hz cap: steady frames at 16.7 ms. Climb while the passes leave room, stop when they fill 30% of the frame.
 assert(pick_cap(window(16.667f,16.8f,3))>16&&pick_cap(window(8.33f,8.4f,1))>8&&!pick_cap(window(15,15.2f,1))&&!pick_cap(window(16.667f,19,1)));
 d=pick_decide(3,3,false,false,window(16.667f,16.8f,4));assert(!d.done&&d.tier==4&&d.reason==PickRoom);
 d=pick_decide(4,3,false,false,window(16.667f,16.8f,6));assert(d.done&&d.tier==4&&d.reason==PickCapFull);
 // K37: only the changed rows a committed preset owns move; Top rows, Custom and current settings stay.
 auto old=profile(defaults(),3);old[Sharpening]=0;old[FogDensity]=0;auto c=old;assert(restage(c,0)==1&&c[Sharpening]==1&&c[FogDensity]==0);
 c=old;assert(restage(c,1)==0&&c==old);c=old;c[Preset]=CustomProfile;auto before=c;assert(restage(c,0)==0&&c==before);c=old;c[Preset]=0;assert(restage(c,0)==0);
 old=profile(defaults(),4);old[LightingQuality]=3;old[LocalBudget]=3;old[WaterReflections]=0;old[SunGlint]=0;c=old;assert(restage(c,0)==4&&c[LightingQuality]==2&&c[LocalBudget]==2&&c[WaterReflections]==2&&c[SunGlint]==1);
 old=profile(defaults(),NaturalProfile);old[DetailedAO]=2;c=old;assert(restage(c,0)==1&&c[DetailedAO]==0);
 // AG-7, K47, Natural: the 0.33.0 tables.
 const float quality[]={0,0,1,2,2,3},budget[]={0,0,1,2,3,3},crisp[]={0,0,1,1,1,2};
 for(int t=1;t<=6;++t){auto v=profile(defaults(),t);assert(v[LightingQuality]==quality[t-1]&&v[LocalBudget]==budget[t-1]&&v[Sharpening]==crisp[t-1]&&v[FogDensity]==.65f);}
 assert(profile(defaults(),NaturalProfile)[DetailedAO]==0&&profile(defaults(),NaturalProfile)[LightingQuality]==1&&profile(defaults(),ClearDayProfile)[LightingQuality]==1);
 // AG-2: fresh installs and Reset get Balanced.
 assert(balanced()==profile(defaults(),3)&&balanced()[NativeAA]==5&&balanced()[Preset]==3&&balanced()[ToneMapping]==2);
 // Preference rows and hidden rows survive every preset; the hidden rows are never drawn.
 auto mine=defaults();mine[MoonSource]=1;mine[LightFollowsTime]=1;mine[WeatherResponse]=1;mine[CloudModel]=0;mine[BloomFalloff]=.7f;mine[TaaThinRescue]=1;mine[PresetRevision]=1;mine[SkyModel]=0;mine[OutputDither]=0;
 for(int level=1;level<=SoftWarmProfile;++level){auto v=profile(mine,level);for(auto i:{MoonSource,LightFollowsTime,WeatherResponse,CloudModel,BloomFalloff,TaaThinRescue,PresetRevision,SkyModel,OutputDither})assert(v[i]==mine[i]);}
 for(size_t i=0;i<Count;++i)assert(hidden(Index(i))==(i==BloomAmount||i==BloomThreshold||i==SceneExposure||i==Advanced||i>=AmdLampPath));
 // A look run by auto-pick at a lower tier never costs more than that tier.
 for(int look:{NaturalProfile,VibrantProfile,MoodyProfile,ClearDayProfile,SoftWarmProfile})for(size_t t=0;t<6;++t){auto tier=tier_values(defaults(),t),v=tier;apply_style(look,v);
  for(auto i:{Atmosphere,SunShafts,LocalBeams,LightingQuality,LocalBudget,DetailedAO,ContactShadows,BounceLight,WaterReflections,SunGlint,DrawDistance,DepthOfField,MotionBlur})assert(v[i]<=tier[i]);}
 // Light colour follows time: the design palettes scaled by the Light colour level, inside the service's .5..1.5.
 assert(palette(GoldenKey,2,2)==.78f&&std::abs(palette(GoldenKey,2,3)-.692f)<1e-5f&&palette(MoonKey,0,0)==1&&std::abs(palette(BlueShade,2,1)-1.075f)<1e-5f);
 for(uint32_t q=0;q<PaletteCount;++q)for(int ch=0;ch<3;++ch)for(int level=0;level<4;++level){float x=palette(Palette(q),ch,float(level));assert(x>=.5f&&x<=1.5f);}
 assert(aa_history_bias(6)==1&&aa_history_bias(5)==0&&moon_target*moonlight_levels[3]<=.03f);
 // The settings file layout.
 assert(std::size(golden)==Count);
 for(size_t i=0;i<Count;++i){std::string group=definitions[i].group;for(auto& ch:group)ch=char(std::tolower((unsigned char)ch));char line[160];snprintf(line,sizeof(line),"%s\t%s\t%u\t%g\t%g\t%g",group.c_str(),definitions[i].id,definitions[i].kind,definitions[i].initial,definitions[i].minimum,definitions[i].maximum);
  if(std::string(line)!=golden[i]){std::cerr<<"golden settings row "<<i<<": "<<line<<std::endl;assert(false);}}
 std::cout<<"PURE_TESTS ok"<<std::endl;}

// Runs this test again in another mode, with the same fixture and plugin paths, and requires it to pass.
static void run_child(wchar_t** argv,const wchar_t* mode){wchar_t self[MAX_PATH];GetModuleFileNameW(nullptr,self,MAX_PATH);
 std::wstring command=L"\""+std::wstring(self)+L"\" \""+argv[1]+L"\" \""+argv[2]+L"\" \""+argv[3]+L"\" "+mode;
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi{};
 assert(CreateProcessW(self,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi));WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 std::wcout<<L"CHILD "<<mode<<L" exit="<<code<<std::endl;assert(code==0);}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;using namespace graphics_options;assert(argc==4||argc==5);const std::wstring mode=argc==5?argv[4]:L"";
 // Modes: default (AnyAPI 0.28-era services), --local-lights (AnyAPI 0.40.2), --no-scene, and three the default mode runs
 // itself: --legacy (AnyAPI 0.40.1), --revision (settings saved by AnyGraphics 0.32/0.33 before preset_revision) and
 // --fresh (no settings file and no scene lighting).
 const bool local=mode==L"--local-lights",legacy=mode==L"--legacy",revision=mode==L"--revision",fresh=mode==L"--fresh",absent=mode==L"--no-scene";
 assert(mode.empty()||local||legacy||revision||fresh||absent);const bool current=local||revision,v040=current||legacy;
 if(mode.empty())pure_tests();
 auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto service=AnyAPI_Services();assert(service);assert(service->publish("anyapi.post_process",1,&post));if(!absent){assert(service->publish("anyapi.scene_controls",1,&scene));assert(service->publish("anyapi.scene_controls",2,&scene_v2));assert(service->publish("anyapi.scene_antialiasing",1,&aa_api));if(!fresh)assert(service->publish("anyapi.scene_lighting",1,&lighting_api));}auto graphics=LoadLibraryW(argv[3]);assert(graphics);
 if(v040){assert(service->publish("anyapi.scene_lighting",2,&local_api));assert(service->publish("anyapi.scene_lighting",3,&shaped_api));assert(service->publish("anyapi.scene_antialiasing",2,&aa2_api));assert(service->publish("anyapi.scene_timing",1,&timing_api));assert(service->publish("anyapi.scene_effects",1,&effects_api));assert(service->publish("anyapi.scene_controls",3,&scene_v3));assert(service->publish("anyapi.gpu_info",1,&gpu1_api));}
 // Scene effects v2 (water) is AnyAPI 0.40.2: --legacy (0.40.1) has effects v1 only.
 if(current){assert(service->publish("anyapi.scene_effects",2,&effects2_api));assert(service->publish("anyapi.scene_lighting",4,&lamps_api));assert(service->publish("anyapi.scene_antialiasing",3,&aa3_api));assert(service->publish("anyapi.scene_timing",2,&timing2_api));assert(service->publish("anyapi.scene_effects",3,&effects3_api));assert(service->publish("anyapi.scene_controls",4,&controls4_api));assert(service->publish("anyapi.gpu_info",2,&gpu2_api));}
 if(fresh)assert(service->publish("anyapi.scene_controls",3,&scene_v3));
 // Wine reuses process ids: a failed run's folder must not look like saved settings.
 auto root=std::filesystem::temp_directory_path()/(L"AnyGraphicsNative-"+std::to_wstring(GetCurrentProcessId()));{std::error_code stale;std::filesystem::remove_all(root,stale);}std::filesystem::create_directories(root/L"AnyHelpers");
 // Migration retains native keys, ignores removed post FX and does not load AnyHelpers. The default mode also saved TAA,
 // which scene AA v1 does not offer (AG-16).
 if(revision){std::filesystem::create_directories(root/L"AnyGraphics");std::ofstream own(root/L"AnyGraphics"/L"settings.tsv");auto row=[&](const char* key,int kind,const char* value){own<<key<<'\t'<<kind<<'\t'<<hex(value)<<'\n';};
  // High as AnyGraphics 0.32 saved it: 48-step volumetrics and 8 lamps, no water rows yet, and a fog the user picked.
  row("anygraphics.general\tquality_profile",4,"4");row("anygraphics.lighting\tlighting_quality",4,"3");row("anygraphics.lighting\tlocal_budget",4,"3");row("anygraphics.scene\tcrispness",4,"1");row("anygraphics.scene\tdetailed_ao",4,"2");row("anygraphics.scene\tfog_density",3,"0.35");
  // Hidden rows a tester set by hand.
  row("anygraphics.hidden\ttone_revision",1,"1");row("anygraphics.hidden\tbloom_falloff",3,"0.7");row("anygraphics.hidden\tcloud_model",1,"0");row("anygraphics.hidden\tkeep_native_ssao",1,"1");row("anygraphics.hidden\ttaa_thin_rescue",1,"1");row("anygraphics.hidden\tamd_lamp_path",4,"3");
  row("anygraphics.hidden\tsky_model",1,"0");row("anygraphics.hidden\taa_before_overlays",1,"0");row("anygraphics.hidden\ttaa_ocean",1,"0");row("anygraphics.hidden\toutput_dither",1,"0");}
 else if(!fresh){std::ofstream old(root/L"AnyHelpers"/L"settings.tsv");old<<"anygraphics.scene\tsun_light\t3\t312e35\nanygraphics.general\tcomparison\t4\t31\nanygraphics.sharpness\tsharpen\t1\t31\n";if(mode.empty())old<<"anygraphics.scene\tnative_aa\t4\t35\n";}
 auto path=root.wstring();AnyModHostV1 host;host.game_directory=path.c_str();host.plugin_directory=path.c_str();host.log=log;AnyModCallbacksV1 g;assert(((AnyModInitV1)GetProcAddress(graphics,"AnyAPI_ModInit"))(&host,&g));((void(*)())GetProcAddress(graphics,"AnyAPI_ModReady"))();assert(g.render);
 auto event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureGraphicsEvent");auto stage=(void(*)(uint64_t,double))GetProcAddress(fixture,"FixtureGraphicsStage");auto rows=(uint32_t(*)())GetProcAddress(fixture,"FixtureRows");auto dirty=(uint32_t(*)())GetProcAddress(fixture,"FixtureGraphicsDirty");auto title=(const char*(*)())GetProcAddress(fixture,"FixtureGraphicsTitle");auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");assert(event&&stage&&rows&&dirty&&title&&ui);assert(std::string(title()).find("Modded")!=std::string::npos);assert(!service->query("anyhelpers.settings",1));
 AnyFrameV1 frame;frame.tick=100;frame.focused=1;AnyCanvasV1 canvas;ui(1);auto render=[&](){frame.tick+=16;g.render(&frame,&canvas,nullptr);assert(!canvas.pixels&&post_calls==0);};auto apply=[&](Index i,double v){event(ANY_MENU_OPEN);stage(i+1,v);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);render();};render();
 assert(has(("SETTINGS_REGISTERED count="+std::to_string(Count)).c_str())||absent);
 if(absent){assert(!submissions);event(ANY_MENU_OPEN);stage(0,0);assert(rows()>3);event(ANY_MENU_CANCEL);}
 else if(local){
 assert(lighting_policy.enabled&&local_policy.local_strength==1&&local_policy.local_budget==4);
 assert(has("SERVICES controls=4 lighting=4 effects=3 aa=3 timing=2 gpu_info=2 lamp_budget_max=8")&&has("PRESET_REVISION from=0 to=1 preset=3 rows_changed=0 saved=1"));
 // abi-1: the revision is written even though no row moved, so the next launch does not run the migration again.
 assert(read_file(root/L"AnyGraphics"/L"settings.tsv").find("anygraphics.hidden\tpreset_revision\t2\t31\n")!=std::string::npos);
 // Lighting v4: the lamp budget, the sea plane and the default lamp flags; the hidden rows send the 0.40.2 defaults.
 assert(lamps_sets&&lamps_policy.lamp_budget==4&&lamps_policy.atmosphere_flags==ANY_ATMOS_SEA_PLANE&&lamps_policy.flags==15&&lamps_policy.weather_response==0&&lamps_policy.amd_path==0&&lamps_policy.lamp_model==0);
 assert(fx3_sets&&fx3_policy.cloud_model==1&&fx3_policy.shading_revision==1&&fx3_policy.tone_revision==0&&fx3_policy.bloom_falloff==1&&fx3_policy.flags==0&&fx3_policy.weather==0&&fx3_policy.sky_model==1&&fx3_policy.dither==1);
 assert(aa3_policy.taa_flags==(ANY_TAA_CATMULL_ROM|ANY_TAA_STILL_V2)&&aa3_policy.history_bias==0&&aa3_policy.flags==0&&aa3_policy.dither==1);
 assert(controls4_sets&&controls4_policy.tint_targets==1&&!controls4_policy.time_shaped&&!controls4_policy.moon_mode&&controls4_policy.moonlight==0&&!controls4_policy.weather_fog_keep&&controls4_policy.shadow_split==0);
 // Presets keep the fine-tuning groups closed; choosing Custom opens them all so each row can be set.
 apply(Preset,CustomProfile);
 apply(Atmosphere,0);apply(SunShafts,0);assert(lighting_policy.enabled&&lighting_policy.fog_strength==0&&lighting_policy.sun_shafts==0);
 apply(LocalBeams,4);apply(LocalIntensity,3);apply(LocalBudget,3);assert(local_policy.local_strength==9&&local_policy.local_budget==8);
 event(ANY_MENU_OPEN);stage(LocalIntensity+1,1);render();assert(local_policy.local_strength==9);event(ANY_MENU_CANCEL);render();assert(local_policy.local_strength==9);
 apply(LocalIntensity,0);assert(!lighting_policy.enabled);apply(LocalIntensity,1);assert(lighting_policy.enabled);
 apply(LocalBeams,0);assert(!lighting_policy.enabled);apply(LocalBeams,2);apply(Preset,0);assert(!lighting_policy.enabled);apply(Preset,3);assert(lighting_policy.enabled);
 // Exercise real menu selection and submitted policy, not just the profile helper.
 // Presets 1-6: Performance, Low, Medium, High, Ultra, Cinematic.
 const float densities[]={.002f,.002f,.001f,.001f,.001f,.001f},sun_strength[]={0,.5f,1,2,3,4.5f},local_strength[]={0,.5f,1,1,2,2.4f};
 // AG-7: volumetric quality and the lamp budget have their own tables; K47: Balanced TAA is sharpened.
 const uint32_t quality[]={0,0,1,2,2,3},budgets[]={2,2,4,6,8,8};
 const float clarity[]={.85f,.85f,.85f,.85f,1,1},reach[]={60,60,120,120,250,250},sharpening[]={0,0,.3f,.3f,.3f,.55f};const uint32_t methods[]={1,1,4,4,4,4};
 for(int preset=1;preset<=6;++preset){apply(Preset,preset);auto i=preset-1;std::cout<<"PRESET "<<preset<<" enabled="<<lighting_policy.enabled<<" density="<<lighting_policy.fog_density<<" sun="<<lighting_policy.sun_shafts<<" local="<<local_policy.local_strength<<" quality="<<lighting_policy.quality<<" budget="<<local_policy.local_budget<<" height="<<lighting_policy.height_falloff<<std::endl;assert(lighting_policy.enabled==(preset>1)&&lighting_policy.fog_density==densities[i]&&std::abs(lighting_policy.sun_shafts-sun_strength[i])<.001f&&std::abs(local_policy.local_strength-local_strength[i])<.001f&&lighting_policy.quality==quality[i]&&local_policy.local_budget==budgets[i]&&lighting_policy.height_falloff==0);
  assert(shaped_policy.clarity==clarity[i]&&shaped_policy.beam_reach==reach[i]&&shaped_policy.sun_response==1&&shaped_policy.temporal==1);
  assert(aa2_policy.enabled==(preset>1)&&(preset==1||aa2_policy.method==methods[i])&&std::abs(aa2_policy.sharpening-sharpening[i])<.001f);
  // Scene AA v3: Extra smooth (Ultra, Cinematic) adds a history level instead of raising the AA quality.
  const uint32_t aa_quality[]={1,1,2,2,3,3};assert(aa3_policy.base.quality==aa_quality[i]&&aa3_policy.history_bias==(preset>=5?1u:0u)&&lamps_policy.lamp_budget==budgets[i]);
  // Finishing: Filmic from Low up; Vivid from Ultra; bloom and glare grow with the preset.
  const float scene_bloom[]={0,.25f,.25f,.45f,.7f,.7f};const uint32_t looks[]={0,0,0,5,5,3};
  assert(effects_policy.enabled==(preset>1)&&(preset==1||effects_policy.tonemap==1)&&(preset==1||effects_policy.look==looks[i])&&(preset==1||std::abs(effects_policy.bloom-scene_bloom[i])<.001f)&&effects_policy.eye_adaptation==1);
  // Detailed shading from Medium up; Performance and Low keep the game's SSAO.
  const float ao_radius[]={1,1,1,1,1.4f,1.4f};assert(effects_policy.ambient_occlusion==(preset>=4)&&effects_policy.finish==(preset>1)&&effects_policy.ao_radius==ao_radius[i]);
  // Night sky and moonlight from Low up; Cinematic has the brightest stars.
  const float stars[]={0,.6f,1,1,1.5f,1.5f};assert(effects_policy.night_sky==(preset>1)&&effects_policy.stars==stars[i]&&effects_policy.moonlight==(preset>2?1.f:preset==2?.6f:0.f));
  const float contact[]={.6f,.6f,.3f,.6f,1,1};assert(effects_policy.contact_shadows==(preset>=3)&&effects_policy.contact_length==contact[i]);
  const float bounce[]={0,0,0,1,1.5f,1.5f};assert(effects_policy.bounce_light==bounce[i]);
  // Depth of field and motion blur are Cinematic only.
  assert(effects_policy.depth_of_field==(preset==6?1.f:0.f)&&effects_policy.motion_blur==(preset==6?.4f:0.f));
  const float reach[]={1,1,1,1.5f,2,3};assert(effects_policy.draw_distance==reach[i]);
  const float sun[]={0,.6f,1,1,1.5f,1.5f},cloud[]={0,0,.6f,1,1.5f,1.5f};assert(effects_policy.sun_disc==sun[i]&&effects_policy.clouds==cloud[i]);
  // Water reflections and glint from High up (Medium on High, High on Ultra and Cinematic); off below.
  const float water[]={0,0,0,1,1,1};const uint32_t water_quality[]={1,1,1,1,2,2};assert(water_sets&&water_policy.water_reflections==water[i]&&water_policy.sun_glint==water[i]&&water_policy.reflection_quality==water_quality[i]);}
 apply(Preset,4);assert(water_policy.water_reflections==1&&water_policy.sun_glint==1&&effects_policy.enabled);apply(Preset,1);assert(water_policy.water_reflections==0&&water_policy.sun_glint==0);
 // Looks: Natural keeps the game's colours, Vibrant is the shader-pack look, Moody a darker grade; all keep fog light.
 apply(Preset,NaturalProfile);assert(effects_policy.tonemap==2&&effects_policy.look==0&&effects_policy.vignette==0&&lighting_policy.fog_strength==0&&!effects_policy.ambient_occlusion&&lighting_policy.quality==1);
 apply(Preset,VibrantProfile);assert(water_policy.water_reflections==1&&water_policy.reflection_quality==2);assert(effects_policy.look==5&&effects_policy.look_strength==1&&effects_policy.clouds==1.5f&&effects_policy.sun_disc==1.5f&&lighting_policy.fog_density==.001f&&shaped_policy.clarity==1);
 apply(Preset,MoodyProfile);assert(effects_policy.look==1&&effects_policy.look_strength==1&&effects_policy.vignette==.5f&&lighting_policy.fog_density==.001f&&std::abs(lighting_policy.sun_shafts-2)<.001f);
 // Presets show the top rows and closed groups; Custom keeps the current values and opens every group.
 event(ANY_MENU_OPEN);stage(0,0);auto preset_rows=rows();event(ANY_MENU_CANCEL);
 apply(Preset,ClearDayProfile);assert(effects_policy.tonemap==2&&effects_policy.look==0&&lighting_policy.fog_strength==0&&std::abs(aa2_policy.sharpening-.55f)<.001f);
 apply(Preset,SoftWarmProfile);assert(effects_policy.tonemap==3&&effects_policy.look==6&&!effects_policy.ambient_occlusion&&water_policy.water_reflections==1&&water_policy.reflection_quality==1&&water_policy.sun_glint==1);if(local){assert(look_sets&&look_policy.key_tint[0]==1.08f&&look_policy.shade_tint[2]==1.1f&&look_policy.wind_scale==1);apply(Preset,CustomProfile);apply(WindStrength,3);assert(look_policy.wind_scale==2.5f);apply(LightColour,0);assert(look_policy.key_tint[0]==1);}
 apply(Preset,MoodyProfile);apply(Preset,CustomProfile);assert(effects_policy.look==1&&effects_policy.vignette==.5f);event(ANY_MENU_OPEN);stage(0,0);std::cout<<"ROWS preset="<<preset_rows<<" custom="<<rows()<<std::endl;assert(rows()>preset_rows+15);event(ANY_MENU_CANCEL);
 // Shaping rows submit their own values; the GPU cost readout polls timing while the menu draws.
 apply(Preset,3);apply(Preset,CustomProfile);apply(ShaftClarity,3);apply(BeamReach,3);apply(SunResponse,0);apply(TemporalLighting,0);assert(shaped_policy.clarity==0&&shaped_policy.beam_reach==0&&!shaped_policy.sun_response&&!shaped_policy.temporal);
 // Classic shaft clarity caps the sun phase (lighting v4).
 assert(lamps_policy.atmosphere_flags==(ANY_ATMOS_SEA_PLANE|ANY_ATMOS_PHASE_CAP));
 apply(NativeAA,5);apply(Sharpening,3);apply(AAQuality,1);assert(aa2_policy.enabled&&aa2_policy.method==4&&aa2_policy.quality==1&&std::abs(aa2_policy.sharpening-.8f)<.001f);apply(NativeAA,6);assert(aa2_policy.method==4&&aa2_policy.quality==1&&aa3_policy.history_bias==1);apply(NativeAA,2);assert(!aa2_policy.enabled);
 event(ANY_MENU_OPEN);stage(0,0);assert(timing2_reads>0&&effects_reads>0&&water_reads>0);event(ANY_MENU_CANCEL);
 // Water rows: Low, Medium and High are ray qualities 0-2 at full strength; the glint is a separate switch.
 apply(WaterReflections,1);assert(water_policy.water_reflections==1&&water_policy.reflection_quality==0&&water_policy.sun_glint==0&&effects_policy.enabled);apply(SunGlint,1);assert(water_policy.sun_glint==1);
 apply(WaterReflections,3);assert(water_policy.reflection_quality==2);apply(WaterReflections,0);assert(water_policy.water_reflections==0&&water_policy.sun_glint==1);apply(SunGlint,0);assert(water_policy.sun_glint==0);
 // Moonlight source Game light (K49): the moon becomes the game's night light, so the screen-space moonlight goes off.
 apply(Moonlight,2);apply(MoonSource,1);assert(controls4_policy.moon_mode==1&&std::abs(controls4_policy.moonlight-.012f)<1e-6f&&effects_policy.moonlight==0&&fx3_policy.moon_disc_follows_light==1);
 apply(MoonSource,0);assert(controls4_policy.moon_mode==0&&controls4_policy.moonlight==0&&effects_policy.moonlight==1&&!fx3_policy.moon_disc_follows_light);
 // Light colour follows time (K17): the palettes follow the Light colour level; the row shows only with a Light colour.
 apply(LightColour,2);apply(LightFollowsTime,1);assert(controls4_policy.time_shaped==1&&controls4_policy.golden_key[2]==.78f&&controls4_policy.noon_key[0]==1.02f&&controls4_policy.moon_key[2]==1.2f);
 apply(LightColour,3);assert(std::abs(controls4_policy.golden_key[2]-.692f)<1e-5f);apply(LightColour,0);assert(!controls4_policy.time_shaped);event(ANY_MENU_OPEN);stage(LightFollowsTime+1,0);assert(!dirty());event(ANY_MENU_CANCEL);
 // Weather response (K18): every service that responds to the weather gets it; nothing else changes.
 apply(WeatherResponse,1);assert(fx3_policy.weather==1&&lamps_policy.weather_response==1&&lamps_policy.atmosphere_flags==(ANY_ATMOS_SEA_PLANE|ANY_ATMOS_WEATHER_DENSITY|ANY_ATMOS_PHASE_CAP|ANY_ATMOS_FLASH_GLOW)&&controls4_policy.weather_fog_keep==1);
 apply(WeatherResponse,0);assert(fx3_policy.weather==0&&lamps_policy.weather_response==0&&lamps_policy.atmosphere_flags==(ANY_ATMOS_SEA_PLANE|ANY_ATMOS_PHASE_CAP)&&!controls4_policy.weather_fog_keep);
 // AG-3: hidden rows are never drawn, so the menu cannot stage them.
 event(ANY_MENU_OPEN);for(auto i:{BloomAmount,BloomThreshold,SceneExposure,Advanced,CloudModel,ToneRevision,KeepNativeSsao,PresetRevision})stage(i+1,0);assert(!dirty());event(ANY_MENU_CANCEL);
 // K25 and K11: one more row each for the sun shafts note (game shadows off) and the AMD lamp shadow note.
 event(ANY_MENU_OPEN);stage(NativeShadows+1,2);stage(0,0);auto plain=rows();stage(NativeShadows+1,1);stage(0,0);assert(rows()==plain+1);event(ANY_MENU_CANCEL);
 apply(LocalBeams,2);event(ANY_MENU_OPEN);stage(0,0);plain=rows();gpu_shadows=0;stage(0,0);assert(rows()==plain+1);gpu_shadows=1;event(ANY_MENU_CANCEL);
 // A rejected v4 lighting call falls back to v3 and logs once.
 reject_v4=true;apply(LocalBudget,2);apply(LocalBudget,1);assert(local_policy.local_budget==4&&count("v4_rejected=1 fallback=v3")==1);reject_v4=false;apply(LocalBudget,2);assert(lamps_policy.lamp_budget==6);
 apply(ToneMapping,3);apply(ColourLook,3);apply(LookStrength,0);apply(SunGlare,0);apply(Vignette,3);apply(Brightness,1);
 assert(effects_policy.enabled&&effects_policy.tonemap==2&&effects_policy.look==3&&effects_policy.look_strength==.5f&&effects_policy.glare==0&&effects_policy.vignette==.8f&&effects_policy.exposure==1);
 apply(DetailedAO,1);apply(ToneMapping,0);assert(effects_policy.enabled&&!effects_policy.finish&&effects_policy.ambient_occlusion);
 apply(NativeSSAO,1);assert(!effects_policy.ambient_occlusion);apply(NativeSSAO,2);apply(DetailedAO,0);assert(effects_policy.enabled&&effects_policy.night_sky);apply(NightSky,0);assert(effects_policy.night_sky&&effects_policy.stars==0&&effects_policy.moonlight==1);apply(Moonlight,0);assert(effects_policy.enabled&&effects_policy.contact_shadows);apply(ContactShadows,0);apply(BounceLight,0);apply(DepthOfField,3);assert(effects_policy.enabled&&effects_policy.depth_of_field==1.6f);apply(FocusBlur,2);assert(std::abs(effects_policy.depth_of_field-3.2f)<.001f);apply(FocusBlur,3);assert(effects_policy.depth_of_field==4);apply(FocusBlur,1);apply(MotionBlur,2);assert(effects_policy.motion_blur==.8f);apply(DepthOfField,0);apply(MotionBlur,0);apply(DrawDistance,3);assert(effects_policy.enabled&&effects_policy.draw_distance==3);apply(DrawDistance,0);apply(SunDisc,0);apply(CloudLayer,3);assert(effects_policy.enabled&&effects_policy.clouds==1.5f);apply(CloudLayer,0);assert(effects_policy.draw_distance==1&&effects_policy.sun_disc==0&&effects_policy.clouds==0&&effects_policy.bounce_light==0&&effects_policy.depth_of_field==0&&effects_policy.motion_blur==0&&!effects_policy.enabled&&!effects_policy.night_sky&&!effects_policy.contact_shadows);
 apply(Preset,6);
 event(ANY_MENU_OPEN);stage(Preset+1,1);render();assert(std::abs(local_policy.local_strength-2.4f)<.001f);event(ANY_MENU_CANCEL);render();assert(std::abs(local_policy.local_strength-2.4f)<.001f);
 // SC-18: a photo-key tap between two frames still toggles photo mode; a held key toggles once.
 {AnyInputV1 down;down.kind=ANY_KEY_DOWN;down.key=VK_F1;AnyInputV1 up=down;up.kind=ANY_KEY_UP;frame.tick+=1000;
  g.input(&down,nullptr);g.input(&up,nullptr);render();assert(has("PHOTO_MODE on=1")&&effects_policy.hide_ui);
  frame.tick+=1000;g.input(&down,nullptr);g.input(&down,nullptr);render();g.input(&down,nullptr);render();g.input(&up,nullptr);render();assert(has("PHOTO_MODE on=0")&&!effects_policy.hide_ui&&count("PHOTO_MODE on=")==2);}
 // K34: in a menu with Gameplay only, the readout shows the last gameplay numbers instead of reading the timers.
 apply(Enabled,0);assert(!lighting_policy.enabled);apply(Enabled,1);ui(13);render();assert(!lighting_policy.enabled);
 {auto reads=timing2_reads;event(ANY_MENU_OPEN);stage(0,0);assert(timing2_reads==reads);event(ANY_MENU_CANCEL);}
 apply(GameplayOnly,0);assert(lighting_policy.enabled);
 }else if(legacy){
 // AnyAPI 0.40.1: the 0.33.0 rows stay hidden and Extra smooth raises the AA quality one level (scene AA v2).
 assert(has("SERVICES controls=3 lighting=3 effects=1 aa=2 timing=1 gpu_info=1 lamp_budget_max=8")&&!lamps_sets&&!fx3_sets&&!controls4_sets);
 apply(Preset,CustomProfile);apply(NativeAA,6);apply(AAQuality,1);assert(aa2_policy.method==4&&aa2_policy.quality==2);
 // Scene effects v1: the v1 set carries the policy, and the water rows (effects v2) stay hidden.
 assert(effects_sets>0&&!water_sets&&effects_policy.enabled);event(ANY_MENU_OPEN);stage(WaterReflections+1,1);stage(SunGlint+1,1);assert(!dirty());event(ANY_MENU_CANCEL);
 apply(LightColour,2);apply(Moonlight,2);event(ANY_MENU_OPEN);stage(MoonSource+1,1);stage(LightFollowsTime+1,1);stage(WeatherResponse+1,1);assert(!dirty());event(ANY_MENU_CANCEL);
 // K11 with gpu_info v1: AMD on AnyAPI 0.39.2-0.40.1 runs lamp beams without shadows, one more row.
 apply(LocalBeams,2);event(ANY_MENU_OPEN);stage(0,0);auto plain=rows();gpu_vendor=0x1002;stage(0,0);assert(rows()==plain+1);event(ANY_MENU_CANCEL);
 // AnyAPI 0.39.1 on AMD switched lamp beams off: the old notice replaces the new one and the lamp rows hide.
 gpu_beams=0;event(ANY_MENU_OPEN);stage(0,0);assert(rows()<plain);event(ANY_MENU_CANCEL);gpu_beams=1;gpu_vendor=0x10de;
 apply(LocalBudget,2);assert(local_policy.local_budget==6&&shaped_policy.lighting.local_budget==6);
 }else if(revision){
 // K37: the committed High moves to the 0.33.0 tables once; the user's fog stays.
 assert(has("PRESET_REVISION from=0 to=1 preset=4 rows_changed=4 saved=1"));
 assert(lighting_policy.quality==2&&lamps_policy.lamp_budget==6&&water_policy.water_reflections==1&&water_policy.reflection_quality==1&&water_policy.sun_glint==1&&policy.fog==.35f&&aa3_policy.base.sharpening==.3f);
 // Hidden rows reach the services.
 assert(fx3_policy.tone_revision==1&&std::abs(fx3_policy.bloom_falloff-.7f)<1e-6f&&fx3_policy.cloud_model==0&&fx3_policy.flags==ANY_FX_KEEP_NATIVE_SSAO&&aa3_policy.taa_flags==7&&lamps_policy.amd_path==3);
 assert(fx3_policy.sky_model==0&&fx3_policy.dither==0&&aa3_policy.flags==(ANY_AA_AFTER_OVERLAYS|ANY_AA_OCEAN_DEPTH)&&aa3_policy.dither==0);
 auto text=read_file(root/L"AnyGraphics"/L"settings.tsv");assert(text.find("anygraphics.hidden\tpreset_revision\t2\t31\n")!=std::string::npos&&text.find("anygraphics.sky\twater_reflections\t4\t32\n")!=std::string::npos&&text.find("anygraphics.scene\tfog_density\t3\t"+hex("0.34999999403953552"))!=std::string::npos);
 // A second start finds the revision and changes nothing.
 FreeLibrary(graphics);logs.clear();graphics=LoadLibraryW(argv[3]);assert(graphics);assert(((AnyModInitV1)GetProcAddress(graphics,"AnyAPI_ModInit"))(&host,&g));((void(*)())GetProcAddress(graphics,"AnyAPI_ModReady"))();
 assert(has("SERVICES")&&!has("PRESET_REVISION")&&read_file(root/L"AnyGraphics"/L"settings.tsv")==text);
 std::filesystem::remove_all(root);std::cout<<"PASS: preset revision"<<std::endl;ExitProcess(0);
 }else if(fresh){
 // AG-2: a fresh install gets Balanced (scene AA, here Enhanced SMAA on scene AA v1) without writing a file.
 assert(!std::filesystem::exists(root/L"AnyGraphics"/L"settings.tsv")&&!has("PRESET_REVISION")&&policy.enabled&&policy.aa==0&&aa_policy.enabled&&aa_policy.method==2);
 // AG-14: without scene lighting the Lighting group still holds Sunlight, Sky light, Ambient light and Light colour.
 apply(Preset,CustomProfile);apply(SunLight,3);assert(policy.sun==1.15f);apply(LightColour,2);assert(look_policy.key_tint[0]==1.08f);
 // Reset brings Balanced back.
 apply(NativeAA,1);assert(!aa_policy.enabled&&policy.aa==1);event(ANY_MENU_OPEN);stage(0,0);event(ANY_MENU_RESET);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);render();assert(aa_policy.enabled&&aa_policy.method==2&&policy.aa==0&&policy.sun==1);
 }else {
 assert(policy.enabled&&policy.sun==1.3f&&policy.aa==0&&aa_policy.enabled&&aa_policy.method==2);apply(Preset,CustomProfile);assert(policy.sun==1.3f);apply(SunLight,3);assert(policy.sun==1.15f);
 apply(Clouds,1);apply(Grass,2);apply(Foliage,1);assert(detail_policy.clouds==1&&detail_policy.grass==2&&detail_policy.foliage==1);
 event(ANY_MENU_OPEN);stage(Clouds+1,2);render();assert(detail_policy.clouds==1);event(ANY_MENU_CANCEL);render();assert(detail_policy.clouds==1);
 event(ANY_MENU_OPEN);stage(SunLight+1,4);assert(dirty());render();assert(policy.sun==1.15f);event(ANY_MENU_CANCEL);render();assert(policy.sun==1.15f);
 apply(NativeAA,3);assert(aa_policy.enabled&&policy.aa==0&&aa_policy.quality==2);apply(AAQuality,3);assert(aa_policy.quality==3);
 apply(NativeAA,4);assert(aa_policy.enabled&&aa_policy.method==2&&policy.aa==0);
 apply(NativeAA,1);assert(!aa_policy.enabled);apply(NativeBloom,5);assert(policy.aa==1&&policy.bloom==2&&policy.bloom_intensity==.35f);
 apply(NativeSSAO,1);apply(NativeShadows,2);apply(NativeFogBlur,1);apply(FogDensity,1);assert(policy.ssao==1&&policy.shadows==2&&policy.fog_blur==1&&policy.fog==.35f&&policy.light_exposure==0);
 for(int preset=1;preset<=6;++preset){apply(Preset,preset);auto expected=profile(defaults(),preset);assert(policy.enabled&&policy.sun==1&&policy.fog==expected[FogDensity]&&policy.ssao==uint32_t(expected[NativeSSAO])&&policy.bloom_intensity==expected[BloomAmount]);assert(detail_policy.clouds==uint32_t(expected[Clouds])&&detail_policy.grass==uint32_t(expected[Grass])&&detail_policy.foliage==0);event(ANY_MENU_OPEN);stage(0,0);assert(rows()>3);event(ANY_MENU_CANCEL);}
 assert(lighting_submissions&&lighting_policy.enabled);apply(Preset,CustomProfile);apply(Atmosphere,0);apply(SunShafts,0);assert(!lighting_policy.enabled);
 apply(Atmosphere,3);apply(SunShafts,2);apply(ShaftIntensity,1);apply(BeamFocus,1);apply(LightingQuality,3);apply(GroundFog,2);
 assert(lighting_policy.enabled&&lighting_policy.fog_density==.004f&&lighting_policy.sun_shafts==1&&lighting_policy.quality==3&&lighting_policy.height_falloff==.025f);
 event(ANY_MENU_OPEN);stage(Atmosphere+1,4);render();assert(lighting_policy.fog_density==.004f);event(ANY_MENU_CANCEL);render();assert(lighting_policy.fog_density==.004f);
 apply(Atmosphere,0);assert(lighting_policy.enabled&&lighting_policy.fog_strength==0&&lighting_policy.sun_shafts==1);
 apply(ShaftIntensity,2);assert(lighting_policy.sun_shafts==2&&lighting_policy.fog_density==.002f);
 apply(BeamFocus,2);assert(lighting_policy.anisotropy==.55f);
 event(ANY_MENU_OPEN);stage(ShaftIntensity+1,4);render();assert(lighting_policy.sun_shafts==2);event(ANY_MENU_CANCEL);render();assert(lighting_policy.sun_shafts==2);
 apply(ShaftIntensity,0);assert(!lighting_policy.enabled);apply(ShaftIntensity,1);assert(lighting_policy.enabled&&lighting_policy.sun_shafts==1);
 apply(SunShafts,4);apply(ShaftIntensity,4);assert(lighting_policy.sun_shafts==12);apply(SunShafts,0);assert(!lighting_policy.enabled);
 apply(Atmosphere,2);apply(Preset,0);assert(!policy.enabled&&!aa_policy.enabled&&!lighting_policy.enabled);apply(Preset,3);assert(policy.enabled);apply(Preset,CustomProfile);
 for(int level=0;level<5;++level){apply(FogDensity,level);assert(policy.fog==fog_levels[level]);}
 for(int level=2;level<6;++level){apply(NativeBloom,level);assert(policy.bloom==2&&policy.bloom_intensity==bloom_levels[level-2]);}
 event(ANY_MENU_OPEN);stage(0,0);event(ANY_MENU_RESET);assert(dirty());render();assert(policy.fog==1.5f);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);render();assert(policy.sun==1&&policy.aa==0&&aa_policy.enabled&&aa_policy.method==2&&policy.bloom==2&&policy.fog==.65f);
 apply(Preset,CustomProfile);apply(Atmosphere,2);apply(Enabled,0);assert(!policy.enabled&&!lighting_policy.enabled);apply(Enabled,1);ui(13);render();assert(!policy.enabled&&!lighting_policy.enabled);apply(GameplayOnly,0);assert(policy.enabled);ready=false;render();ready=true;render();assert(policy.enabled);
 reject_scene=true;ready=false;render();auto rejected_count=submissions;render();assert(submissions==rejected_count);reject_scene=false;frame.tick+=501;render();assert(submissions==rejected_count+1);render();assert(submissions==rejected_count+1);

 }
 {std::ifstream saved(root/L"AnyGraphics"/L"settings.tsv");std::string text((std::istreambuf_iterator<char>(saved)),{});assert(text.find("comparison")==std::string::npos&&text.find("sharpen")==std::string::npos);
  // Every row is saved under its golden key, and nothing else.
  if(mode.empty()){std::set<std::string> keys;std::istringstream lines(text);for(std::string line;std::getline(lines,line);){auto tab=line.find('\t',line.find('\t')+1);keys.insert(line.substr(0,tab));}
   std::set<std::string> want;for(auto row:golden){std::string r=row;auto first=r.find('\t'),second=r.find('\t',first+1);want.insert("anygraphics."+r.substr(0,second));}assert(keys==want);}}
 FreeLibrary(graphics);FreeLibrary(fixture);std::filesystem::remove_all(root);
 if(mode.empty()){
  // K38: the docs' preset table matches profile().
  auto table=preset_table();std::cout<<"PRESET_TABLE_BEGIN\n"<<table<<"PRESET_TABLE_END"<<std::endl;
  auto docs=std::filesystem::path(__FILE__).parent_path()/".."/"docs"/"mods"/"graphics.md";std::error_code missing;
  if(std::filesystem::exists(docs,missing)){auto text=read_file(docs);std::string begin="<!-- preset-table:begin -->\n",end="<!-- preset-table:end -->";auto a=text.find(begin),b=text.find(end);
   assert(a!=std::string::npos&&b!=std::string::npos);if(text.substr(a+begin.size(),b-a-begin.size())!=table){std::cerr<<"docs/mods/graphics.md preset table differs from profile(); paste the table printed above"<<std::endl;assert(false);}std::cout<<"DOCS_TABLE matches"<<std::endl;}
  else std::cout<<"DOCS_TABLE skipped (docs not next to the source)"<<std::endl;
  for(auto child:{L"--legacy",L"--revision",L"--fresh"})run_child(argv,child);}std::cout<<"PASS: native-only Graphics section, no AnyHelpers dependency or post-FX registrations, migration, draft/apply/cancel/reset, quality levels, thirteen profiles, light colour and wind, water reflections, TAA, sharpening, beam shaping, GPU cost readout, gameplay bypass and unavailable service\n";
}
