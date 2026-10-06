# GPU drawing service v1

Query `anyapi.gpu_draw`, version 1, during `AnyAPI_ModReady`, then register one renderer for the calling DLL. The service is generic: AnyMap owns its layout, terrain, routes and visibility policy. Existing canvas mods and ABI 1 remain supported.

The renderer receives the current presentation frame. Call `texture`, `emit` and `measure` only inside that callback. Commands use viewport pixel coordinates, a six-element row-vector affine transform, straight ARGB colors, and opacity from zero to one. Text and polygon points are copied synchronously. Texture input is BGRA with premultiplied alpha and positive row pitch; data is copied when dimensions or revision change. Reuse the same ID and revision for unchanged images.

Supported commands: rectangles, rounded rectangles, ellipses, lines, polygons, text, image quads, and nested rectangular clips. A line uses rect as its two endpoints; other shapes use x/y/width/height. Image source uses source x/y/width/height in texture pixels. Flags select outline, bold/centered/no-wrap text, and dashed lines. Clip pushes capture the command transform; pair them with pops. Remaining clips are closed at batch end.

Limits: one callback per DLL, 8,192 commands per frame, 8,192 characters or polygon points per command, 16 textures per DLL, 8,192 pixels per texture dimension and 128 MiB per texture. IDs belong to their DLL. No graphics device pointers cross the public ABI. Text layout caching is bounded to 1,024 layouts. Texture pixels remain available for device reset; resident GPU textures are recreated only as needed.

The host uses its existing in-process Direct2D presentation integration, plus DirectWrite text. It submits a GPU batch even when a DLL supplies no CPU canvas. Resize/device reset drops GPU objects, retaining texture data. Renderer exceptions fault that mod and discard its batch.

`GPU_DRAW` logs aggregate command collection/submission CPU time every ten seconds of rendering, command count and lifetime texture uploads. These are CPU timings; they do not measure GPU fence completion or game FPS. Verify performance in-game at the same position with minimap disabled, enabled, and full map open.

Validation: `platform_render` checks actual WARP pixels, mixed/command-only presentation, texture reuse, reset recovery and the existing presentation clock. `map_gpu` loads the real AnyMap DLL, renders full map/minimap images, exercises every-frame batches and hiding, and compares CPU raster time with cached command collection.
