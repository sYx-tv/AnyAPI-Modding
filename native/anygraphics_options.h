#pragma once
#include "anyhelpers_settings_v1.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace graphics_options {
enum Index {Enabled,GameplayOnly,Antialias,AAAmount,AAThreshold,Sharpen,SharpAmount,SharpRadius,SharpLimit,Bloom,BloomAmount,BloomThreshold,BloomKnee,BloomRadius,BloomQuality,BloomWarmth,BloomSaturation,Exposure,Gamma,Contrast,Brightness,Saturation,Vibrance,Temperature,Tint,Shadows,Highlights,BlackLift,WhitePoint,Tone,Vignette,VignetteAmount,VignetteRadius,VignetteSoftness,Grain,GrainAmount,GrainSize,Chromatic,ChromaticAmount,Comparison,Preset,Advanced,Count};
struct Option {const char* id;const char* group;const char* label;const char* description;uint32_t kind;float initial,minimum,maximum,step;};
static constexpr Option definitions[]={
 {"enabled","General","Enable AnyGraphics","Bypasses all shader passes when off.",ANY_SETTING_BOOL,1,0,1,1},
 {"gameplay_only","General","Gameplay only","Bypasses effects in menus and inventories. Native HUD may still be affected.",ANY_SETTING_BOOL,1,0,1,1},
 {"antialias","Edges","Edge smoothing","Additional spatial edge smoothing. Does not replace native AA; no TAA or DLSS.",ANY_SETTING_CHOICE,0,0,2,1},
 {"aa_amount","Edges","Smoothing strength","Blending along detected edges. High values can soften detail.",ANY_SETTING_NUMBER,.5f,0,1,.05f},
 {"aa_threshold","Edges","Edge threshold","Lower values smooth more low-contrast edges.",ANY_SETTING_NUMBER,.12f,.02f,.5f,.01f},
 {"sharpen","Sharpness","Enable sharpening","Contrast-limited unsharp masking, applied after bloom and smoothing.",ANY_SETTING_BOOL,1,0,1,1},
 {"sharp_amount","Sharpness","Strength","Use modest values to avoid halos.",ANY_SETTING_NUMBER,.15f,0,1.5f,.05f},
 {"sharp_radius","Sharpness","Radius (pixels)","The size of the sharpening neighborhood.",ANY_SETTING_NUMBER,1,.5f,3,.25f},
 {"sharp_limit","Sharpness","Halo limit","Limits each sharpening correction.",ANY_SETTING_NUMBER,.06f,.01f,.3f,.01f},
 {"bloom","Bloom","Enable bloom","Adds bloom from the finished image. Disable native bloom for easier tuning.",ANY_SETTING_BOOL,0,0,1,1},
 {"bloom_amount","Bloom","Intensity","Strength of the blurred bright regions.",ANY_SETTING_NUMBER,.2f,0,2,.05f},
 {"bloom_threshold","Bloom","Brightness threshold","Minimum brightness contributing to bloom.",ANY_SETTING_NUMBER,.75f,0,1,.025f},
 {"bloom_knee","Bloom","Soft knee","Softens the transition into the bloom threshold.",ANY_SETTING_NUMBER,.15f,.01f,.5f,.01f},
 {"bloom_radius","Bloom","Spread (pixels)","Blur radius in the downsampled bloom image.",ANY_SETTING_NUMBER,3,.5f,12,.5f},
 {"bloom_quality","Bloom","Resolution","Quarter resolution is lighter; half resolution retains finer glow.",ANY_SETTING_CHOICE,0,0,1,1},
 {"bloom_warmth","Bloom","Warmth","Warms or cools the added glow.",ANY_SETTING_NUMBER,0,-1,1,.05f},
 {"bloom_saturation","Bloom","Saturation","Color saturation of the added glow.",ANY_SETTING_NUMBER,1,0,2,.05f},
 {"exposure","Color","Exposure (stops)","Image brightness multiplier; does not recover clipped HDR highlights.",ANY_SETTING_NUMBER,0,-2,2,.1f},
 {"gamma","Color","Gamma","Adjusts midtone response.",ANY_SETTING_NUMBER,1,.5f,2,.05f},
 {"contrast","Color","Contrast","Contrast around middle gray.",ANY_SETTING_NUMBER,1,.5f,1.75f,.05f},
 {"brightness","Color","Brightness","Adds or subtracts a small image offset.",ANY_SETTING_NUMBER,0,-.2f,.2f,.01f},
 {"saturation","Color","Saturation","Overall color saturation.",ANY_SETTING_NUMBER,1,0,2,.05f},
 {"vibrance","Color","Vibrance","Boosts less saturated colors preferentially.",ANY_SETTING_NUMBER,0,-1,1,.05f},
 {"temperature","Color","Temperature","Balances warm red and cool blue tones.",ANY_SETTING_NUMBER,0,-1,1,.05f},
 {"tint","Color","Tint","Balances green and magenta tones.",ANY_SETTING_NUMBER,0,-1,1,.05f},
 {"shadows","Tone","Shadows","Adjusts darker regions without a depth buffer.",ANY_SETTING_NUMBER,0,-.5f,.5f,.025f},
 {"highlights","Tone","Highlights","Adjusts bright regions of the finished image.",ANY_SETTING_NUMBER,0,-.5f,.5f,.025f},
 {"black_lift","Tone","Black lift","Raises the darkest values.",ANY_SETTING_NUMBER,0,0,.15f,.01f},
 {"white_point","Tone","White point","Scales the highlight range.",ANY_SETTING_NUMBER,1,.7f,1.5f,.05f},
 {"tone","Tone","Tone curve","Optional screen-space tonal styling.",ANY_SETTING_CHOICE,0,0,2,1},
 {"vignette","Lens","Enable vignette","Darkens the outer image. Off by default.",ANY_SETTING_BOOL,0,0,1,1},
 {"vignette_amount","Lens","Vignette strength","Amount of edge darkening.",ANY_SETTING_NUMBER,.2f,0,1,.05f},
 {"vignette_radius","Lens","Vignette radius","Size of the unaffected central area.",ANY_SETTING_NUMBER,.75f,.1f,1.5f,.05f},
 {"vignette_softness","Lens","Vignette softness","Width of the transition to darkened edges.",ANY_SETTING_NUMBER,.5f,.05f,1,.05f},
 {"grain","Film","Enable grain","Adds subtle static screen-space grain. Off by default.",ANY_SETTING_BOOL,0,0,1,1},
 {"grain_amount","Film","Grain strength","Noise intensity.",ANY_SETTING_NUMBER,.015f,0,.1f,.005f},
 {"grain_size","Film","Grain size (pixels)","Scale of the noise pattern.",ANY_SETTING_NUMBER,1,1,4,1},
 {"chromatic","Lens","Chromatic separation","Optional red/blue edge separation. Off by default.",ANY_SETTING_BOOL,0,0,1,1},
 {"chromatic_amount","Lens","Separation (pixels)","Maximum color separation near screen edges.",ANY_SETTING_NUMBER,.5f,0,3,.25f},
 {"comparison","General","Before / after","Shows an original half beside the processed half for comparison.",ANY_SETTING_CHOICE,0,0,2,1},
 {"preset","General","Look","Choose a complete look. Custom restores your saved tuning; presets leave those values intact.",ANY_SETTING_CHOICE,0,0,5,1},
 {"advanced","General","Advanced tuning","Shows detailed controls for Custom. Hidden values are preserved.",ANY_SETTING_BOOL,0,0,1,1}
};
static_assert(std::size(definitions)==Count);
inline std::array<float,64> defaults(){std::array<float,64> v{};for(size_t i=0;i<Count;++i)v[i]=definitions[i].initial;return v;}
// Presets are complete conservative looks, derived without overwriting saved custom values.
inline std::array<float,64> effective(const std::array<float,64>& custom){
 int look=int(custom[Preset]);if(!look)return custom;auto v=defaults();
 v[Enabled]=custom[Enabled];v[GameplayOnly]=custom[GameplayOnly];v[Comparison]=custom[Comparison];v[Preset]=custom[Preset];v[Advanced]=custom[Advanced];
 switch(look){
 case 1: v[Antialias]=1;v[AAAmount]=.35f;v[SharpAmount]=.15f;break; // Natural
 case 2: v[Antialias]=2;v[AAAmount]=.65f;v[SharpAmount]=.1f;break; // Soft edges
 case 3: v[Antialias]=1;v[AAAmount]=.4f;v[SharpAmount]=.2f;v[Saturation]=1.1f;v[Vibrance]=.15f;v[Contrast]=1.05f;break;
 case 4: v[Antialias]=1;v[AAAmount]=.45f;v[SharpAmount]=.1f;v[Bloom]=1;v[BloomAmount]=.15f;v[BloomThreshold]=.8f;v[BloomWarmth]=.1f;v[Saturation]=.95f;v[Temperature]=.05f;v[Tone]=2;break;
 case 5: v[SharpAmount]=.15f;break; // Lightweight: sharpening alone
 }
 return v;
}
inline bool basic(size_t i){return i==Antialias||i==SharpAmount||i==Bloom||i==BloomAmount||i==Exposure||i==Saturation;}
inline float clean(size_t i,double value){auto& d=definitions[i];if(!std::isfinite(value))return d.initial;float v=float(std::clamp(value,double(d.minimum),double(d.maximum)));if(std::abs(v-d.initial)<d.step*.0001f)v=d.initial;return d.kind==ANY_SETTING_BOOL||d.kind==ANY_SETTING_CHOICE?std::round(v):v;}
}
