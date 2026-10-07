# AnyAPI and AnyGraphics 0.28.0

For Anymaker 0.1.23, Steam build 25755694, Windows x64.

AnyGraphics now lives in the game's Graphics tab and changes native renderer
settings. Its previous colour filters, sharpening and finished-screen effects
are removed. Apply, Cancel and Reset use the game's existing buttons.

## Changes

- Six presets: Off, Performance, Low, Medium, High and Ultra.
- Fog, bloom and lighting use named strength levels.
- Native switches for ambient occlusion, shadows and fog blur.
- Advanced controls for native clouds, grass and foliage rendering.
- SMAA 1x and Enhanced SMAA with four quality levels, applied before HUD drawing.
- Automatic FXAA replacement while scene AA is ready, with native bindings
  restored afterwards. Disabling the mod restores the game's settings.
- Versioned scene controls and scene AA services, with public SDK headers.

Install API 0.28.0 before AnyGraphics 0.28.0. The other four mods remain at
0.27.0 and are compatible. Manager 1.3.1 can discover these updates; its bundled
offline API remains 0.27.0. No new manager EXE is needed.

## Validation and limits

All 35 native checks passed, including GPU readback, scene/HUD separation,
resizing, RGBA/BGRA targets, settings lifecycle and exceptional restoration.
Installed DLLs match the published archives.

Live logs confirm Enhanced SMAA Ultra at 2560 × 1440 before HUD drawing, with
FXAA replaced and no native scene rejections. The author requested publication
after testing. Residual jagged edges remain; screenshots taken at different
camera angles do not quantify improvement. Spatial AA does not eliminate all
temporal shimmer, and no controlled FPS benchmark has been completed. Separate
visual acceptance of the new detail switches is still pending.

TAA, MSAA, DLSS, volumetric lighting and god rays are not included in this release.
