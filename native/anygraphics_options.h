#pragma once
#include "anyhelpers_settings_v1.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
namespace graphics_options {
enum Index {Enabled,GameplayOnly,Preset,Advanced,NativeAA,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,NativeShadows,NativeFogBlur,SunLight,SkyLight,AmbientLight,FogDensity,SceneExposure,AAQuality,Clouds,Grass,Foliage,Atmosphere,SunShafts,LightingQuality,GroundFog,ShaftIntensity,BeamFocus,LocalBeams,LocalIntensity,LocalBudget,ShaftClarity,BeamReach,SunResponse,TemporalLighting,Sharpening,CostReadout,ToneMapping,ColourLook,LookStrength,EyeAdaptation,Brightness,SceneBloom,SunGlare,Vignette,DetailedAO,NightSky,Moonlight,ContactShadows,BounceLight,DepthOfField,MotionBlur,DrawDistance,SunDisc,CloudLayer,FocusBlur,LightColour,WindStrength,WaterReflections,SunGlint,MoonSource,LightFollowsTime,WeatherResponse,
 // Hidden rows (group Hidden): settings.tsv only, never drawn. Testers flip them; Reset puts them back.
 AmdLampPath,CloudModel,ShadingRevision,ToneRevision,BloomFalloff,TaaStillV2,TaaThinRescue,KeepNativeSsao,PresetRevision,SkyModel,AaBeforeOverlays,TaaOcean,OutputDither,Count};
struct Option {const char* id;const char* group;const char* label;const char* description;uint32_t kind;float initial,minimum,maximum,step;};
static constexpr Option definitions[]={
 {"enabled","General","Enable AnyGraphics","Use the selected native graphics settings. Off restores the game's own settings.",ANY_SETTING_BOOL,1,0,1,1},
 {"gameplay_only","General","Gameplay only","Apply native overrides during gameplay; bypass them in menus and inventories.",ANY_SETTING_BOOL,1,0,1,1},
 {"quality_profile","General","Preset","Off keeps the game's look. Performance to Cinematic trade frame rate for looks; Natural, Clear Day, Vibrant, Moody and Soft & warm are styles. Changing anything in the groups below switches to Custom, starting from your last preset.",ANY_SETTING_CHOICE,3,0,12,1},
 {"advanced","General","Advanced options","Show lighting strength and scene-detail choices.",ANY_SETTING_BOOL,0,0,1,1},
 {"native_aa","Scene","Antialiasing","Choose native FXAA or scene AA before the HUD. SMAA replaces FXAA; Enhanced SMAA adds subpixel smoothing. TAA blends recent frames to stop shimmer and jagged edges; Extra smooth keeps more of them and can trail slightly behind fast motion.",ANY_SETTING_CHOICE,2,0,6,1},
 {"native_bloom","Scene","Bloom","Use the game setting, turn bloom off, or choose the strength of native bloom.",ANY_SETTING_CHOICE,2,0,2,1},
 {"bloom_amount","Bloom","Bloom intensity","Native bloom intensity when Bloom is Custom.",ANY_SETTING_NUMBER,.1f,0,2,.05f},
 {"bloom_threshold","Bloom","Bloom threshold","Native brightness threshold when Bloom is Custom.",ANY_SETTING_NUMBER,.75f,0,4,.025f},
 {"native_ssao","Scene","Ambient occlusion","Use the game's SSAO setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"native_shadows","Scene","Shadows","Use the game's shadow setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"native_fog_blur","Scene","Fog blur","Use the game's fog-blur setting or explicitly turn it off/on.",ANY_SETTING_CHOICE,2,0,2,1},
 {"sun_light","Scene","Sunlight","Scale sunlight before native scene constants are built. Game preserves the scene input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"sky_light","Scene","Sky light","Scale sky illumination without changing weather or time. Game preserves the input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"ambient_light","Scene","Ambient light","Scale ambient, view and back lighting before rendering. Game preserves the input.",ANY_SETTING_NUMBER,1,0,3,.05f},
 {"fog_density","Scene","Fog","Strength of the game's base fog, including its weather and time-of-day fog. Game keeps the game's own fog; Off removes it, which can show the edge of the render distance.",ANY_SETTING_NUMBER,.65f,0,3,.05f},
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
 {"tone_mapping","Finish","Tone mapping","Off keeps the game's look. Game curve keeps its curve; Filmic holds colour in bright light; Clean stays bright and true; Soft film rolls skies off gently. Any choice but Off replaces the game's bloom.",ANY_SETTING_CHOICE,0,0,4,1},
 {"colour_look","Finish","Colour look","A colour grade on top of tone mapping. Vivid is the bright shader-pack look; Soft & warm has golden light and cool blue shade.",ANY_SETTING_CHOICE,0,0,6,1},
 {"look_strength","Finish","Look strength","How strongly the colour look is applied.",ANY_SETTING_CHOICE,1,0,2,1},
 {"eye_adaptation","Finish","Eye adaptation","Briefly brightens dark places and dims bright ones, like your eyes adjusting, then settles back to the game's own day and night brightness.",ANY_SETTING_BOOL,1,0,1,1},
 {"brightness","Finish","Brightness (stops)","Overall exposure before tone mapping. 0 keeps the game's brightness.",ANY_SETTING_NUMBER,0,-2,2,.25f},
 {"scene_bloom","Finish","Bloom","Soft, wide glow around bright light, before tone mapping. Replaces the game's bloom while tone mapping is on.",ANY_SETTING_CHOICE,2,0,3,1},
 {"sun_glare","Finish","Sun glare","A glow and faint streaks around the sun when it is in view, dimmed when trees or terrain block it.",ANY_SETTING_CHOICE,2,0,3,1},
 {"vignette","Finish","Vignette","Darkens the corners of the screen.",ANY_SETTING_CHOICE,0,0,3,1},
 {"detailed_ao","Scene","Detailed shading","Replaces the game's ambient occlusion with softer, deeper contact shading in corners, under parts and between blocks. Needs Ambient occlusion to be on or left to the game.",ANY_SETTING_CHOICE,0,0,3,1},
 {"night_sky","Sky","Night sky","Stars, a milky way and a moon with phases on the game's own clock: the stars turn through the night and fade at dawn, and the moon rises at dusk and sets at dawn.",ANY_SETTING_CHOICE,0,0,3,1},
 {"moonlight","Sky","Moonlight","Blue light from the moon on the ground and builds at night. With Moonlight source Sky glow it has no shadows; Game light uses the game's own night light and shadows.",ANY_SETTING_CHOICE,0,0,3,1},
 {"contact_shadows","Scene","Contact shadows","Small, sharp sun shadows the game's shadows are too coarse for: under parts, between blocks and where things touch the ground.",ANY_SETTING_CHOICE,0,0,3,1},
 {"bounce_light","Scene","Bounce light","Light bouncing off lit surfaces onto nearby ones, tinted with their colour. Only what is on screen can bounce.",ANY_SETTING_CHOICE,0,0,3,1},
 {"depth_of_field","Scene","Depth of field","Blurs what is nearer or farther than the thing in the middle of the screen, like a camera lens. Focus follows where you look.",ANY_SETTING_CHOICE,0,0,3,1},
 {"motion_blur","Scene","Motion blur","Blurs the world as the camera turns and moves. Your own hands, held items and nearby cockpit parts stay sharp.",ANY_SETTING_CHOICE,0,0,3,1},
 {"draw_distance","Scene","Object draw distance","How far away characters, props, dropped items and vehicle parts keep being drawn. Terrain and trees follow the game's own render distance. Farther costs CPU and GPU time in busy areas; hosts may see a lower server tick.",ANY_SETTING_CHOICE,0,0,3,1},
 {"sun_disc","Sky","Sun","A bright, round sun in the sky with a soft halo, coloured by the time of day.",ANY_SETTING_CHOICE,0,0,3,1},
 {"cloud_layer","Sky","Cloud layer","A high layer of drifting clouds above the game's own. Lit by the sun by day, glowing at sunrise and sunset, and moonlit at night.",ANY_SETTING_CHOICE,0,0,3,1},
 {"focus_blur","Scene","Blur strength","How strongly depth of field blurs what is out of focus. 1 is the level's normal blur; higher blurs more and farther.",ANY_SETTING_NUMBER,1,.25f,3,.05f},
 {"light_colour","Lighting","Light colour","Golden sunlight and cool blue shade, put into the game's own lights rather than over the screen. Brightness stays the same.",ANY_SETTING_CHOICE,0,0,3,1},
 {"wind_strength","Sky","Wind in trees","How hard trees and grass sway. Only the look changes: vehicles, physics and other players are not affected.",ANY_SETTING_CHOICE,1,0,3,1},
 {"water_reflections","Sky","Water reflections","Boats, coasts and hills mirrored in the sea, on top of the game's sky reflection. Only what is on screen can reflect; higher levels trace more carefully and cost more GPU time.",ANY_SETTING_CHOICE,0,0,3,1},
 {"sun_glint","Sky","Sun glint","A bright, rippling path of sunlight on the sea while the sun is up, in the colour of the sunlight.",ANY_SETTING_CHOICE,0,0,1,1},
 // 0.33.0 rows (AnyAPI 0.40.2 scene controls v4, scene effects v3, scene lighting v4). Presets never change them.
 {"moon_source","Sky","Moonlight source","Sky glow is a soft glow on screen without shadows. Game light puts the moonlight into the game's own night light, so the moon casts the game's shadows, lights the fog and glints on the sea.",ANY_SETTING_CHOICE,0,0,1,1},
 {"light_follows_time","Lighting","Light colour follows time","Golden light at sunrise and sunset, neutral at noon, cool blue at dusk and blue moonlight at night, following the game's clock. Off keeps one colour all day.",ANY_SETTING_BOOL,0,0,1,1},
 {"weather_response","Sky","Weather response","Lets rain, snow and storms change AnyGraphics too: thicker fog and cloud, dimmer stars and sun, a rougher sea, and lightning that lights up the fog without the exposure pumping. Almost no GPU cost.",ANY_SETTING_BOOL,0,0,1,1},
 // Hidden tuning (defaults are the AnyAPI 0.40.2 service defaults; docs/mods/graphics.md explains how to edit them).
 {"amd_lamp_path","Hidden","AMD lamp path","Testing only: 0 automatic (= 3 until the sized shaders are confirmed on AMD), 1 constant-buffer lamps with shaders sized to the lamps in view, 2 the uniform-branch experiment, 3 the 0.40.1 single 8-lamp shader. Other graphics cards ignore it.",ANY_SETTING_CHOICE,0,0,3,1},
 {"cloud_model","Hidden","Cloud model","Testing only: 1 clouds v2 (default), 0 the AnyAPI 0.40.1 clouds.",ANY_SETTING_BOOL,1,0,1,1},
 {"shading_revision","Hidden","Shading revision","Testing only: 1 the 0.40.2 detailed shading and bounce light (default), 0 the 0.40.1 versions.",ANY_SETTING_BOOL,1,0,1,1},
 {"tone_revision","Hidden","Filmic revision","Testing only: 1 Filmic with more midtone contrast, 0 the 0.40.1 Filmic (default).",ANY_SETTING_BOOL,0,0,1,1},
 {"bloom_falloff","Hidden","Bloom falloff","Testing only: 1 weights every bloom level the same (default); lower values weaken the widest glow.",ANY_SETTING_NUMBER,1,.5f,1,.05f},
 {"taa_still_v2","Hidden","TAA history v2","Testing only: 1 the 0.40.2 TAA history (Catmull-Rom resampling and the still/moving pixel rule, default), 0 the 0.40.1 history.",ANY_SETTING_BOOL,1,0,1,1},
 {"taa_thin_rescue","Hidden","TAA thin rescue","Testing only: 1 keeps thin wires and grass in the TAA history (experiment), 0 off (default).",ANY_SETTING_BOOL,0,0,1,1},
 {"keep_native_ssao","Hidden","Keep game SSAO","Testing only: 1 runs the game's ambient occlusion even when Detailed shading replaces it, 0 skips it (default).",ANY_SETTING_BOOL,0,0,1,1},
 {"preset_revision","Hidden","Preset revision","Internal: the preset tables these settings were saved with. Do not edit.",ANY_SETTING_INTEGER,1,0,1000,1},
 {"sky_model","Hidden","Night sky model","Testing only: 1 the night sky on the game clock (stars all night, moon phases, dawn fade; default), 0 the AnyAPI 0.40.1 night sky.",ANY_SETTING_BOOL,1,0,1,1},
 {"aa_before_overlays","Hidden","AA before overlays","Testing only: 1 runs scene AA before the editor, placement and inspection overlays (default), 0 the 0.40.1 placement after them, where TAA pauses while an overlay shows.",ANY_SETTING_BOOL,1,0,1,1},
 {"taa_ocean","Hidden","TAA sea surface","Testing only: 1 TAA reprojects ocean pixels on the sea surface (default), 0 with the depth buffer as in 0.40.1.",ANY_SETTING_BOOL,1,0,1,1},
 {"output_dither","Hidden","Output dither","Testing only: 1 adds about one step of dither to the finish and scene AA output against banding (default), 0 off.",ANY_SETTING_BOOL,1,0,1,1}
};
static_assert(std::size(definitions)==Count);
// K37: State::add rejects a description over 255 bytes or a label over 127, which would disable the whole mod.
consteval bool definitions_fit(){for(auto& d:definitions)if(std::char_traits<char>::length(d.description)>255||std::char_traits<char>::length(d.label)>127)return false;return true;}
static_assert(definitions_fit(),"an AnyGraphics setting description is over 255 bytes or a label over 127");
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
// Light colour: key (sun side) and shade tints per level; AnyAPI keeps each light's brightness.
inline constexpr float key_tints[4][3]={{1,1,1},{1.04f,1,.94f},{1.08f,1,.88f},{1.12f,1,.82f}},shade_tints[4][3]={{1,1,1},{.96f,.99f,1.05f},{.92f,.98f,1.1f},{.88f,.97f,1.15f}};
inline constexpr float wind_levels[]={.5f,1,1.6f,2.5f};
inline constexpr float ao_radius_levels[]={1,.75f,1,1.4f},ao_strength_levels[]={.8f,.6f,.8f,1};
// Native AA choice -> scene AA method: 3 SMAA, 4 Enhanced SMAA, 5 TAA and 6 TAA (extra smooth) both jitter the camera,
// since TAA without jitter blends identical frames and smooths nothing. Extra smooth keeps one quality level more history.
inline uint32_t aa_method(float choice){return choice>=5?4:choice>=4?2:1;}
inline uint32_t aa_quality(float choice,float quality){return uint32_t(std::min(3.f,quality+(choice>=6?1.f:0.f)));}
// Scene AA v3 (AnyAPI 0.40.2): Extra smooth keeps the chosen SMAA quality and adds one TAA history level instead, so it
// also smooths more at Ultra (K47). Older APIs keep aa_quality() above.
inline uint32_t aa_history_bias(float choice){return choice>=6?1u:0u;}
// Light colour follows time (scene controls v4): key and shade tints for noon, golden hour, blue hour and moonlight, scaled
// by the Light colour level. Every channel stays inside the service's .5..1.5.
enum Palette:uint32_t{NoonKey,NoonShade,GoldenKey,GoldenShade,BlueKey,BlueShade,MoonKey,MoonShade,PaletteCount};
inline constexpr float palette_tints[PaletteCount][3]={{1.02f,1,.97f},{.98f,.99f,1.03f},{1.15f,1,.78f},{.9f,.97f,1.12f},{.95f,.98f,1.08f},{.9f,.96f,1.15f},{.82f,.92f,1.2f},{.88f,.95f,1.15f}};
inline constexpr float palette_scale[]={0,.5f,1,1.4f};
inline float palette(Palette p,int c,float light_colour){return 1+palette_scale[std::clamp(int(light_colour),0,3)]*(palette_tints[p][c]-1);}
// Moonlight source Game light: the Moonlight level as an absolute night key-light target (scene controls v4, 0..0.03).
inline constexpr float moon_target=.012f;
// Menu order: new rows sit next to the controls they modify.
inline constexpr Index order[]={Enabled,GameplayOnly,Preset,Advanced,NativeAA,AAQuality,Sharpening,NativeBloom,BloomAmount,BloomThreshold,NativeSSAO,DetailedAO,NativeShadows,ContactShadows,BounceLight,DrawDistance,NativeFogBlur,SunLight,SkyLight,AmbientLight,LightColour,LightFollowsTime,FogDensity,SceneExposure,Clouds,Grass,Foliage,WindStrength,
 ToneMapping,ColourLook,LookStrength,Brightness,EyeAdaptation,SceneBloom,SunGlare,Vignette,DepthOfField,FocusBlur,MotionBlur,SunDisc,CloudLayer,WaterReflections,SunGlint,NightSky,Moonlight,MoonSource,WeatherResponse,
 Atmosphere,SunShafts,ShaftIntensity,ShaftClarity,BeamReach,SunResponse,BeamFocus,LightingQuality,TemporalLighting,GroundFog,LocalBeams,LocalIntensity,LocalBudget,CostReadout,
 AmdLampPath,CloudModel,ShadingRevision,ToneRevision,BloomFalloff,TaaStillV2,TaaThinRescue,KeepNativeSsao,PresetRevision,SkyModel,AaBeforeOverlays,TaaOcean,OutputDither};
static_assert(std::size(order)==Count);
template<size_t N> inline uint32_t nearest(float value,const float(&levels)[N]){uint32_t best=0;for(uint32_t i=1;i<N;++i)if(std::abs(value-levels[i])<std::abs(value-levels[best]))best=i;return best;}
// Menu groups: the top level holds the preset and a few main choices; the rest sit in fine-tuning groups that open
// and close.
enum Group {Top,Lighting,Shadows,Sky,AntiAliasing,Camera,Finish,Performance,GroupCount};
inline constexpr const char* group_names[GroupCount]={"","Lighting and sun shafts","Shadows and shading","Sky and weather","Anti-aliasing and sharpness","Camera and photo","Colour and finish","Performance"};
inline constexpr Group group_of(Index i){
 switch(i){
 case Atmosphere:case SunShafts:case ShaftIntensity:case ShaftClarity:case BeamReach:case SunResponse:case BeamFocus:case LightingQuality:case TemporalLighting:case GroundFog:case LocalBeams:case LocalIntensity:case LocalBudget:case SunLight:case SkyLight:case AmbientLight:case LightColour:case LightFollowsTime:return Lighting;
 case NativeShadows:case ContactShadows:case NativeSSAO:case DetailedAO:case BounceLight:return Shadows;
 case SunDisc:case CloudLayer:case NightSky:case Moonlight:case Clouds:case NativeFogBlur:case WindStrength:case WaterReflections:case SunGlint:case MoonSource:case WeatherResponse:return Sky;
 case AAQuality:case Sharpening:return AntiAliasing;
 case DepthOfField:case FocusBlur:case MotionBlur:return Camera;
 case ToneMapping:case LookStrength:case EyeAdaptation:case SceneBloom:case SunGlare:case Vignette:case NativeBloom:case BloomAmount:case BloomThreshold:case SceneExposure:return Finish;
 case Grass:case Foliage:return Performance;
 default:return Top;}}
// AG-3: rows no menu loop draws. The old numeric bloom and exposure rows are replaced by named levels, Advanced is unused,
// and the hidden tuning rows live in settings.tsv only.
inline constexpr bool hidden(Index i){return i==BloomAmount||i==BloomThreshold||i==SceneExposure||i==Advanced||(i>=AmdLampPath&&i<Count);}
// Rows no preset changes: the switches, the 0.33.0 behaviour rows and the hidden rows. Changing one keeps the preset.
inline constexpr bool preset_keeps(Index i){return i==Enabled||i==GameplayOnly||i==Advanced||i==CostReadout||i==MoonSource||i==LightFollowsTime||i==WeatherResponse||(i>=AmdLampPath&&i<Count);}
// Preset choices: 0 Off, 1-6 Performance..Cinematic, then the looks, Custom (11) and Soft & warm (12, added after Custom so saved choices keep their meaning).
inline constexpr int NaturalProfile=7,VibrantProfile=8,MoodyProfile=9,ClearDayProfile=10,CustomProfile=11,SoftWarmProfile=12;
// Index 0..5: Performance, Low, Medium, High, Ultra, Cinematic. Looks start from a quality tier: Natural, Moody, Clear
// Day and Soft & warm from High, Vibrant from Ultra.
inline size_t tier_of(int level){return size_t(level==VibrantProfile?4:level>=NaturalProfile?3:level-1);}
// Everything a quality tier sets. Rows presets keep come from current.
inline Values tier_values(const Values& current,size_t tier){
 auto v=defaults();for(size_t i=0;i<Count;++i)if(preset_keeps(Index(i)))v[i]=current[i];
 static constexpr float aa[]={2,3,5,5,6,6},aa_quality[]={1,1,2,2,3,3},sharpen[]={0,0,1,1,1,2};
 v[NativeAA]=aa[tier];v[AAQuality]=aa_quality[tier];v[Sharpening]=sharpen[tier];
 v[NativeSSAO]=tier>=2?2.f:1.f;static constexpr float ao[]={0,0,0,2,3,3};v[DetailedAO]=ao[tier];static constexpr float night[]={0,1,2,2,3,3},moon[]={0,1,2,2,2,2};v[NightSky]=night[tier];v[Moonlight]=moon[tier];static constexpr float contact[]={0,0,1,2,3,3};v[ContactShadows]=contact[tier];static constexpr float bounce[]={0,0,0,2,3,3};v[BounceLight]=bounce[tier];static constexpr float dof[]={0,0,0,0,0,2},blur[]={0,0,0,0,0,1};v[DepthOfField]=dof[tier];v[MotionBlur]=blur[tier];static constexpr float object_reach[]={0,0,0,1,2,3};v[DrawDistance]=object_reach[tier];static constexpr float sun_levels[]={0,1,2,2,3,3},cloud_tiers[]={0,0,1,2,3,3};v[SunDisc]=sun_levels[tier];v[CloudLayer]=cloud_tiers[tier];static constexpr float water[]={0,0,0,2,3,3},glint[]={0,0,0,1,1,1};v[WaterReflections]=water[tier];v[SunGlint]=glint[tier];v[NativeShadows]=tier>=1?2.f:1.f;v[NativeFogBlur]=tier>=2?2.f:1.f;
 static constexpr float bloom[]={0,.05f,.1f,.2f,.35f,.35f};v[NativeBloom]=tier>=1?2.f:1.f;v[BloomAmount]=bloom[tier];v[BloomThreshold]=.75f;
 // Fog and beams are artistic strengths, not sample quality. High tiers add samples and lights but keep fog light: no
 // preset goes past Low volumetric fog or the Reduced base fog, since thick fog washes the whole scene out. Every tier
 // keeps the Reduced base fog (AG-9): 0 removed the game's weather fog and saved no GPU time.
 // AG-7: sampling quality and the lamp budget are separate tables, so High is not as costly as Ultra.
 static constexpr float atmosphere[]={0,0,1,1,1,1},beams[]={0,1,2,3,4,4},local[]={0,1,2,2,3,3},quality[]={0,0,1,2,2,3},budget[]={0,0,1,2,3,3};
 static constexpr float sun_intensity[]={1,1,1,1,1,1.5f},local_intensity[]={1,1,1,1,1,1.2f},focus[]={1,1,1,1,2,2},clarity[]={1,1,1,1,0,0},reach[]={0,0,1,1,2,2};
 v[Atmosphere]=atmosphere[tier];v[SunShafts]=beams[tier];v[LocalBeams]=local[tier];v[LightingQuality]=quality[tier];v[LocalBudget]=budget[tier];
 v[ShaftIntensity]=sun_intensity[tier];v[LocalIntensity]=local_intensity[tier];v[BeamFocus]=focus[tier];v[GroundFog]=0;
 v[ShaftClarity]=clarity[tier];v[BeamReach]=reach[tier];v[SunResponse]=1;v[TemporalLighting]=1;
 // Finishing: Filmic from Low up, a subtle Vivid on High, Vivid on Ultra, a strong film grade on Cinematic.
 // High tiers turn the heavier effects up, but beams stay Clear (light only through gaps) so they never read as fog.
 static constexpr float tone[]={0,2,2,2,2,2},look[]={0,0,0,5,5,3},strength[]={1,1,1,0,1,2},scene_bloom[]={0,1,1,2,3,3},glare[]={0,1,1,2,3,3},vignette[]={0,0,0,0,1,2};
 v[ToneMapping]=tone[tier];v[ColourLook]=look[tier];v[LookStrength]=strength[tier];v[EyeAdaptation]=1;v[Brightness]=0;v[SceneBloom]=scene_bloom[tier];v[SunGlare]=glare[tier];v[Vignette]=vignette[tier];
 v[Clouds]=tier==0?1.f:0.f;v[Grass]=tier<=1?1.f:0.f;v[Foliage]=0;v[FogDensity]=.65f;
 return v;}
// The looks: colour and mood on top of a tier. A look never costs more than its tier: rows with a GPU cost are capped at
// the tier's own value (on the preset tiers this changes nothing; it matters when auto-pick runs a look at a lower tier).
inline void apply_style(int level,Values& v){
 const auto tier=v;
 if(level==NaturalProfile){ // The game's own colours, only cleaner. No detailed (GTAO) shading; light shafts need fewer samples.
  v[ToneMapping]=3;v[ColourLook]=0;v[Vignette]=0;v[SceneBloom]=1;v[SunGlare]=1;v[Atmosphere]=0;v[SunShafts]=1;v[SunDisc]=1;v[CloudLayer]=1;v[NightSky]=1;v[Moonlight]=1;v[DetailedAO]=0;v[LightingQuality]=1;}
 else if(level==VibrantProfile){ // The bright shader-pack look: rich colour, big skies, clear beams.
  v[ColourLook]=5;v[LookStrength]=2;v[SceneBloom]=2;v[SunGlare]=2;v[SunShafts]=3;v[ShaftIntensity]=1.2f;v[ShaftClarity]=0;v[FogDensity]=.35f;v[SunDisc]=3;v[CloudLayer]=3;v[NightSky]=3;v[Moonlight]=2;v[BounceLight]=2;v[ContactShadows]=2;}
 else if(level==MoodyProfile){ // Golden-hour colour, strong beams and a little haze.
  v[ColourLook]=1;v[LookStrength]=2;v[LightColour]=1;v[Vignette]=2;v[SunShafts]=3;v[SceneBloom]=2;v[SunGlare]=2;v[CloudLayer]=3;v[SunDisc]=1;v[NightSky]=2;v[Moonlight]=1;}
 else if(level==ClearDayProfile){ // Crisp and clear: neutral colour, low haze, extra sharpness.
  v[ToneMapping]=3;v[ColourLook]=0;v[Vignette]=0;v[FogDensity]=.35f;v[Atmosphere]=0;v[SunShafts]=1;v[ShaftClarity]=0;v[BeamReach]=0;v[Sharpening]=2;v[SunDisc]=2;v[CloudLayer]=1;v[LightingQuality]=1;}
 else if(level==SoftWarmProfile){ // Complementary-style: soft film curve, golden light, cool shade, soft glow; no detailed (GTAO) shading.
  v[ToneMapping]=4;v[ColourLook]=6;v[LookStrength]=1;v[LightColour]=2;v[SceneBloom]=2;v[SunGlare]=2;v[Vignette]=1;v[DetailedAO]=0;v[SunShafts]=2;v[ShaftClarity]=0;v[CloudLayer]=2;v[SunDisc]=2;v[NightSky]=2;v[Moonlight]=2;}
 for(auto i:{Atmosphere,SunShafts,LocalBeams,LightingQuality,LocalBudget,DetailedAO,ContactShadows,BounceLight,WaterReflections,SunGlint,DrawDistance,DepthOfField,MotionBlur})v[i]=std::min(v[i],tier[i]);
}
inline bool is_look(int level){return level>=NaturalProfile&&level!=CustomProfile&&level<=SoftWarmProfile;}
inline Values profile(const Values& current,int level){
 if(!level)return current; // Off bypasses overrides and keeps the previous individual choices.
 // Custom starts from the current values so tweaks begin where the last preset left off.
 if(level==CustomProfile){auto v=current;v[Preset]=float(level);return v;}
 auto v=tier_values(current,tier_of(level));v[Preset]=float(level);apply_style(level,v);return v;
}
// AG-2: what a fresh install and the menu's Reset get. Preset's own initial value is Balanced.
inline Values balanced(){return profile(defaults(),3);}
// Graphics that share the system's memory: anything D3D12 reports as UMA, Intel, and the Steam Deck APUs (LCD and OLED).
// A fresh install there starts on Low instead of Balanced.
inline bool integrated_gpu(uint32_t vendor,uint32_t device,uint32_t uma){return uma!=0||vendor==0x8086||(vendor==0x1002&&(device==0x163f||device==0x1435));}
// K37: preset tables changed after settings were saved. Revision 1 (AnyGraphics 0.33.0): AG-7 sampling and lamp tables,
// K47 Balanced sharpening, Natural without detailed shading, Performance and Low fog (AG-9).
inline constexpr int preset_revision=1;
// Re-stages, on a committed preset (1-10, 12), only the rows the preset owns whose table changed since `from`. These are
// all group rows, which cannot differ from the table without the preset turning Custom, so they follow the new table. Top
// rows (Antialiasing, Fog and the other main choices) are the user's and stay. Returns how many rows changed.
// Rows that are new in a revision are not taken: they start off for a saved preset (the water rows, WATER-13, stay off
// until the preset is chosen again or reset), so an update never switches on a new pass by itself.
inline uint32_t restage(Values& committed,int from){
 const int level=int(committed[Preset]);if(level<1||level>SoftWarmProfile||level==CustomProfile||from>=preset_revision)return 0;
 const auto next=profile(committed,level);uint32_t changed=0;auto take=[&](Index i){if(committed[i]!=next[i]){committed[i]=next[i];++changed;}};
 if(from<1)for(auto i:{LightingQuality,LocalBudget,Sharpening,DetailedAO})take(i);
 return changed;
}
// K35 auto-pick: one measured window of gameplay at a tier (1 Performance .. 5 Ultra).
// pass_ms: AnyAPI GPU pass sum; present_block_ms: time inside the game's Present (scene timing v2); p05_ms: the fastest
// 5% of frames. -1 when unknown.
struct PickWindow {float median_ms{-1},p95_ms{-1},pass_ms{-1},present_block_ms{-1},p05_ms{-1};};
enum PickReason:uint32_t{PickHolds,PickSlow,PickConfirm,PickFast,PickRoom,PickCapFull,PickCpuBound,PickTop,PickBottom};
struct PickDecision {int tier;bool done;PickReason reason;};
// A refresh cap: frame times this steady, at a common refresh period, mean vsync or a limiter decides the frame rate.
// 50, 45 and 40 Hz are Steam Deck limits; the climb path never sees them (it only runs at 55 fps and up).
inline float pick_cap(const PickWindow& w){static constexpr float periods[]={6.944f,8.333f,11.111f,13.333f,16.667f,20.f,22.222f,25.f,33.333f};if(!(w.median_ms>0)||w.p95_ms>w.median_ms*1.03f)return 0;for(float p:periods)if(std::abs(w.median_ms-p)<=p*.02f)return p;return 0;}
// Slow is under 55 fps typical or under 40 fps for the slowest 5% of frames. A slow tier steps down only after a second
// slow window. When AnyAPI's passes are under 15% of the frame the game or CPU sets the frame rate: it stops there and
// never ends below Balanced from a start at Balanced or above. Under a cap it climbs while the passes stay under 30% of
// the frame; otherwise it climbs at 80 fps. With scene timing v2 the CPU-bound call also needs the game to wait in
// Present for under 15% of the frame: a GPU-bound frame (a low-end integrated GPU busy with the game's own drawing)
// waits there, and stepping down helps it. Without that reading the pass share decides alone. A frame-rate limit under
// 55 fps (a Steam Deck 30 or 40 Hz limit, Battery Boost, half-rate vsync) also waits in Present, so it does not count as
// GPU-bound: a limit holds the fastest frames at the period too, while vsync rounding a GPU-bound frame mixes in faster
// frames. Without the fastest-5% reading nothing counts as a limit.
inline PickDecision pick_decide(int tier,int start,bool went_down,bool confirming,const PickWindow& w){
 const bool known=w.pass_ms>0,slow=w.median_ms>1000/55.f||w.p95_ms>1000/40.f;const float share=known?w.pass_ms/w.median_ms:1;
 const float cap=pick_cap(w);const bool limited=cap>0&&w.p05_ms>0&&w.p05_ms>=.97f*cap;
 const bool gpu_waits=!limited&&w.present_block_ms>=0&&w.present_block_ms>=.15f*w.median_ms;
 if(slow){
  if(known&&share<.15f&&!gpu_waits)return {std::max(tier,std::min(start,3)),true,PickCpuBound};
  if(tier<=1)return {tier,true,PickBottom};
  if(!confirming)return {tier,false,PickConfirm};
  return {tier-1,false,PickSlow};}
 if(went_down)return {tier,true,PickHolds};
 if(tier>=5)return {tier,true,PickTop};
 if(cap){if(known&&w.pass_ms<=.3f*cap)return {tier+1,false,PickRoom};return {tier,true,PickCapFull};}
 if(w.median_ms<=1000/80.f&&w.p95_ms<=1000/60.f)return {tier+1,false,PickFast};
 return {tier,true,PickHolds};
}
inline Values effective(const Values& custom){return custom;}
inline float clean(size_t i,double value){auto& d=definitions[i];if(!std::isfinite(value))return d.initial;float v=float(std::clamp(value,double(d.minimum),double(d.maximum)));if(i==FogDensity)return fog_levels[nearest(v,fog_levels)];if(i==SunLight||i==SkyLight||i==AmbientLight)return light_levels[nearest(v,light_levels)];if(i==BloomAmount&&v>0)return bloom_levels[nearest(v,bloom_levels)];if(std::abs(v-d.initial)<d.step*.0001f)v=d.initial;return d.kind==ANY_SETTING_BOOL||d.kind==ANY_SETTING_CHOICE||d.kind==ANY_SETTING_INTEGER?std::round(v):v;}
}
