# Scene antialiasing integration

Target: Anymaker 0.1.28 / Steam build 26277967. Version 0.28.0 includes SMAA 1x and Enhanced SMAA. Live logs confirm
Enhanced SMAA Ultra at 2560 × 1440; residual aliasing remains.

## Placement

The exact-build hook replaces the shared call cell for
`renderer._record_commands_postprocess` from `renderer._record_render_commands`
(code end 2600, relocation 37). It calls the original composition routine once,
then records SMAA on the game's current D3D12 command list. Native
`frontend_ui.render` and `mm_ui.render` follow this boundary. No AA runs in the
Present hook. The observed native scene target is 2560 × 1440 in a loaded world.

The reviewed executable's graphics context is at RVA 0x5e23d0; its current
command list is at offset 0x2650 (0.1.25: 0x5e5340 and 0x2648; 0.1.23: 0x5e3340 and 0x1d48). The context getter
ends in `lea rax, <context>` (0.1.27: RVA 0x70050); the field is the one the frame code loads before calling the
list's ResourceBarrier (vtable +0xd0). Exact executable/GCL hashes gate these offsets.
The current presentation buffer is acquired for the call and released before
return; it is not held across swapchain resizing.

Since AnyAPI 0.40.2 the replacement records one level deeper: an inner call-cell
hook on `pipeline_postprocess.renderer.cmd_render` inside
`_record_commands_postprocess` (code end 5944, call cell 15; `self` is
`scene_render.m_renderer_postprocess` at scene+0x2450). It returns before the
editor, placement and inspection overlays are drawn into `m_target_overlay` and
composited into the swapchain, so those overlays (drawn with the jittered camera)
are no longer smoothed or smeared. The scene-controls worker installs it through
`scene_frame::seams.install_postprocess` and the log reads `SCENE_AA_STAGE inner=1`.
The renderer and scene come from the scene hook of the same frame (one
`renderer.render` builds and records one stack `scene_render` on one thread) and
are checked against `self - 0x2450`. The hook forwards all four integer argument
registers (rcx, rdx, r8, r9) and the return value to the original. Version 3
`flags` `ANY_AA_AFTER_OVERLAYS` (AnyGraphics hidden row `aa_before_overlays` 0)
leaves every frame to the boundary record below while the hook stays installed.
Without the inner hook (`inner=0`, with the
prologue found in cell 15 printed as `cell15=`) AA records at the 0.1.27 boundary
above and TAA pauses while an overlay vector (seven `vector<element>` at
scene+0x25b8, step 0x28, count at +0xc) is non-empty. Jitter stays off for 30
frames after any overlay was drawn (`SCENE_AA_OVERLAY` lists the counts).

## Implementation

Official iryoku/smaa code is pinned by commit and SHA-256 in
`native/third_party/smaa/provenance.json`, with the MIT license retained.
`scene_smaa.hlsl` implements colour edge detection, blend-weight calculation
with the Area/Search tables, and neighbourhood blending. Kernels are built
for Low, Medium, High and Ultra. Compilation generates the embedded bytecode;
production does not compile shaders while playing. Linear/point samplers have
explicit s0/s1 bindings; shader reflection checks every compiled entry. The
original sampler declarations allowed the compiler to put PointSampler at s0
for edge detection, mismatching the root signature. That integration bug is fixed.

The GPU implementation copies the composited scene colour before the overlays,
executes the three passes and returns the buffer to render-target state.
Root signature: 44 pixel-stage constants (cbuffer `Frame`, 11 float4), table
t0–t2 and table t3–t4, static linear s0 and point s1, no input layout, vertex,
hull, domain and geometry root access denied. Shader-visible heap (22): 0/3/6
the SMAA stages (edges are R8G8), 9 and 12 the temporal tables (resolved, other
history, depth), 15/16/17 the output tables, 18–19 padding, 20–21 gbuffer_ocean
and gbuffer_0 (null until bound). The 3-wide tables are packed, so the TAA
surfaces have their own table. The resolved image and the two RGBA16F histories
are created on the first frame that needs them. With TAA and no sharpening the
temporal pass writes history and back buffer as two render targets; otherwise a
final pass copies or sharpens. That final write adds a static triangular dither
(about 1 LSB, none at black or white). A failed initialisation retries after 1,
2, 4 … 60 s. Undithering returns after four green gathers on 4×4 periods without
variation and computes its moments from gathers. Bindings are tracked per
command list in a four-entry cache; each tracked call stores only what changed.
Native root signature, descriptor heaps, graphics root arguments, pipeline,
topology, viewports, scissors and render targets are tracked and restored.
Vertex/index bindings, blend factors and stencil reference are not changed.
Lookup uploads remain alive; resizing waits for prior submitted GPU work before
replacing owned resources. No CPU colour readback occurs in the game.

Native FXAA is disabled while a successfully initialized replacement is active.
If the boundary, device, format or captured bindings are unavailable, SMAA
bypasses and the native AA setting is preserved on subsequent frames. The
service accepts SMAA 1x and Enhanced SMAA; unsupported identifiers are rejected.

## Validation

Automated GPU readback verifies all four quality levels smooth a stepped
diagonal, preserve flat colours, and let a distinct HUD pipeline draw crisp,
correctly clipped pixels after binding restoration. Repeated frames, resource
resizing and RGBA/BGRA formats are covered. The GPU test runs WARP with GPU-based
validation and fails on any D3D12 error. TAA cases cover lazy targets, a failed
target creation (SMAA only, retried after the backoff), a static
scene, re-centring by the jitter, history kept at the same depth and rejected at
another, ocean and rim inputs, resize with history, the MRT path against the
separate output pass (at most 1 LSB), output dither, and the gather undither
against the 0.40.1 kernel (at most 0.5 LSB on natural patterns). The kernel test
checks texture, sampler and constant-buffer bindings by reflection and runs CPU
contracts for the view mask, jitter and un-jitter, overlay counts, binding masks
and the list cache. The native scene-boundary diagnostic
has been observed in a loaded world; SMAA's game visuals and FPS still require
live acceptance. Logs distinguish `SCENE_AA_STAGE` from actual `SCENE_SMAA` frames.

## Remaining methods

TAA (0.36.0) reprojects through depth and the camera only: the game exposes no
motion vectors, so moving objects rely on colour clipping. MSAA requires
multisampled scene/depth targets, compatible native pipelines and resolves; it
is not implemented. Not yet done: building only the selected SMAA preset's
pipelines, rebuilding only size-dependent targets on resize, a capped SMAA
preset under TAA, and releasing the GPU objects after AA has been off for a
while.
