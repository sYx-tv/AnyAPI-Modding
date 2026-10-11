#pragma once
#include "anyhelpers_settings_v1.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace graphics_options {
enum Index {Enabled,GameplayOnly,Preset,Advanced,NativeAA,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,NativeShadows,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,AAQuality,Clouds,Grass,Foliage,Atmosphere,SunShafts,LightingQuality,GroundFog,ShaftIntensity,BeamFocus,LocalBeams,LocalIntensity,LocalBudget,ShaftClarity,BeamReach,SunResponse,TemporalLighting,Sharpening,CostReadout,ToneMapping,ColourLook,LookStrength,EyeAdaptation,Brightness,SceneBloom,SunGlare,Vignette,Count};
struct Option {const char* id;const char* group;const char* label;const char* description;uint32_t kind;float initial,minimum,maximum,step;};
static constexpr Option definitions[]={
 {"enabled","General","Enable AnyGraphics","Use the selected native graphics settings. Off restores the game's own settings.",ANY_SETTING_BOOL,1,0,1,1},
 {"gameplay_only","General","Gameplay only","Apply native overrides during gameplay; bypass them in menus and inventories.",ANY_SETTING_BOOL,1,0,1,1},
 {"quality_profile","General","Preset","Off restores game settings. Other profiles provide starting values; individual choices can be changed below. Cinematic favours looks over frame rate.",ANY_SETTING_CHOICE,3,0,6,1},
 {"advanced","General","Advanced options","Show lighting strength and scene-detail choices.",ANY_SETTING_BOOL,0,0,1,1},
 {"native_aa","Scene","Antialiasing","Choose native FXAA or scene AA before the HUD. SMAA replaces FXAA; Enhanced SMAA adds subpixel smoothing. TAA blends recent frames to stop shimmer; TAA + jitter is the smoothest and can trail slightly behind fast motion.",ANY_SETTING_CHOICE,2,0,6,1},
 {"native_bloom","Scene","Bloom","Use the game setting, turn bloom off, or choose the strength of native bloom.",ANY_SETTING_CHOICE,2,0,2,1},
 {"bloom_amount","Bloom","Bloom intensity","Native bloom intensity when Bloom is Custom.",ANY_SETTING_NUMBER,.1f,0,2,.05f},
 {"bloom_threshold","Bloom","Bloom threshold","Native brightness threshold when Bloom is Custom.",ANY_SETTING_NUMBER,.75f,0,4,.025f},
 {"native_ssao","Scene","Ambient occlusion","Use the game's SSAO setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"native_shadows","Scene","Shadows","Use the game's shadow setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"native_fog_blur","Scene","Fog blur","Use the game's fog-blur setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"sun_light","Scene","Sunlight","Scale sunlight before native scene constants are built. 1 preserves the scene input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"sky_light","Scene","Sky light","Scale sky illumination without changing weather or time. 1 preserves the input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"ambient_light","Scene","Ambient light","Scale ambient, view and back lighting before rendering. 1 preserves the input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"fog_density","Scene","Fog","Choose the strength of native base fog. Off removes base fog.",ANY_SETTING_NUMBER,.65f,0,3,.05f},
 {"scene_exposure","Scene","Scene lighting (stops)","Scale lighting before rendering. 0 preserves the input; this is not a screen filter.",ANY_SETTING_NUMBER,0,-2,2,.1f},
 {"aa_quality","Scene","AA quality","SMAA edge search quality. With TAA, higher levels also keep more of the previous frames (smoother, slightly more trailing).",ANY_SETTING_CHOICE,2,0,3,1},
 {"clouds","Scene","Cloud rendering","Use the native cloud-rendering setting, or override it off/on.",ANY_SETTING_CHOICE,0,0,2,1},
 {"grass","Scene","Grass rendering","Use the native grass-rendering setting, or override it off/on.",ANY_SETTING_CHOICE,0,0,2,1},
 {"foliage","Scene","Foliage rendering","Render native vegetation. Changes rendering only, not world objects or collision.",ANY_SETTING_CHOICE,0,0,2,1},
 {"atmosphere","Lighting","Volumetric fog","Experimental depth-aware fog in the HDR scene, before bloom and HUD.",ANY_SETTING_CHOICE,0,0,4,1},
 {"sun_shafts","Lighting","Sun shafts","Experimental shadowed sunlight scattering. Uses the native sun-shadow cascades before bloom and HUD.",ANY_SETTING_CHOICE,0,0,4,1},
 {"lighting_quality","Lighting","Volumetric quality","Ray-march sampling quality. Higher settings use more GPU time.",ANY_SETTING_CHOICE,1,0,3,1},
 {"ground_fog","Lighting","Fog height falloff","Off keeps added fog uniform with altitude. Higher levels concentrate fog near sea level and may make it faint on hills.",ANY_SETTING_CHOICE,0,0,3,1},
 {"shaft_intensity","Lighting","Sun shaft intensity","Scale beam brightness independently of fog. 1 is the current look; higher values make beams more dramatic without adding samples.",ANY_SETTING_NUMBER,1,0,4,.1f},
 {"beam_focus","Lighting","Beam focus","Soft spreads scattering widely. Focused and Strong concentrate sun and local beams toward their light sources.",ANY_SETTING_CHOICE,1,0,3,1},
 {"local_beams","Lighting","Local light beams","Volumetric headlights, torches, spotlights and point lights in the same scene pass. Native spotlight shadows are used when available.",ANY_SETTING_CHOICE,2,0,4,1},
 {"local_intensity","Lighting","Local beam intensity","Scale local-light scattering independently of sun shafts and fog.",ANY_SETTING_NUMBER,1,0,4,.1f},
 {"local_budget","Lighting","Local light budget","Prioritize nearby influential lights: 2, 4, 6 or 8 per frame. Higher budgets cost more GPU time.",ANY_SETTING_CHOICE,1,0,3,1},
 {"shaft_clarity","Lighting","Haze between beams","Clear keeps light only where it breaks through gaps. Classic is the original look, which lights all air and reads as fog.",ANY_SETTING_CHOICE,1,0,3,1},
 {"beam_reach","Lighting","Beam reach","How far from you sun beams build up. Shorter keeps distant scenery clear.",ANY_SETTING_CHOICE,1,0,3,1},
 {"sun_response","Lighting","Follow sun height","Strongest, warm beams at sunrise and sunset; subtle at noon; none at night.",ANY_SETTING_BOOL,1,0,1,1},
 {"temporal_lighting","Lighting","Smooth beams and fog","Blend recent frames to remove banding and noise in volumetric lighting.",ANY_SETTING_BOOL,1,0,1,1},
 {"crispness","Scene","Sharpening","Contrast-adaptive sharpening after scene AA, before the HUD. Restores detail softened by TAA or SMAA.",ANY_SETTING_CHOICE,0,0,3,1},
 {"cost_readout","General","Show GPU cost","Show how long volumetric lighting, scene effects and scene AA take on the GPU, and the frame rate.",ANY_SETTING_BOOL,1,0,1,1},
 {"tone_mapping","Finish","Tone mapping","Off keeps the game's look. Filmic keeps colour and detail in bright light like a film camera; Clean stays bright and true to the game's colours. Any choice other than Off also replaces the game's bloom.",ANY_SETTING_CHOICE,0,0,3,1},
 {"colour_look","Finish","Colour look","A colour grade on top of tone mapping. Vivid is the shader-pack look.",ANY_SETTING_CHOICE,0,0,5,1},
 {"look_strength","Finish","Look strength","How strongly the colour look is applied.",ANY_SETTING_CHOICE,1,0,2,1},
 {"eye_adaptation","Finish","Eye adaptation","Briefly brightens dark places and dims bright ones, like your eyes adjusting, then settles back to the game's own day and night brightness.",ANY_SETTING_BOOL,1,0,1,1},
 {"brightness","Finish","Brightness (stops)","Overall exposure before tone mapping. 0 keeps the game's brightness.",ANY_SETTING_NUMBER,0,-2,2,.25f},
 {"scene_bloom","Finish","Bloom","Soft, wide glow around bright light, before tone mapping. Replaces the game's bloom while tone mapping is on.",ANY_SETTING_CHOICE,2,0,3,1},
 {"sun_glare","Finish","Sun glare","A glow and faint streaks around the sun when it is in view, dimmed when trees or terrain block it.",ANY_SETTING_CHOICE,2,0,3,1},
 {"vignette","Finish","Vignette","Darkens the corners of the screen.",ANY_SETTING_CHOICE,0,0,3,1}
};
static_assert(std::size(definitions)==Count);
using Values=std::array<float,Count>;
inline Values defaults(){Values v{};for(size_t i=0;i<Count;++i)v[i]=definitions[i].initial;return v;}
// Quality tiers alter actual supported intensity inputs, never invent sampling modes.
inline constexpr float fog_levels[]={0,.35f,.65f,1,1.5f};
inline constexpr float light_levels[]={0,.75f,1,1.15f,1.3f};
inline constexpr float bloom_levels[]={.05f,.1f,.2f,.35f};
inline constexpr float clarity_levels[]={1,.85f,.6f,0},reach_levels[]={60,120,250,0},sharpen_levels[]={0,.3f,.55f,.8f};
inline constexpr float look_strength_levels[]={.5f,.8f,1},scene_bloom_levels[]={0,.25f,.45f,.7f},glare_levels[]={0,.5f,1,1.6f},vignette_levels[]={0,.25f,.5f,.8f};
// Native AA choice -> scene AA method: 3 SMAA, 4 Enhanced SMAA, 5 TAA, 6 TAA + jitter.
inline uint32_t aa_method(float choice){return choice>=6?4:choice>=5?3:choice>=4?2:1;}
// Menu order: new rows sit next to the controls they modify.
inline constexpr Index order[]={Enabled,GameplayOnly,Preset,Advanced,NativeAA,AAQuality,Sharpening,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,NativeShadows,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,Clouds,Grass,Foliage,
 ToneMapping,ColourLook,LookStrength,Brightness,EyeAdaptation,SceneBloom,SunGlare,Vignette,
 Atmosphere,SunShafts,ShaftIntensity,ShaftClarity,BeamReach,SunResponse,BeamFocus,LightingQuality,TemporalLighting,GroundFog,LocalBeams,LocalIntensity,LocalBudget,CostReadout};
static_assert(std::size(order)==Count);
template<size_t N> inline uint32_t nearest(float value,const float(&levels)[N]){uint32_t best=0;for(uint32_t i=1;i<N;++i)if(std::abs(value-levels[i])<std::abs(value-levels[best]))best=i;return best;}
inline Values profile(const Values& current,int level){
 auto v=defaults();for(auto i:{Enabled,GameplayOnly,Advanced,CostReadout})v[i]=current[i];v[Preset]=float(level);
 if(!level)return current; // Off bypasses overrides and keeps the previous individual choices.
 // Index 0..5: Performance, Low, Medium, High, Ultra, Cinematic.
 auto tier=size_t(level-1);
 static constexpr float aa[]={2,3,3,5,6,6},aa_quality[]={1,1,2,2,3,3},sharpen[]={0,0,0,1,1,2};
 v[NativeAA]=aa[tier];v[AAQuality]=aa_quality[tier];v[Sharpening]=sharpen[tier];
 v[NativeSSAO]=level>=3?2.f:1.f;v[NativeShadows]=level>=2?2.f:1.f;v[NativeFogBlur]=level>=3?2.f:1.f;
 static constexpr float bloom[]={0,.05f,.1f,.2f,.35f,.35f};v[NativeBloom]=level>=2?2.f:1.f;v[BloomAmount]=bloom[tier];v[BloomThreshold]=.75f;
 // Fog and beams are artistic strengths, not sample quality. High tiers add
 // samples and lights but keep fog moderate so beams never wash out the scene.
 static constexpr float atmosphere[]={0,0,1,1,2,2},beams[]={0,1,2,2,3,3},local[]={0,1,2,3,3,4},quality[]={0,0,1,2,3,3};
 static constexpr float sun_intensity[]={1,1,1,1,1,1.3f},local_intensity[]={1,1,1,1,1.1f,1.3f},focus[]={1,1,1,1,2,2},clarity[]={1,1,1,1,1,2},reach[]={0,0,1,1,2,2},fog[]={0,.35f,.65f,.65f,1,1};
 v[Atmosphere]=atmosphere[tier];v[SunShafts]=beams[tier];v[LocalBeams]=local[tier];v[LightingQuality]=v[LocalBudget]=quality[tier];
 v[ShaftIntensity]=sun_intensity[tier];v[LocalIntensity]=local_intensity[tier];v[BeamFocus]=focus[tier];v[GroundFog]=0;
 v[ShaftClarity]=clarity[tier];v[BeamReach]=reach[tier];v[SunResponse]=1;v[TemporalLighting]=1;
 // Finishing: Filmic from Low up, the shader-pack look from Ultra.
 static constexpr float tone[]={0,2,2,2,2,2},look[]={0,0,0,0,5,5},strength[]={1,1,1,1,1,2},scene_bloom[]={0,1,1,2,2,3},glare[]={0,1,1,2,2,2},vignette[]={0,0,0,0,1,1};
 v[ToneMapping]=tone[tier];v[ColourLook]=look[tier];v[LookStrength]=strength[tier];v[EyeAdaptation]=1;v[Brightness]=0;v[SceneBloom]=scene_bloom[tier];v[SunGlare]=glare[tier];v[Vignette]=vignette[tier];
 v[Clouds]=level==1?1.f:0.f;v[Grass]=level<=2?1.f:0.f;v[Foliage]=0;v[FogDensity]=fog[tier];return v;
}
inline Values effective(const Values& custom){return custom;}
inline bool advanced(size_t i){return (i>=SunLight&&i<=SceneExposure)||(i>=Clouds&&i<ShaftClarity);}
inline float clean(size_t i,double value){auto& d=definitions[i];if(!std::isfinite(value))return d.initial;float v=float(std::clamp(value,double(d.minimum),double(d.maximum)));if(i==FogDensity)return fog_levels[nearest(v,fog_levels)];if(i==SunLight||i==SkyLight||i==AmbientLight)return light_levels[nearest(v,light_levels)];if(i==BloomAmount&&v>0)return bloom_levels[nearest(v,bloom_levels)];if(std::abs(v-d.initial)<d.step*.0001f)v=d.initial;return d.kind==ANY_SETTING_BOOL||d.kind==ANY_SETTING_CHOICE?std::round(v):v;}
}
