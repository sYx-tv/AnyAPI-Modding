# Scene antialiasing integration

Target: Anymaker 0.1.23 / Steam build 25755694. Version 0.28.0 includes SMAA 1x and Enhanced SMAA. Live logs confirm
Enhanced SMAA Ultra at 2560 × 1440; residual aliasing remains.

## Placement

The exact-build hook replaces the shared call cell for
`renderer._record_commands_postprocess` from `renderer._record_render_commands`
(code end 2600, relocation 37). It calls the original composition routine once,
then records SMAA on the game's current D3D12 command list. Native
`frontend_ui.render` and `mm_ui.render` follow this boundary. No AA runs in the
Present hook. The observed native scene target is 2560 × 1440 in a loaded world.

The reviewed executable's graphics context is at RVA 0x5e3340; its current
command list is at offset 0x1d48. Exact executable/GCL hashes gate these offsets.
The current presentation buffer is acquired for the call and released before
return; it is not held across swapchain resizing.

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

The GPU implementation copies the composited scene colour before the HUD,
executes the three passes and returns the buffer to render-target state.
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
resizing and RGBA/BGRA formats are covered. The native scene-boundary diagnostic
has been observed in a loaded world; SMAA's game visuals and FPS still require
live acceptance. Logs distinguish `SCENE_AA_STAGE` from actual `SCENE_SMAA` frames.

## Remaining methods

TAA requires validated depth, motion vectors, camera jitter and history-reset
contracts. MSAA requires multisampled scene/depth targets, compatible native
pipelines and resolves. Neither is advertised as working or shown as a selector
option. SMAA 1x is spatial AA; it does not eliminate all temporal shimmer.
