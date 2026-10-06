# AnyGraphics

In-process DLL mod using generic AnyAPI GPU post-processing. API 0.26.0 or newer;
the initial effects were confirmed working in game. Requires AnyAPI 0.26.0 or newer.
Install AnyHelpers for the editable menu in Settings → Mod Settings.

The menu now has one AnyGraphics section and five presets. A preset shows four
main controls: Enable AnyGraphics, Look, Gameplay only and Before / after.
Custom / saved tuning preserves your existing settings and reveals edge smoothing,
sharpness, bloom, exposure and saturation. Advanced tuning reveals the remaining
controls. Select Apply Changes to commit. Cancel discards pending
changes; Reset stages the documented defaults. All groups share AnyHelpers'
native scrolling, save file and Apply/Cancel lifecycle. Visibility responds to
pending selections immediately; effects change only after Apply.

| Look | Appearance | Processing |
| --- | --- | --- |
| Natural | Light edge smoothing and modest sharpening | Two full-size passes |
| Soft edges | Stronger smoothing with gentler sharpening | Two full-size passes |
| Vibrant | Slightly richer color and contrast | Smoothing, sharpening, color |
| Cinematic | Soft glow, warm tone and restrained saturation | Quarter-resolution bloom, smoothing, sharpening, color |
| Lightweight | Modest sharpening alone | One full-size pass |

Presets do not overwrite saved custom values. Select Custom to restore them;
select Advanced tuning to adjust detailed values. Initial Look remains Custom
so upgrading preserves the appearance you already chose. The new preset/menu
layout awaits in-game acceptance. It requires the accompanying updated AnyHelpers;
older AnyHelpers versions still show all registered rows. No gameplay FPS benefit
is claimed for any preset; Lightweight simply enables fewer effect passes.

Defaults enable modest sharpening (0.15), with gameplay-only rendering. Bloom,
edge smoothing, vignette, grain and chromatic separation start off. Color and tone
start neutral. Additional effects remain opt-in. Without AnyHelpers the same
conservative defaults run; there is no separate external interface.

- Edges: spatial edge smoothing strength and threshold. Native AA stays enabled;
  this is not TAA, SMAA or DLSS and cannot eliminate temporal shimmer.
- Sharpness: strength, radius and halo limit.
- Bloom: intensity, threshold, soft knee, spread, quarter/half resolution, warmth
  and saturation. This uses the finished image, not an HDR scene buffer.
- Color/tone: exposure, gamma, contrast, brightness, saturation, vibrance,
  temperature, tint, shadows, highlights, black lift, white point and tone curve.
- Lens/film: vignette, grain and chromatic separation with individual controls.
- General: enable/bypass, gameplay-only and original/processed split comparison.

For the first visual test, enable Edge smoothing, keep sharpening modest, and
compare a static scene using Before / after. Enable bloom separately and start
with quarter resolution. Native bloom can be disabled in the game's Graphics
tab if you prefer a single bloom implementation. Heavy bloom and lens settings
can soften detail or affect the native HUD. Menus and inventories are bypassed by
default; disable Gameplay only if you intentionally want those affected too.

The mod does not spawn an EXE. Capture, effect textures and rendering stay on the
GPU; no frame pixels are copied back to the CPU. With all effects disabled the
pipeline skips the image copy and draws. Default sharpening requires one shader
pass and one GPU image copy. Bloom adds three small intermediate passes and one
full-resolution composite. Persistent resources avoid per-frame allocation.
Actual game FPS impact has not yet been measured.

Saved values live in `AnyAPI and Modding/mods/AnyHelpers/settings.tsv`. Logs include
DLL_READY, POST_PROCESS_REGISTERED, SETTINGS_REGISTERED, SETTINGS_APPLIED,
GPU_READY and failure/bypass reasons. Settings are committed through AnyHelpers.
See [Post-processing](../api/post-processing.md) for service declarations, limits and rendering order.
