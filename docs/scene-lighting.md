# Scene lighting development

The public release is AnyAPI and AnyGraphics 0.28.0. The lighting branch adds
an experimental 0.29.0 service and local candidate; it is not a public release.

## Render integration

The new pass records immediately after `renderer._record_commands_additive`,
before native bloom, tone mapping, scene antialiasing and UI. It samples HDR
scene colour, native scene depth, and the nearest native shadow cascade.
The camera, sun and shadow transform are copied from the current scene frame.
It never changes world weather, time, materials or the HUD.

Two GPU passes integrate scattering at half resolution and composite it using
depth-aware upsampling. Settings provide fog and shaft strengths, four sampling
levels, and height falloff. Lighting starts Off. Existing presets leave these
experimental effects Off. Settings use native Apply, Cancel and Reset.

## Service

`anyapi.scene_lighting`, version 1, uses
[`anyapi_scene_lighting_v1.h`](../sdk/include/anyapi_scene_lighting_v1.h).
Only an active mod can submit policy. A second active owner cannot replace an
enabled policy. Missing/inactive owners disable the pass. Inputs are finite,
bounded values. Query status to distinguish a registered service from a
successfully rendered frame. A failed native-frame or GPU-resource contract
bypasses the effect and records a reason.

The service requires the exact supported game build. It exposes copied settings
and status, not raw native resource pointers.

## Validation

All 38 native checks pass, including shader compilation/reflection, 28 synthetic
D3D12 lighting cases, policy ownership, camera/depth contracts and menu staging.
GPU cases cover all quality levels, standard/reversed-depth shadow occlusion, depth-limited fog, zero
density identity, resize and a sharp HUD drawn afterward. Production restores
borrowed resource states and the tracked caller bindings.

Native-world visual acceptance and FPS measurements are pending. Synthetic GPU
success does not establish correct game camera/shadow conventions.

## Current limits and next work

Only the nearest shadow cascade supplies occlusion. Samples outside it remain
unshadowed. There is no local point/spotlight scattering, temporal accumulation,
multi-cascade blending, volumetric cloud replacement or water reflection
overhaul. Special vision, underwater and underground views bypass this pass.

After native-world acceptance, prioritize all-cascade shadow sampling and
stable accumulation, then local-light volumes and material/water/cloud
integration. Each addition needs its own native contract and visual/performance
validation. This prototype is not equivalent to a complete shader pack.

## Native-world correction

The first candidate skipped all lighting because camera-array element size was
read as count. Native arrays place count at +8 and stride at +12. The corrected
capture checks both independently, including 720-byte cameras and 16-byte target
references. A read-only world probe confirmed these layouts and reversed-Z
projection/shadow depth. The shader now selects the correct shadow comparison
from the shadow-camera projection. World visual acceptance remains pending.

The next live session rejected the shadow format at resource capture. The
nearest shadow target has an R16_UNORM SRV, not R32_FLOAT. The backend now
accepts R16_TYPELESS/R16_UNORM shadow resources and creates the matching SRV;
scene depth remains R32. Eight additional GPU cases exercise the native
16-bit shadow format with reversed depth across all quality levels.
