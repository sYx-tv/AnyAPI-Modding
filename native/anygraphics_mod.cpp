#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_post_process_v1.h"
#include "anygraphics_options.h"
#include "anyhelpers_settings_v2.h"
#include "anygraphics_shaders.h"
namespace graphics {
using namespace graphics_options;
static AnyModHostV1 host;static const AnyPostProcessV1* post;static const AnyHelpersSettingsV1* settings;static const AnyUiStateV1* ui;
static std::array<float,64> values=defaults();static uint64_t tokens[Count]{},passes[11]{},revision=UINT64_MAX;static bool last_on{},configured{};static uint64_t status_at{};
static void log(const std::string& text){host.log(0,"anygraphics",text.c_str());}
static bool valid(const AnyPostProcessV1* p){return p&&p->version==1&&p->struct_size==sizeof(*p)&&p->register_pass&&p->set&&p->status;}
static uint64_t add(const char* id,const std::string& shader,uint32_t scale,uint64_t input=0,uint64_t aux=0){AnyPostPassV1 d;d.id=id;d.shader=shader.c_str();d.shader_bytes=uint32_t(shader.size());d.downsample=scale;d.input=input;d.auxiliary=aux;return post->register_pass(&d);}
static void read(){if(!settings||revision==settings->revision())return;revision=settings->revision();for(size_t i=0;i<Count;++i){AnySettingValueV1 value;if(tokens[i]&&settings->get(tokens[i],&value))values[i]=clean(i,value.number);}configured=false;log("SETTINGS_APPLIED revision="+std::to_string(revision)+" edge_mode="+std::to_string(int(values[Antialias]))+" bloom="+std::to_string(int(values[Bloom]))+" sharpening="+std::to_string(values[SharpAmount]));}
static void render(const AnyFrameV1* frame,AnyCanvasV1*,void*){
 if(!post)return;read();bool on=values[Enabled]>.5f;
 if(on&&values[GameplayOnly]>.5f){AnyUiSnapshotV1 state;on=ui&&ui->copy(&state)&&state.kind==ANY_UI_GAMEPLAY;}
 if(!configured||on!=last_on){auto custom=values;values=effective(custom);for(size_t i=0;i<11;++i){AnyPostParametersV1 p;std::copy(values.begin(),values.end(),p.values);bool bloom=values[Bloom]>.5f&&values[BloomAmount]>0;bool quarter=values[BloomQuality]<.5f;
  bool active=i==0?values[Antialias]>.5f:i>=1&&i<=4?bloom&&quarter:i>=5&&i<=8?bloom&&!quarter:i==9?values[Sharpen]>.5f&&values[SharpAmount]>0:true;
  // Skip the color/lens pass when all settings are neutral, so sharpen-only uses one GPU shader pass without an extra full-resolution copy pass.
  if(i==10){active=values[Exposure]!=0||values[Gamma]!=1||values[Contrast]!=1||values[Brightness]!=0||values[Saturation]!=1||values[Vibrance]!=0||values[Temperature]!=0||values[Tint]!=0||values[Shadows]!=0||values[Highlights]!=0||values[BlackLift]!=0||values[WhitePoint]!=1||values[Tone]!=0||values[Vignette]>.5f||values[Grain]>.5f||values[Chromatic]>.5f||values[Comparison]!=0;}
  p.enabled=on&&active;if(!post->set(passes[i],&p)){log("PASS_UPDATE_FAILED effects_bypassed=1");on=false;}
 }values=custom;configured=true;last_on=on;}
 if(frame&&frame->tick-status_at>=10000){status_at=frame->tick;AnyPostStatusV1 s;if(post->status(&s)&&s.error[0])log(std::string("RENDER_BYPASS reason=")+s.error);}
}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){
 if(!host||!callbacks||host->abi!=ANYAPI_MOD_ABI||host->struct_size!=sizeof(*host)||!host->log)return false;graphics::host=*host;callbacks->id="anygraphics";callbacks->render=graphics::render;graphics::log("DLL_READY settings=42 presets=5 frame_copy=GPU_ONLY native_AA_replacement=0");return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace graphics;auto services=AnyAPI_Services();if(!services)return;post=(const AnyPostProcessV1*)services->query("anyapi.post_process",1);if(!valid(post)){post=nullptr;log("POST_PROCESS unavailable requires_API_revision_26=1");return;}
 passes[0]=add("edges",graphics_shaders::smoothing(),1);
 for(int q=0;q<2;++q){size_t first=1+q*4;uint32_t scale=q?2:4;passes[first]=add(q?"bloom_half_extract":"bloom_quarter_extract",graphics_shaders::extract(),scale,passes[0]);passes[first+1]=add(q?"bloom_half_horizontal":"bloom_quarter_horizontal",graphics_shaders::blur(true),scale,passes[first]);passes[first+2]=add(q?"bloom_half_vertical":"bloom_quarter_vertical",graphics_shaders::blur(false),scale,passes[first+1]);passes[first+3]=add(q?"bloom_half_composite":"bloom_quarter_composite",graphics_shaders::composite(),1,0,passes[first+2]);}
 passes[9]=add("sharpen",graphics_shaders::sharpen(),1);passes[10]=add("color_lens",graphics_shaders::finish(),1);
 if(std::any_of(std::begin(passes),std::end(passes),[](auto p){return !p;})){post=nullptr;log("SHADER_REGISTRATION failed effects_disabled=1");return;}
 ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui&&(ui->version!=1||ui->struct_size!=sizeof(*ui)))ui=nullptr;
 settings=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);if(settings&&(settings->version!=1||settings->struct_size!=sizeof(*settings)))settings=nullptr;
 if(settings){static std::array<std::string,Count> ids,names;static const char* edges[]={"Off","Edge adaptive","Edge adaptive · stronger"},*quality[]={"Quarter resolution","Half resolution"},*tone[]={"Neutral","Soft contrast","Filmic"},*comparison[]={"Off","Original on left","Original on right"},*looks[]={"Custom / saved tuning","Natural","Soft edges","Vibrant","Cinematic","Lightweight"};
  for(size_t i=0;i<Count;++i){auto& option=definitions[i];ids[i]="anygraphics."+std::string(option.group);std::transform(ids[i].begin(),ids[i].end(),ids[i].begin(),[](unsigned char c){return char(std::tolower(c));});names[i]="AnyGraphics";AnyModSettingV1 d;d.mod_id=ids[i].c_str();d.mod_name=names[i].c_str();d.setting_id=option.id;d.label=option.label;d.description=option.description;d.order=i==Enabled?-100:i==Preset?-90:i==GameplayOnly?-80:i==Comparison?-70:i==Advanced?-60:int32_t(i);d.kind=option.kind;d.default_number=option.initial;d.minimum=option.minimum;d.maximum=option.maximum;d.step=option.step;
   if(i==Preset){d.choices=looks;d.choice_count=6;}if(i==Antialias){d.choices=edges;d.choice_count=3;}if(i==BloomQuality){d.choices=quality;d.choice_count=2;}if(i==Tone){d.choices=tone;d.choice_count=3;}if(i==Comparison){d.choices=comparison;d.choice_count=3;}tokens[i]=settings->register_setting(&d);
  }
  auto presentation=(const AnyHelpersSettingsV2*)services->query("anyhelpers.settings",2);
  if(presentation&&presentation->version==2&&presentation->struct_size==sizeof(*presentation)&&presentation->visibility){
   for(size_t i=0;i<Count;++i){if(i==Enabled||i==Preset||i==GameplayOnly||i==Comparison)continue;
    presentation->visibility(tokens[i],tokens[Preset],0,i==Advanced||basic(i)?0:tokens[Advanced],1);
   }
  }else log("SETTINGS_PRESENTATION requires_AnyHelpers_update=1");
  auto registered=std::count_if(std::begin(tokens),std::end(tokens),[](auto t){return t!=0;});log("SETTINGS_REGISTERED count="+std::to_string(registered)+" provider=AnyHelpers");
 }else log("ANYHELPERS unavailable conservative_defaults=1");log("POST_PROCESS_REGISTERED passes=11 custom_AA=spatial_only native_HUD_in_source=1");
}
