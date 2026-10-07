# AnyAPI and AnyGraphics 0.29.0

For Anymaker 0.1.23, Steam build 25755694, Windows x64.

This update adds HDR volumetric fog, shadowed sun shafts and local-light beams
before native bloom, tone mapping, scene antialiasing and the HUD. Sunlight,
headlights, torches and native point/spot lights share one lighting pass.

- Independent fog, sun and local-beam strength controls.
- Sun/local intensity multipliers and shared beam focus.
- Four sampling levels and budgets of 2–8 nearby local lights.
- Off / Performance / Low / Medium / High / Ultra presets now include lighting.
- Native Apply, Cancel, Reset and saved settings remain supported.
- Public `anyapi.scene_lighting` v1/v2 SDK headers and documentation.

Update AnyAPI to 0.29.0 before updating AnyGraphics. AnyHelpers, AnyInventory,
AnyStorage and AnyMap stay at 0.27.0. Manager 1.3.1 discovers these updates through
its existing catalog; no new manager EXE is required. Its offline bundled API
remains 0.27.0. Packages contain DLLs only, preserving user settings.

All 39 native checks passed, including 54 D3D12 lighting cases. The author
confirmed the fog, sun shafts and local-light visuals and approved publication.
Native FPS varies with sampling and light budgets; controlled benchmarks remain
pending. Spotlights use matching native shadow maps when available. Point lights
without shadow maps remain unshadowed. No TAA, MSAA, DLSS, temporal accumulation
or new point-light shadow generation is included.
