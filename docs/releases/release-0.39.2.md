# AnyAPI 0.39.2

For Anymaker 0.1.25 (Steam build 25904830).

- **Lamp beams are back on AMD graphics cards.** AMD gets its own lamp path: the lights are passed in a constant
  buffer and read by an unrolled loop. A player on an AMD card confirmed it before release.
- NVIDIA and other cards run exactly the same lamp beam shaders as in 0.39.1. Only the AMD shader is new.
- `anyapi.gpu_info` now reports lamp beams as supported everywhere, so AnyGraphics shows the lamp beam settings on AMD
  again ([GPU info](../api/gpu-info.md)).

No mod updates are needed.
