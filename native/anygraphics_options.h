#pragma once
#include "anyhelpers_settings_v1.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace graphics_options {
enum Index {Enabled,GameplayOnly,Preset,Advanced,NativeAA,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,NativeShadows,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,AAQuality,Clouds,Grass,Foliage,Atmosphere,SunShafts,LightingQuality,GroundFog,Count};
struct Option {const char* id;const char* group;const char* label;const char* description;uint32_t kind;float initial,minimum,maximum,step;};
static constexpr Option definitions[]={
 {"enabled","General","Enable AnyGraphics","Use the selected native graphics settings. Off restores the game's own settings.",ANY_SETTING_BOOL,1,0,1,1},
 {"gameplay_only","General","Gameplay only","Apply native overrides during gameplay; bypass them in menus and inventories.",ANY_SETTING_BOOL,1,0,1,1},
 {"quality_profile","General","Preset","Off restores game settings. Other profiles provide starting values; individual choices can be changed below.",ANY_SETTING_CHOICE,3,0,5,1},
 {"advanced","General","Advanced options","Show lighting strength and scene-detail choices.",ANY_SETTING_BOOL,0,0,1,1},
 {"native_aa","Scene","Antialiasing","Choose native FXAA or SMAA 1x on the scene before the HUD. SMAA replaces FXAA. Enhanced SMAA adds scene-only subpixel smoothing.",ANY_SETTING_CHOICE,2,0,4,1},
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
 {"aa_quality","Scene","SMAA quality","Edge search quality. Higher levels cost more GPU time.",ANY_SETTING_CHOICE,2,0,3,1},
 {"clouds","Scene","Cloud rendering","Use the native cloud-rendering setting, or override it off/on.",ANY_SETTING_CHOICE,0,0,2,1},
 {"grass","Scene","Grass rendering","Use the native grass-rendering setting, or override it off/on.",ANY_SETTING_CHOICE,0,0,2,1},
 {"foliage","Scene","Foliage rendering","Render native vegetation. Changes rendering only, not world objects or collision.",ANY_SETTING_CHOICE,0,0,2,1},
 {"atmosphere","Lighting","Volumetric fog","Experimental depth-aware fog in the HDR scene, before bloom and HUD.",ANY_SETTING_CHOICE,0,0,4,1},
 {"sun_shafts","Lighting","Sun shafts","Experimental shadowed sunlight scattering. Current build uses the nearest native shadow cascade.",ANY_SETTING_CHOICE,0,0,4,1},
 {"lighting_quality","Lighting","Volumetric quality","Ray-march sampling quality. Higher settings use more GPU time.",ANY_SETTING_CHOICE,1,0,3,1},
 {"ground_fog","Lighting","Ground fog","Concentrate added fog near sea level; higher choices increase height falloff.",ANY_SETTING_CHOICE,1,0,3,1}
};
static_assert(std::size(definitions)==Count);
using Values=std::array<float,Count>;
inline Values defaults(){Values v{};for(size_t i=0;i<Count;++i)v[i]=definitions[i].initial;return v;}
// Quality tiers alter actual supported intensity inputs, never invent sampling modes.
inline constexpr float fog_levels[]={0,.35f,.65f,1,1.5f};
inline constexpr float light_levels[]={0,.75f,1,1.15f,1.3f};
inline constexpr float bloom_levels[]={.05f,.1f,.2f,.35f};
template<size_t N> inline uint32_t nearest(float value,const float(&levels)[N]){uint32_t best=0;for(uint32_t i=1;i<N;++i)if(std::abs(value-levels[i])<std::abs(value-levels[best]))best=i;return best;}
inline Values profile(const Values& current,int level){
 auto v=defaults();for(auto i:{Enabled,GameplayOnly,Advanced})v[i]=current[i];v[Preset]=float(level);
 if(!level)return current; // Off bypasses overrides and keeps the previous individual choices.
 v[NativeAA]=2;v[NativeSSAO]=level>=3?2.f:1.f;v[NativeShadows]=level>=2?2.f:1.f;v[NativeFogBlur]=level>=3?2.f:1.f;
 v[NativeBloom]=level>=2?2.f:1.f;v[BloomAmount]=level>=2?bloom_levels[level-2]:0;v[BloomThreshold]=.75f;
 v[Clouds]=level==1?1.f:0.f;v[Grass]=level<=2?1.f:0.f;v[Foliage]=0;v[FogDensity]=fog_levels[level-1];return v;
}
inline Values effective(const Values& custom){return custom;}
inline bool advanced(size_t i){return (i>=SunLight&&i<=SceneExposure)||i>=Clouds;}
inline float clean(size_t i,double value){auto& d=definitions[i];if(!std::isfinite(value))return d.initial;float v=float(std::clamp(value,double(d.minimum),double(d.maximum)));if(i==FogDensity)return fog_levels[nearest(v,fog_levels)];if(i==SunLight||i==SkyLight||i==AmbientLight)return light_levels[nearest(v,light_levels)];if(i==BloomAmount&&v>0)return bloom_levels[nearest(v,bloom_levels)];if(std::abs(v-d.initial)<d.step*.0001f)v=d.initial;return d.kind==ANY_SETTING_BOOL||d.kind==ANY_SETTING_CHOICE?std::round(v):v;}
}
