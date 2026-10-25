#include "anymaker_mod_api.h"
#include <type_traits>
#include <cassert>
#include <iostream>
static_assert(std::is_standard_layout_v<AnymakerModContextV16>);
static_assert(offsetof(AnymakerModContextV16,register_inventory_pulse)==sizeof(AnymakerModContextV15));
static_assert(ANYMAKER_MOD_API_VERSION==16);
#include "anyapi_scene_lighting_v4.h"
#include "anyapi_scene_effects_v3.h"
#include "anyapi_scene_controls_v4.h"
#include "anyapi_scene_antialiasing_v3.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_gpu_info_v2.h"
#include <cstddef>
// AnyAPI 0.40.2 service versions (frame-core). These layouts are frozen: a change here breaks shipped mods.
static_assert(std::is_standard_layout_v<AnySceneLightingParametersV4>);
static_assert(std::is_standard_layout_v<AnySceneLightingStatusV2>);
static_assert(std::is_standard_layout_v<AnySceneLightingV4>);
static_assert(std::is_standard_layout_v<AnySceneEffectsParametersV3>);
static_assert(std::is_standard_layout_v<AnySceneEffectsStatusV3>);
static_assert(std::is_standard_layout_v<AnySceneEffectsV3>);
static_assert(std::is_standard_layout_v<AnySceneParametersV4>);
static_assert(std::is_standard_layout_v<AnySceneStatusV2>);
static_assert(std::is_standard_layout_v<AnySceneControlsV4>);
static_assert(std::is_standard_layout_v<AnySceneAntialiasingParametersV3>);
static_assert(std::is_standard_layout_v<AnySceneAntialiasingStatusV2>);
static_assert(std::is_standard_layout_v<AnySceneAntialiasingV3>);
static_assert(std::is_standard_layout_v<AnySceneTimingV2>);
static_assert(std::is_standard_layout_v<AnySceneTimingServiceV2>);
static_assert(std::is_standard_layout_v<AnyGpuInfoV2>);
static_assert(std::is_standard_layout_v<AnyGpuInfoServiceV2>);
static_assert(sizeof(AnySceneLightingParametersV4)==156);static_assert(offsetof(AnySceneLightingParametersV4,shaped)==8);static_assert(offsetof(AnySceneLightingParametersV4,lamp_model)==92);static_assert(offsetof(AnySceneLightingParametersV4,weather_response)==112);static_assert(offsetof(AnySceneLightingParametersV4,flags)==120);static_assert(offsetof(AnySceneLightingParametersV4,amd_path)==128);static_assert(offsetof(AnySceneLightingParametersV4,atmosphere_flags)==132);static_assert(offsetof(AnySceneLightingParametersV4,sky_inscatter)==136);static_assert(offsetof(AnySceneLightingParametersV4,reserved)==140);
static_assert(sizeof(AnySceneLightingStatusV2)==168);static_assert(offsetof(AnySceneLightingStatusV2,base)==8);static_assert(offsetof(AnySceneLightingStatusV2,lamps_drawn)==72);static_assert(offsetof(AnySceneLightingStatusV2,lamp_budget_max)==96);static_assert(offsetof(AnySceneLightingStatusV2,atmosphere_flags_supported)==120);static_assert(offsetof(AnySceneLightingStatusV2,raw29_hits)==128);static_assert(offsetof(AnySceneLightingStatusV2,capture_us)==144);static_assert(offsetof(AnySceneLightingStatusV2,reserved)==148);
static_assert(sizeof(AnySceneLightingV4)==32);static_assert(offsetof(AnySceneLightingV4,set)==8);static_assert(offsetof(AnySceneLightingV4,status_v2)==24);
static_assert(sizeof(AnySceneEffectsParametersV3)==196);static_assert(offsetof(AnySceneEffectsParametersV3,water)==8);static_assert(offsetof(AnySceneEffectsParametersV3,sky_model)==144);static_assert(offsetof(AnySceneEffectsParametersV3,bloom_falloff)==168);static_assert(offsetof(AnySceneEffectsParametersV3,flags)==176);static_assert(offsetof(AnySceneEffectsParametersV3,reserved)==180);
static_assert(sizeof(AnySceneEffectsStatusV3)==272);static_assert(offsetof(AnySceneEffectsStatusV3,base)==8);static_assert(offsetof(AnySceneEffectsStatusV3,pass_error)==160);static_assert(offsetof(AnySceneEffectsStatusV3,sky_model_active)==224);static_assert(offsetof(AnySceneEffectsStatusV3,sun_visibility)==248);static_assert(offsetof(AnySceneEffectsStatusV3,reserved)==252);
static_assert(sizeof(AnySceneEffectsV3)==40);static_assert(offsetof(AnySceneEffectsV3,status_v3)==32);
static_assert(sizeof(AnySceneParametersV4)==272);static_assert(offsetof(AnySceneParametersV4,looked)==8);static_assert(offsetof(AnySceneParametersV4,tint_targets)==124);static_assert(offsetof(AnySceneParametersV4,noon_key)==132);static_assert(offsetof(AnySceneParametersV4,moon_shade)==216);static_assert(offsetof(AnySceneParametersV4,moon_mode)==228);static_assert(offsetof(AnySceneParametersV4,weather_fog_keep)==240);static_assert(offsetof(AnySceneParametersV4,shadow_split)==244);static_assert(offsetof(AnySceneParametersV4,grass_far)==252);static_assert(offsetof(AnySceneParametersV4,reserved)==256);
static_assert(sizeof(AnySceneStatusV2)==128);static_assert(offsetof(AnySceneStatusV2,base)==8);static_assert(offsetof(AnySceneStatusV2,clock_source)==48);static_assert(offsetof(AnySceneStatusV2,t)==68);static_assert(offsetof(AnySceneStatusV2,wet)==100);static_assert(offsetof(AnySceneStatusV2,flash_frames)==104);static_assert(offsetof(AnySceneStatusV2,reserved)==108);
static_assert(sizeof(AnySceneControlsV4)==32);static_assert(offsetof(AnySceneControlsV4,status_v2)==24);
static_assert(sizeof(AnySceneAntialiasingParametersV3)==60);static_assert(offsetof(AnySceneAntialiasingParametersV3,base)==8);static_assert(offsetof(AnySceneAntialiasingParametersV3,history_bias)==32);static_assert(offsetof(AnySceneAntialiasingParametersV3,taa_flags)==36);static_assert(offsetof(AnySceneAntialiasingParametersV3,dither)==40);static_assert(offsetof(AnySceneAntialiasingParametersV3,reserved)==44);
static_assert(sizeof(AnySceneAntialiasingStatusV2)==112);static_assert(offsetof(AnySceneAntialiasingStatusV2,base)==8);static_assert(offsetof(AnySceneAntialiasingStatusV2,temporal_active)==56);static_assert(offsetof(AnySceneAntialiasingStatusV2,jittered_frames)==72);static_assert(offsetof(AnySceneAntialiasingStatusV2,gpu_ms)==88);static_assert(offsetof(AnySceneAntialiasingStatusV2,reserved)==92);
static_assert(sizeof(AnySceneAntialiasingV3)==32);static_assert(offsetof(AnySceneAntialiasingV3,status_v2)==24);
static_assert(sizeof(AnySceneTimingV2)==168);static_assert(offsetof(AnySceneTimingV2,v1)==8);static_assert(offsetof(AnySceneTimingV2,frame_index)==32);static_assert(offsetof(AnySceneTimingV2,frame_ms_p50)==40);static_assert(offsetof(AnySceneTimingV2,sync_interval)==56);static_assert(offsetof(AnySceneTimingV2,pass_ms)==64);static_assert(offsetof(AnySceneTimingV2,overlay_ms_p95)==128);static_assert(offsetof(AnySceneTimingV2,native_reads)==136);static_assert(offsetof(AnySceneTimingV2,flags)==148);static_assert(offsetof(AnySceneTimingV2,reserved)==152);
static_assert(sizeof(AnySceneTimingServiceV2)==24);static_assert(offsetof(AnySceneTimingServiceV2,copy_v2)==16);
static_assert(sizeof(AnyGpuInfoV2)==208);static_assert(offsetof(AnyGpuInfoV2,v1)==8);static_assert(offsetof(AnyGpuInfoV2,uma)==32);static_assert(offsetof(AnyGpuInfoV2,width)==52);static_assert(offsetof(AnyGpuInfoV2,lamp_shadows_supported)==60);static_assert(offsetof(AnyGpuInfoV2,adapter_name)==64);static_assert(offsetof(AnyGpuInfoV2,reserved)==192);
static_assert(sizeof(AnyGpuInfoServiceV2)==24);static_assert(offsetof(AnyGpuInfoServiceV2,copy_v2)==16);
static_assert(ANY_PASS_COUNT==16&&ANY_PASS_OVERLAY==9&&ANY_LAMP_OCEAN==8&&ANY_ATMOS_FLASH_GLOW==64&&ANY_FX_KEEP_NATIVE_SSAO==1&&ANY_TAA_THIN_RESCUE==4&&ANY_AA_FALLBACK_BINDINGS==8);
int main() {
    assert(sizeof(void*)==8);
    assert(offsetof(AnymakerUiRenderFrameV1,visible_panels)>offsetof(AnymakerUiRenderFrameV1,ui_cell_size));
    std::cout<<"PASS: x64 API v16 preserves v15 context prefix\n";
    std::cout<<"PASS: AnyAPI 0.40.2 service layouts (lighting v4, effects v3, controls v4, antialiasing v3, timing v2, gpu_info v2)\n";
}
