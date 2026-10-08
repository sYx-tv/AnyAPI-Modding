# AnyGraphics

AnyGraphics 0.28.0 is a native graphics mod for Anymaker 0.1.23. Its controls
live inside Settings → Graphics, in the scrolling **AnyGraphics · Modded**
section. It requires AnyAPI 0.28.0 and does not require AnyHelpers.

## Controls

The original game controls remain above the mod section. Choosing Game setting
uses those values; choosing an override tells the renderer to use AnyGraphics'
value during gameplay. Override choices are explicitly labelled. Disable
AnyGraphics to return to the game's own rendering settings.

Controls include native FXAA, SSAO, shadows, fog blur, bloom intensity and
threshold, sunlight, sky light, ambient light, base fog and scene-lighting stops.
They alter native renderer switches or frame lighting inputs before the game
builds scene constants and records rendering commands.

Fog, bloom and optional lighting controls use **Off / Low / Medium / High /
Ultra** strength levels instead of numeric sliders. Bloom also offers Game
setting. These levels change native intensity inputs; they are not separate
shadow-map or SSAO sampling-quality modes. Shadows, ambient occlusion and fog
blur retain the game's actual on/off capability.

Presets are **Off / Performance / Low / Medium / High / Ultra**. Choosing a
profile stages a group of settings; individual choices may then be changed.
Off bypasses overrides and restores the game's own values. Medium is the
initial profile. Advanced options reveals sunlight, sky and ambient strength,
plus native Cloud rendering, Grass rendering and Foliage rendering switches.
Each detail switch offers Game setting / Off / On and changes rendering only;
world objects and collision remain intact. Numeric controls are hidden.

| Preset | Native settings |
| --- | --- |
| Off | Game settings; no native overrides |
| Performance | FXAA on; SSAO, shadows, fog blur, bloom, base fog, clouds and grass off |
| Low | Shadows on, low bloom/base fog; SSAO, fog blur and grass off |
| Medium | Native effects on; medium bloom/base fog |
| High | Native effects on; high bloom/base fog |
| Ultra | Native effects on; strongest supplied bloom/base fog |

High and Ultra do not claim greater texture resolution or sample counts.

Performance is a combination of settings, not a measured FPS guarantee.
Native bloom is part of the game's own render pipeline. AnyGraphics registers
no finished-screen filter passes. SMAA copies scene colour before HUD drawing
for its three AA passes.
Sharpening, supplemental smoothing, colour filters, tone curves, grain,
vignette, chromatic separation and split comparison have been removed.
Version 0.28.0 offers native FXAA and SMAA 1x. TAA, MSAA and DLSS are
not implemented.

## Apply and persistence

Use the Graphics tab's existing Apply Changes, Reset and Back buttons. Draft
changes stay pending until Apply; Cancel discards them. Reset stages defaults.
The mod participates in the native tab's dirty/default comparisons so the same
buttons work for both the original controls and AnyGraphics.

On first launch the mod imports retained native values from the old AnyHelpers
settings file. Removed effect values are ignored. Subsequent changes save to
`AnyAPI and Modding/mods/AnyGraphics/settings.tsv`. Other mods' settings are
untouched. The old shared file remains available for rollback.

Native hooks are confirmed active in the prior corrected build, with renderer
and scene calls recorded and no rejected frames. This native-only menu revision
has live menu acceptance from the user. The simplified quality levels still need a live check. A connection warning appears in the
section if native hooks are unavailable. Readiness and rejection counts are
also recorded in the game-root `anymaker_modding.log`.

See [Native scene controls](../api/scene-controls.md) and
[Menu extensions](../api/menu-extensions.md) for integration contracts.

## Scene antialiasing development

Version 0.28.0 offers Game setting, Off, FXAA **SMAA 1x (scene)**, and **Enhanced SMAA (scene)**.
Enhanced SMAA adds stronger contrast-aware blending at diagonal edges; it may
soften fine scene detail. Both methods keep the HUD untouched.
Choosing SMAA reveals **SMAA quality: Low / Medium / High / Ultra**. Use Apply
Changes. SMAA disables native FXAA once ready and processes scene colour before
the HUD, preserving UI sharpness. The game-bound visual/FPS check is pending;
automated GPU smoothing and HUD restoration tests pass. Unsupported bindings
bypass rather than falling back to a finished-screen filter.

See [Scene AA development](../development/scene-antialiasing.md).

## Volumetric lighting

Version 0.29.0 adds native-depth fog, shadowed sun shafts and local-light beams
in the HDR scene before bloom, tone mapping, antialiasing and the HUD. Headlights,
torches and other native point/spot lights participate. Fog, beam strengths,
beam focus, sampling quality and local-light budgets are editable in Graphics.
Performance disables the added lighting; Low through Ultra progressively add
beams, fog and higher sampling budgets. Off restores game settings.

See [Scene lighting](../scene-lighting.md) for preset values, the API and limits.
