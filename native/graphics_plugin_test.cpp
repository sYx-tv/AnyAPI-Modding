#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "anyapi_services_v1.h"
#include "anyapi_menu_v2.h"
#include "anyapi_scene_controls_v1.h"
#include "anyapi_scene_controls_v2.h"
#include "anyapi_scene_lighting_v1.h"
#include "anyapi_scene_lighting_v2.h"
#include "anyapi_scene_lighting_v3.h"
#include "anyapi_scene_antialiasing_v2.h"
#include "anyapi_scene_timing_v1.h"
#include "anyapi_scene_antialiasing_v1.h"
#include "anyapi_post_process_v1.h"
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
static AnySceneParametersV1 policy;static bool ready=true;static unsigned submissions{},post_calls{};
static bool reject_scene=false;
static bool set(const AnySceneParametersV1* p){++submissions;if(reject_scene)return false;policy=*p;return true;}
static AnySceneParametersV2 detail_policy;
static bool set_v2(const AnySceneParametersV2* p){detail_policy=*p;return set(&p->scene);}
static bool status(AnySceneStatusV1* s){s->ready=ready;return true;}
static uint64_t forbidden(const AnyPostPassV1*){++post_calls;assert(false&&"AnyGraphics must never register post FX");return 0;}
static const AnySceneControlsV1 scene{sizeof(AnySceneControlsV1),1,set,status};
static const AnySceneControlsV2 scene_v2{sizeof(AnySceneControlsV2),2,set_v2,status};
static const AnyPostProcessV1 post{sizeof(AnyPostProcessV1),1,forbidden,nullptr,nullptr};
static void log(uint32_t,const char*,const char* text){std::cout<<text<<'\n';}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;using namespace graphics_options;assert(argc==4||argc==5);bool local=argc==5&&std::wstring(argv[4])==L"--local-lights",absent=argc==5&&!local;
 auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto service=AnyAPI_Services();assert(service);assert(service->publish("anyapi.post_process",1,&post));if(!absent){assert(service->publish("anyapi.scene_controls",1,&scene));assert(service->publish("anyapi.scene_controls",2,&scene_v2));assert(service->publish("anyapi.scene_antialiasing",1,&aa_api));assert(service->publish("anyapi.scene_lighting",1,&lighting_api));}auto graphics=LoadLibraryW(argv[3]);assert(graphics);
 if(local){assert(service->publish("anyapi.scene_lighting",2,&local_api));assert(service->publish("anyapi.scene_lighting",3,&shaped_api));assert(service->publish("anyapi.scene_antialiasing",2,&aa2_api));assert(service->publish("anyapi.scene_timing",1,&timing_api));}
 auto root=std::filesystem::temp_directory_path()/(L"AnyGraphicsNative-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root/L"AnyHelpers");
 // Migration retains native keys, ignores removed post FX and does not load AnyHelpers.
 {std::ofstream old(root/L"AnyHelpers"/L"settings.tsv");old<<"anygraphics.scene\tsun_light\t3\t312e35\nanygraphics.general\tcomparison\t4\t31\nanygraphics.sharpness\tsharpen\t1\t31\n";}
 auto path=root.wstring();AnyModHostV1 host;host.game_directory=path.c_str();host.plugin_directory=path.c_str();host.log=log;AnyModCallbacksV1 g;assert(((AnyModInitV1)GetProcAddress(graphics,"AnyAPI_ModInit"))(&host,&g));((void(*)())GetProcAddress(graphics,"AnyAPI_ModReady"))();assert(g.render);
 auto event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureGraphicsEvent");auto stage=(void(*)(uint64_t,double))GetProcAddress(fixture,"FixtureGraphicsStage");auto rows=(uint32_t(*)())GetProcAddress(fixture,"FixtureRows");auto dirty=(uint32_t(*)())GetProcAddress(fixture,"FixtureGraphicsDirty");auto title=(const char*(*)())GetProcAddress(fixture,"FixtureGraphicsTitle");auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");assert(event&&stage&&rows&&dirty&&title&&ui);assert(std::string(title()).find("Modded")!=std::string::npos);assert(!service->query("anyhelpers.settings",1));
 AnyFrameV1 frame;frame.tick=100;frame.focused=1;AnyCanvasV1 canvas;ui(1);auto render=[&](){frame.tick+=16;g.render(&frame,&canvas,nullptr);assert(!canvas.pixels&&post_calls==0);};auto apply=[&](Index i,double v){event(ANY_MENU_OPEN);stage(i+1,v);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);render();};render();
 if(absent){assert(!submissions);event(ANY_MENU_OPEN);stage(0,0);assert(rows()>3);event(ANY_MENU_CANCEL);}
 else if(local){
 assert(lighting_policy.enabled&&local_policy.local_strength==1&&local_policy.local_budget==4);
 apply(Atmosphere,0);apply(SunShafts,0);assert(lighting_policy.enabled&&lighting_policy.fog_strength==0&&lighting_policy.sun_shafts==0);
 apply(LocalBeams,4);apply(LocalIntensity,3);apply(LocalBudget,3);assert(local_policy.local_strength==9&&local_policy.local_budget==8);
 event(ANY_MENU_OPEN);stage(LocalIntensity+1,1);render();assert(local_policy.local_strength==9);event(ANY_MENU_CANCEL);render();assert(local_policy.local_strength==9);
 apply(LocalIntensity,0);assert(!lighting_policy.enabled);apply(LocalIntensity,1);assert(lighting_policy.enabled);
 apply(LocalBeams,0);assert(!lighting_policy.enabled);apply(LocalBeams,2);apply(Preset,0);assert(!lighting_policy.enabled);apply(Preset,3);assert(lighting_policy.enabled);
 // Exercise real menu selection and submitted policy, not just the profile helper.
 // Presets 1-6: Performance, Low, Medium, High, Ultra, Cinematic.
 const float densities[]={.002f,.002f,.001f,.001f,.002f,.002f},sun_strength[]={0,.5f,1,1,2,2.6f},local_strength[]={0,.5f,1,2,2.2f,3.9f};
 const uint32_t quality[]={0,0,1,2,3,3},budgets[]={2,2,4,6,8,8};
 const float clarity[]={.85f,.85f,.85f,.85f,.85f,.6f},reach[]={60,60,120,120,250,250},sharpening[]={0,0,0,.3f,.3f,.55f};const uint32_t methods[]={1,1,1,3,4,4};
 for(int preset=1;preset<=6;++preset){apply(Preset,preset);auto i=preset-1;std::cout<<"PRESET "<<preset<<" enabled="<<lighting_policy.enabled<<" density="<<lighting_policy.fog_density<<" sun="<<lighting_policy.sun_shafts<<" local="<<local_policy.local_strength<<" quality="<<lighting_policy.quality<<" budget="<<local_policy.local_budget<<" height="<<lighting_policy.height_falloff<<std::endl;assert(lighting_policy.enabled==(preset>1)&&lighting_policy.fog_density==densities[i]&&std::abs(lighting_policy.sun_shafts-sun_strength[i])<.001f&&std::abs(local_policy.local_strength-local_strength[i])<.001f&&lighting_policy.quality==quality[i]&&local_policy.local_budget==budgets[i]&&lighting_policy.height_falloff==0);
  assert(shaped_policy.clarity==clarity[i]&&shaped_policy.beam_reach==reach[i]&&shaped_policy.sun_response==1&&shaped_policy.temporal==1);
  assert(aa2_policy.enabled==(preset>1)&&(preset==1||aa2_policy.method==methods[i])&&std::abs(aa2_policy.sharpening-sharpening[i])<.001f);}
 // Shaping rows submit their own values; the GPU cost readout polls timing while the menu draws.
 apply(Preset,3);apply(ShaftClarity,3);apply(BeamReach,3);apply(SunResponse,0);apply(TemporalLighting,0);assert(shaped_policy.clarity==0&&shaped_policy.beam_reach==0&&!shaped_policy.sun_response&&!shaped_policy.temporal);
 apply(NativeAA,5);apply(Sharpening,3);assert(aa2_policy.enabled&&aa2_policy.method==3&&std::abs(aa2_policy.sharpening-.8f)<.001f);apply(NativeAA,6);assert(aa2_policy.method==4);apply(NativeAA,2);assert(!aa2_policy.enabled);
 event(ANY_MENU_OPEN);stage(0,0);assert(timing_reads>0);event(ANY_MENU_CANCEL);
 apply(Preset,6);
 event(ANY_MENU_OPEN);stage(Preset+1,1);render();assert(std::abs(local_policy.local_strength-3.9f)<.001f);event(ANY_MENU_CANCEL);render();assert(std::abs(local_policy.local_strength-3.9f)<.001f);
 apply(Enabled,0);assert(!lighting_policy.enabled);apply(Enabled,1);ui(13);render();assert(!lighting_policy.enabled);apply(GameplayOnly,0);assert(lighting_policy.enabled);
 }else {
 assert(policy.enabled&&policy.sun==1.3f);apply(Advanced,1);apply(SunLight,3);assert(policy.sun==1.15f);
 apply(Clouds,1);apply(Grass,2);apply(Foliage,1);assert(detail_policy.clouds==1&&detail_policy.grass==2&&detail_policy.foliage==1);
 event(ANY_MENU_OPEN);stage(Clouds+1,2);render();assert(detail_policy.clouds==1);event(ANY_MENU_CANCEL);render();assert(detail_policy.clouds==1);
 event(ANY_MENU_OPEN);stage(SunLight+1,4);assert(dirty());render();assert(policy.sun==1.15f);event(ANY_MENU_CANCEL);render();assert(policy.sun==1.15f);
 apply(NativeAA,3);assert(aa_policy.enabled&&policy.aa==0&&aa_policy.quality==2);apply(AAQuality,3);assert(aa_policy.quality==3);
 apply(NativeAA,4);assert(aa_policy.enabled&&aa_policy.method==2&&policy.aa==0);
 apply(NativeAA,1);assert(!aa_policy.enabled);apply(NativeBloom,5);assert(policy.aa==1&&policy.bloom==2&&policy.bloom_intensity==.35f);
 apply(NativeSSAO,1);apply(NativeShadows,2);apply(NativeFogBlur,1);apply(FogDensity,1);assert(policy.ssao==1&&policy.shadows==2&&policy.fog_blur==1&&policy.fog==.35f&&policy.light_exposure==0);
 for(int preset=1;preset<=6;++preset){apply(Preset,preset);auto expected=profile(defaults(),preset);assert(policy.enabled&&policy.sun==1&&policy.fog==expected[FogDensity]&&policy.ssao==uint32_t(expected[NativeSSAO])&&policy.bloom_intensity==expected[BloomAmount]);assert(detail_policy.clouds==uint32_t(expected[Clouds])&&detail_policy.grass==uint32_t(expected[Grass])&&detail_policy.foliage==0);event(ANY_MENU_OPEN);stage(0,0);assert(rows()>10);event(ANY_MENU_CANCEL);}
 assert(lighting_submissions&&lighting_policy.enabled);apply(Atmosphere,0);apply(SunShafts,0);assert(!lighting_policy.enabled);
 apply(Atmosphere,3);apply(SunShafts,2);apply(ShaftIntensity,1);apply(BeamFocus,1);apply(LightingQuality,3);apply(GroundFog,2);
 assert(lighting_policy.enabled&&lighting_policy.fog_density==.004f&&lighting_policy.sun_shafts==1&&lighting_policy.quality==3&&lighting_policy.height_falloff==.025f);
 event(ANY_MENU_OPEN);stage(Atmosphere+1,4);render();assert(lighting_policy.fog_density==.004f);event(ANY_MENU_CANCEL);render();assert(lighting_policy.fog_density==.004f);
 apply(Atmosphere,0);assert(lighting_policy.enabled&&lighting_policy.fog_strength==0&&lighting_policy.sun_shafts==1);
 apply(ShaftIntensity,2);assert(lighting_policy.sun_shafts==2&&lighting_policy.fog_density==.002f);
 apply(BeamFocus,2);assert(lighting_policy.anisotropy==.55f);
 event(ANY_MENU_OPEN);stage(ShaftIntensity+1,4);render();assert(lighting_policy.sun_shafts==2);event(ANY_MENU_CANCEL);render();assert(lighting_policy.sun_shafts==2);
 apply(ShaftIntensity,0);assert(!lighting_policy.enabled);apply(ShaftIntensity,1);assert(lighting_policy.enabled&&lighting_policy.sun_shafts==1);
 apply(SunShafts,4);apply(ShaftIntensity,4);assert(lighting_policy.sun_shafts==12);apply(SunShafts,0);assert(!lighting_policy.enabled);
 apply(Atmosphere,2);apply(Preset,0);assert(!policy.enabled&&!aa_policy.enabled&&!lighting_policy.enabled);apply(Preset,3);assert(policy.enabled);
 for(int level=0;level<5;++level){apply(FogDensity,level);assert(policy.fog==fog_levels[level]);}
 for(int level=2;level<6;++level){apply(NativeBloom,level);assert(policy.bloom==2&&policy.bloom_intensity==bloom_levels[level-2]);}
 event(ANY_MENU_OPEN);stage(0,0);event(ANY_MENU_RESET);assert(dirty());render();assert(policy.fog==1.5f);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);render();assert(policy.sun==1&&policy.aa==2&&policy.bloom==2&&policy.fog==.65f);
 apply(Atmosphere,2);apply(Enabled,0);assert(!policy.enabled&&!lighting_policy.enabled);apply(Enabled,1);ui(13);render();assert(!policy.enabled&&!lighting_policy.enabled);apply(GameplayOnly,0);assert(policy.enabled);ready=false;render();ready=true;render();assert(policy.enabled);
 reject_scene=true;ready=false;render();auto rejected_count=submissions;render();assert(submissions==rejected_count);reject_scene=false;frame.tick+=501;render();assert(submissions==rejected_count+1);render();assert(submissions==rejected_count+1);

 }
 {std::ifstream saved(root/L"AnyGraphics"/L"settings.tsv");std::string text((std::istreambuf_iterator<char>(saved)),{});assert(text.find("comparison")==std::string::npos&&text.find("sharpen")==std::string::npos);}
 FreeLibrary(graphics);FreeLibrary(fixture);std::filesystem::remove_all(root);std::cout<<"PASS: native-only Graphics section, no AnyHelpers dependency or post-FX registrations, migration, draft/apply/cancel/reset, quality levels, seven profiles, TAA, sharpening, beam shaping, GPU cost readout, gameplay bypass and unavailable service\n";
}
