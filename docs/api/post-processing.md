# GPU post-processing API v1

Introduced in AnyAPI 0.26.0. Current target: Windows x64, Anymaker 0.1.24 / Steam build 25826614.
Query `anyapi.post_process`, version 1, from `AnyAPI_ModReady`. The framework owns
the generic shader pipeline; the calling mod owns effect policy and settings.
Existing plugin ABI 1 layouts remain unchanged.

## Pass registration

Use `AnyPostPassV1` to supply an ID, HLSL source and explicit byte count, an
optional input token, optional auxiliary token, and downsample divisor 1, 2 or 4.
Source and metadata are copied. The pixel shader must export `main` with
`SV_Position`, `TEXCOORD` and `SV_Target`, compilable as ps_5_0. Shader compilation
happens once during registration. Includes and external file loading are disabled.
Tokens belong to the calling mod. Dependencies must refer to earlier passes
owned by that mod. No graphics device or resource pointer crosses the public ABI.

- t0: explicit input pass; token 0 means the latest full-resolution image.
- t1: original finished game image captured on the GPU.
- t2: auxiliary pass; token 0 means the original image.
- s0: linear clamp sampler.
- b0: float4 output dimensions/inverse dimensions, then float4 input dimensions/inverse dimensions.
- b1: 16 float4 values supplied by `AnyPostParametersV1` (64 floats).

Limits: 32 global passes, 12 per mod, IDs up to 63 bytes, shader source up to
64 KiB. Finite parameter values in [-10000,10000] only. Struct sizes and versions
must match exactly. `set` and `register_pass` require owned callback context.
`status` returns copied diagnostics, including **CPU submission time**, never GPU
time or game FPS. All functions serialize against frame/resize handling.

## Composition and lifecycle

Enabled passes run in registration order. Every full-resolution pass advances
the image; downsampled passes are intermediates. Disabled or faulted passes
resolve to their input. The final full-resolution shader writes directly to the
wrapped backbuffer; an extra full-screen copy shader is used only when needed.
Textures, shaders and backbuffer views persist across frames and are recreated
after resize/device reset. Intermediate textures are allocated only when needed.
Disabled pipelines skip the GPU image copy and effect draw calls.

The hook runs after the game's scene and native HUD, before AnyAPI UI drawing.
Thus native HUD pixels can be affected. AnyAPI map/item browser composition is
drawn afterward. This service supplies no depth, motion vectors, HDR scene input,
native AA disable/replace hook, temporal history, render scaling, TAA or DLSS.
The v1 implementation supports single-sample R8G8B8A8_UNORM/B8G8R8A8_UNORM
backbuffers up to 8192 per dimension. Unsupported formats and allocation/shader
failures bypass effects and report an error. Shader registration failure returns
0. C++ exceptions during submission are caught; acquired backbuffers are
still released and flushed.

No CPU pixel readback occurs in the runtime. GPU readback is used only by tests
to prove output correctness. GPU cost still grows with resolution and enabled
effects; no game FPS saving or fixed performance budget is claimed.

Microsoft reference: [D3D11On12 resource ownership and flushing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12).

## Validation

The `graphics_gpu` check (`graphics_gpu_test.cpp`) tests actual D3D12/D3D11On12
composition, shader compilation, ownership, invalid parameters, immediate bypass,
cached views and resize recovery.

AnyGraphics no longer registers post-processing passes: its finished-screen
filters were removed in 0.28.0 in favour of native renderer controls. The shader-output
check and 40-setting plugin check described in older versions of this page no
longer exist. The service remains
available to other mods; see `native/examples/post_process_example.cpp`.
Live gameplay and GPU performance acceptance are separate from these fixtures.
