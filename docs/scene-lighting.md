# Scene lighting development

The public release is AnyAPI and AnyGraphics 0.28.0. The lighting branch adds
an experimental 0.29.0 service and local candidate; it is not a public release.

## Render integration

The new pass records immediately after `renderer._record_commands_additive`,
before native bloom, tone mapping, scene antialiasing and UI. It samples HDR
scene colour, native scene depth, and up to eight native sun-shadow cascades.
The camera, sun and shadow transform are copied from the current scene frame.
It never changes world weather, time, materials or the HUD.

A tiny GPU matrix texture carries copied cascade transforms through inline draw
constants, avoiding mutable per-frame upload buffers. Two full-screen GPU passes integrate scattering at half resolution and composite it using
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

All 38 native checks pass, including shader compilation/reflection, 44 synthetic
D3D12 lighting cases, policy ownership, camera/depth contracts and menu staging.
GPU cases cover all quality levels, standard/reversed-depth shadow occlusion, depth-limited fog, zero
density identity, resize and a sharp HUD drawn afterward. Production restores
borrowed resource states and the tracked caller bindings.

Native-world visual acceptance and FPS measurements are pending. Synthetic GPU
success does not establish correct game camera/shadow conventions.

## Current limits and next work

The finest sun cascade containing a sample supplies occlusion. Cascade count
comes from the native sun-radius array, so spotlight targets are excluded.
Samples beyond all sun cascades remain unshadowed. There is no local point/spotlight
scattering, temporal accumulation, cascade cross-fading, volumetric cloud replacement or water reflection
overhaul. Special vision, underwater and underground views bypass this pass.

After native-world acceptance, prioritize stable accumulation, then local-light volumes and material/water/cloud
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

## Visible fog acceptance

The user confirmed clearly visible fog after setting height falloff Off and
volumetric fog Ultra. The pass now renders into the native HDR scene. The former
Ground fog High choice suppressed density at the tested altitude of about 34 m.
It is renamed Fog height falloff, with a sea-level explanation and an Off
default. Shadowed-shaft appearance and native GPU timing remain unaccepted.

## Sun-shaft correction

The local candidate now samples all available sun cascades (bounded at eight),
with quadratic near-camera sampling and a smaller depth bias that preserves
thin branch occlusion. Eight additional GPU cases use distinct near/far shadow
resources and place the view outside the nearest cascade; a blocked farther
cascade must suppress light. This establishes GPU routing and occlusion, not
native-world visual acceptance or an FPS measurement. Fog visibility was user
confirmed; this revised shaft build still needs a world test.

## Live shadow diagnostics

The current development build records a one-shot 32×24 offscreen probe on the
lighting pass's first valid frame. It reports shadow coverage and lit fractions
through `SHAFT_PROBE`; regular frame logs also report native sun RGB and the
view/sun cosine. Probe readback is fenced after the original command-list
submission and polled without waiting. It is never displayed or composited.
GPU tests verify lit/blocked results, no premature readback, repeated requests
with increasing fence values, and preservation of native HUD rendering.
These diagnostics do not by themselves fix or validate the live shaft appearance.

## Native light-scale calibration

A live world probe reported sun RGB (0.305571, 0.280569, 0.245261), shadow
coverage 0.999994 and lit fractions ranging from 0 to 0.90625. This confirms
that the shader samples covered, occluded native shadow regions. The original
phase included 1/(4*pi) while using the game's artistic direct-light values;
that reduced shaft illumination by 12.57 times relative to the ambient fog.
The shader now uses an isotropic-relative phase, with g=0 giving a factor of 1.
This changes scattering radiance, not global exposure, native surface lighting,
shadow geometry, sample count or pass placement. Eight GPU regressions use the
measured sun colour and viewing angle at Low fog density; visible native-world
beam contrast and performance remain to be tested.
