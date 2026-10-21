#pragma once
#include "anyhelpers_settings_v1.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace graphics_options {
enum Index {Enabled,GameplayOnly,Preset,Advanced,NativeAA,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,NativeShadows,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,AAQuality,Clouds,Grass,Foliage,Atmosphere,SunShafts,LightingQuality,GroundFog,ShaftIntensity,BeamFocus,LocalBeams,LocalIntensity,LocalBudget,ShaftClarity,BeamReach,SunResponse,TemporalLighting,Sharpening,CostReadout,ToneMapping,ColourLook,LookStrength,EyeAdaptation,Brightness,SceneBloom,SunGlare,Vignette,DetailedAO,NightSky,Moonlight,ContactShadows,BounceLight,DepthOfField,MotionBlur,DrawDistance,SunDisc,CloudLayer,FocusBlur,Count};
struct Option {const char* id;const char* group;const char* label;const char* description;uint32_t kind;float initial,minimum,maximum,step;};
static constexpr Option definitions[]={
 {"enabled","General","Enable AnyGraphics","Use the selected native graphics settings. Off restores the game's own settings.",ANY_SETTING_BOOL,1,0,1,1},
 {"gameplay_only","General","Gameplay only","Apply native overrides during gameplay; bypass them in menus and inventories.",ANY_SETTING_BOOL,1,0,1,1},
 {"quality_profile","General","Preset","Off keeps the game's look. Performance to Cinematic trade frame rate for looks; Natural, Clear Day, Vibrant and Moody are styles. Open the groups below for fine tuning; changing anything there switches to Custom, starting from your last preset.",ANY_SETTING_CHOICE,3,0,11,1},
 {"advanced","General","Advanced options","Show lighting strength and scene-detail choices.",ANY_SETTING_BOOL,0,0,1,1},
 {"native_aa","Scene","Antialiasing","Choose native FXAA or scene AA before the HUD. SMAA replaces FXAA; Enhanced SMAA adds subpixel smoothing. TAA blends recent frames to stop shimmer and jagged edges; Extra smooth keeps more of them and can trail slightly behind fast motion.",ANY_SETTING_CHOICE,2,0,6,1},
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
 {"vignette","Finish","Vignette","Darkens the corners of the screen.",ANY_SETTING_CHOICE,0,0,3,1},
 {"detailed_ao","Scene","Detailed shading","Replaces the game's ambient occlusion with softer, deeper contact shading in corners, under parts and between blocks. Needs Ambient occlusion to be on or left to the game.",ANY_SETTING_CHOICE,0,0,3,1},
 {"night_sky","Sky","Night sky","Stars, a milky way and a moon at night. They follow the game's own day and night: the stars turn with the sun and the moon rises as the sun sets.",ANY_SETTING_CHOICE,0,0,3,1},
 {"moonlight","Sky","Moonlight","Soft blue light from the moon on the ground and builds at night. The moon casts no shadows.",ANY_SETTING_CHOICE,0,0,3,1},
 {"contact_shadows","Scene","Contact shadows","Small, sharp sun shadows the game's shadows are too coarse for: under parts, between blocks and where things touch the ground.",ANY_SETTING_CHOICE,0,0,3,1},
 {"bounce_light","Scene","Bounce light","Light bouncing off lit surfaces onto nearby ones, tinted with their colour. Only what is on screen can bounce.",ANY_SETTING_CHOICE,0,0,3,1},
 {"depth_of_field","Scene","Depth of field","Blurs what is nearer or farther than the thing in the middle of the screen, like a camera lens. Focus follows where you look.",ANY_SETTING_CHOICE,0,0,3,1},
 {"motion_blur","Scene","Motion blur","Blurs the world as the camera turns and moves. Your own hands, held items and nearby cockpit parts stay sharp.",ANY_SETTING_CHOICE,0,0,3,1},
 {"draw_distance","Scene","Object draw distance","How far away characters, props, dropped items and vehicle parts keep being drawn. Terrain and trees follow the game's own render distance. Farther costs more GPU time in busy areas.",ANY_SETTING_CHOICE,0,0,3,1},
 {"sun_disc","Sky","Sun","A bright, round sun in the sky with a soft halo, coloured by the time of day.",ANY_SETTING_CHOICE,0,0,3,1},
 {"cloud_layer","Sky","Cloud layer","A high layer of drifting clouds above the game's own. Lit by the sun by day, glowing at sunrise and sunset, and moonlit at night.",ANY_SETTING_CHOICE,0,0,3,1},
 {"focus_blur","Scene","Blur strength","How strongly depth of field blurs what is out of focus. 1 is the level's normal blur; higher blurs more and farther.",ANY_SETTING_NUMBER,1,.25f,3,.05f}
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
inline constexpr float bounce_levels[]={0,.5f,1,1.5f};
inline constexpr float focus_blur_levels[]={0,.5f,1,1.6f},motion_blur_levels[]={0,.4f,.8f,1.2f},draw_distance_levels[]={1,1.5f,2,3},sun_disc_levels[]={0,.6f,1,1.5f},cloud_levels[]={0,.6f,1,1.5f};
inline constexpr float contact_levels[]={.6f,.3f,.6f,1};
inline constexpr float star_levels[]={0,.6f,1,1.5f},moon_levels[]={0,.7f,1,1.3f},moonlight_levels[]={0,.6f,1,1.6f};
inline constexpr float ao_radius_levels[]={1,.75f,1,1.4f},ao_strength_levels[]={.8f,.6f,.8f,1};
// Native AA choice -> scene AA method: 3 SMAA, 4 Enhanced SMAA, 5 TAA and 6 TAA (extra smooth) both jitter the camera,
// since TAA without jitter blends identical frames and smooths nothing. Extra smooth keeps one quality level more history.
inline uint32_t aa_method(float choice){return choice>=5?4:choice>=4?2:1;}
inline uint32_t aa_quality(float choice,float quality){return uint32_t(std::min(3.f,quality+(choice>=6?1.f:0.f)));}
// Menu order: new rows sit next to the controls they modify.
inline constexpr Index order[]={Enabled,GameplayOnly,Preset,Advanced,NativeAA,AAQuality,Sharpening,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,DetailedAO,NativeShadows,ContactShadows,BounceLight,DrawDistance,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,Clouds,Grass,Foliage,
 ToneMapping,ColourLook,LookStrength,Brightness,EyeAdaptation,SceneBloom,SunGlare,Vignette,DepthOfField,FocusBlur,MotionBlur,SunDisc,CloudLayer,NightSky,Moonlight,
 Atmosphere,SunShafts,ShaftIntensity,ShaftClarity,BeamReach,SunResponse,BeamFocus,LightingQuality,TemporalLighting,GroundFog,LocalBeams,LocalIntensity,LocalBudget,CostReadout};
static_assert(std::size(order)==Count);
template<size_t N> inline uint32_t nearest(float value,const float(&levels)[N]){uint32_t best=0;for(uint32_t i=1;i<N;++i)if(std::abs(value-levels[i])<std::abs(value-levels[best]))best=i;return best;}
// Menu groups: the top level holds the preset and a few main choices; the rest sit in fine-tuning groups that open
// and close.
enum Group {Top,Lighting,Shadows,Sky,AntiAliasing,Camera,Finish,Performance,GroupCount};
inline constexpr const char* group_names[GroupCount]={"","Lighting and sun shafts","Shadows and shading","Sky and weather","Anti-aliasing and sharpness","Camera and photo","Colour and finish","Performance"};
inline constexpr Group group_of(Index i){
 switch(i){
 case Atmosphere:case SunShafts:case ShaftIntensity:case ShaftClarity:case BeamReach:case SunResponse:case BeamFocus:case LightingQuality:case TemporalLighting:case GroundFog:case LocalBeams:case LocalIntensity:case LocalBudget:case SunLight:case SkyLight:case AmbientLight:return Lighting;
 case NativeShadows:case ContactShadows:case NativeSSAO:case DetailedAO:case BounceLight:return Shadows;
 case SunDisc:case CloudLayer:case NightSky:case Moonlight:case Clouds:case NativeFogBlur:return Sky;
 case AAQuality:case Sharpening:return AntiAliasing;
 case DepthOfField:case FocusBlur:case MotionBlur:return Camera;
 case ToneMapping:case LookStrength:case EyeAdaptation:case SceneBloom:case SunGlare:case Vignette:case NativeBloom:case BloomAmount:case BloomThreshold:case SceneExposure:return Finish;
 case Grass:case Foliage:return Performance;
 default:return Top;}}
// Preset choices: 0 Off, 1-6 Performance..Cinematic, then the looks and Custom.
inline constexpr int NaturalProfile=7,VibrantProfile=8,MoodyProfile=9,ClearDayProfile=10,CustomProfile=11;
inline Values profile(const Values& current,int level){
 if(!level)return current; // Off bypasses overrides and keeps the previous individual choices.
 // Custom starts from the current values so tweaks begin where the last preset left off.
 if(level==CustomProfile){auto v=current;v[Preset]=float(level);return v;}
 auto v=defaults();for(auto i:{Enabled,GameplayOnly,Advanced,CostReadout})v[i]=current[i];v[Preset]=float(level);
 // Looks start from a quality tier: Natural, Moody and Clear Day from High, Vibrant from Ultra.
 // Index 0..5: Performance, Low, Medium, High, Ultra, Cinematic.
 auto tier=size_t(level==VibrantProfile?4:level>=NaturalProfile?3:level-1);
 static constexpr float aa[]={2,3,5,5,6,6},aa_quality[]={1,1,2,2,3,3},sharpen[]={0,0,0,1,1,2};
 v[NativeAA]=aa[tier];v[AAQuality]=aa_quality[tier];v[Sharpening]=sharpen[tier];
 v[NativeSSAO]=level>=3?2.f:1.f;static constexpr float ao[]={0,0,0,2,3,3};v[DetailedAO]=ao[tier];static constexpr float night[]={0,1,2,2,3,3},moon[]={0,1,2,2,2,2};v[NightSky]=night[tier];v[Moonlight]=moon[tier];static constexpr float contact[]={0,0,1,2,3,3};v[ContactShadows]=contact[tier];static constexpr float bounce[]={0,0,0,2,3,3};v[BounceLight]=bounce[tier];static constexpr float dof[]={0,0,0,0,0,2},blur[]={0,0,0,0,0,1};v[DepthOfField]=dof[tier];v[MotionBlur]=blur[tier];static constexpr float object_reach[]={0,0,0,1,2,3};v[DrawDistance]=object_reach[tier];static constexpr float sun_levels[]={0,1,2,2,3,3},cloud_tiers[]={0,0,1,2,3,3};v[SunDisc]=sun_levels[tier];v[CloudLayer]=cloud_tiers[tier];v[NativeShadows]=level>=2?2.f:1.f;v[NativeFogBlur]=level>=3?2.f:1.f;
 static constexpr float bloom[]={0,.05f,.1f,.2f,.35f,.35f};v[NativeBloom]=level>=2?2.f:1.f;v[BloomAmount]=bloom[tier];v[BloomThreshold]=.75f;
 // Fog and beams are artistic strengths, not sample quality. High tiers add
 // samples and lights but keep fog light: no preset goes past Low volumetric
 // fog or Medium base fog, since thick fog washes the whole scene out.
 static constexpr float atmosphere[]={0,0,1,1,1,1},beams[]={0,1,2,3,4,4},local[]={0,1,2,2,3,3},quality[]={0,0,1,3,3,3};
 static constexpr float sun_intensity[]={1,1,1,1,1,1.5f},local_intensity[]={1,1,1,1,1,1.2f},focus[]={1,1,1,1,2,2},clarity[]={1,1,1,1,0,0},reach[]={0,0,1,1,2,2},fog[]={0,.35f,.65f,.65f,.65f,.65f};
 v[Atmosphere]=atmosphere[tier];v[SunShafts]=beams[tier];v[LocalBeams]=local[tier];v[LightingQuality]=v[LocalBudget]=quality[tier];
 v[ShaftIntensity]=sun_intensity[tier];v[LocalIntensity]=local_intensity[tier];v[BeamFocus]=focus[tier];v[GroundFog]=0;
 v[ShaftClarity]=clarity[tier];v[BeamReach]=reach[tier];v[SunResponse]=1;v[TemporalLighting]=1;
 // Finishing: Filmic from Low up, a subtle Vivid on High, Vivid on Ultra, a strong film grade on Cinematic.
 // High tiers turn the heavier effects up, but beams stay Clear (light only through gaps) so they never read as fog.
 static constexpr float tone[]={0,2,2,2,2,2},look[]={0,0,0,5,5,3},strength[]={1,1,1,0,1,2},scene_bloom[]={0,1,1,2,3,3},glare[]={0,1,1,2,3,3},vignette[]={0,0,0,0,1,2};
 v[ToneMapping]=tone[tier];v[ColourLook]=look[tier];v[LookStrength]=strength[tier];v[EyeAdaptation]=1;v[Brightness]=0;v[SceneBloom]=scene_bloom[tier];v[SunGlare]=glare[tier];v[Vignette]=vignette[tier];
 v[Clouds]=level==1?1.f:0.f;v[Grass]=level==1||level==2?1.f:0.f;v[Foliage]=0;v[FogDensity]=fog[tier];
 if(level==NaturalProfile){ // The game's own colours, only cleaner.
  v[ToneMapping]=3;v[ColourLook]=0;v[Vignette]=0;v[SceneBloom]=1;v[SunGlare]=1;v[Atmosphere]=0;v[SunShafts]=1;v[SunDisc]=1;v[CloudLayer]=1;v[NightSky]=1;v[Moonlight]=1;}
 else if(level==VibrantProfile){ // The bright shader-pack look: rich colour, big skies, clear beams.
  v[ColourLook]=5;v[LookStrength]=2;v[SceneBloom]=2;v[SunGlare]=2;v[SunShafts]=3;v[ShaftIntensity]=1.2f;v[ShaftClarity]=0;v[FogDensity]=.35f;v[SunDisc]=3;v[CloudLayer]=3;v[NightSky]=3;v[Moonlight]=2;v[BounceLight]=2;v[ContactShadows]=2;}
 else if(level==MoodyProfile){ // Golden-hour colour, strong beams and a little haze.
  v[ColourLook]=1;v[LookStrength]=2;v[Vignette]=2;v[SunShafts]=3;v[SceneBloom]=2;v[SunGlare]=2;v[CloudLayer]=3;v[SunDisc]=1;v[NightSky]=2;v[Moonlight]=1;}
 else if(level==ClearDayProfile){ // Crisp and clear: neutral colour, low haze, extra sharpness.
  v[ToneMapping]=3;v[ColourLook]=0;v[Vignette]=0;v[FogDensity]=.35f;v[Atmosphere]=0;v[SunShafts]=1;v[ShaftClarity]=0;v[BeamReach]=0;v[Sharpening]=2;v[SunDisc]=2;v[CloudLayer]=1;}
 return v;
}
inline Values effective(const Values& custom){return custom;}
inline float clean(size_t i,double value){auto& d=definitions[i];if(!std::isfinite(value))return d.initial;float v=float(std::clamp(value,double(d.minimum),double(d.maximum)));if(i==FogDensity)return fog_levels[nearest(v,fog_levels)];if(i==SunLight||i==SkyLight||i==AmbientLight)return light_levels[nearest(v,light_levels)];if(i==BloomAmount&&v>0)return bloom_levels[nearest(v,bloom_levels)];if(std::abs(v-d.initial)<d.step*.0001f)v=d.initial;return d.kind==ANY_SETTING_BOOL||d.kind==ANY_SETTING_CHOICE?std::round(v):v;}
}
