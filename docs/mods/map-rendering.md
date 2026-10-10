# AnyMap rendering

Target: Anymaker 0.1.24, Steam build 25826614, Windows x64, reviewed executable and GCL hashes in [the build manifest](../../native/BUILD_MANIFEST.json).

The normal game path now uploads the composed terrain texture once and draws it as a GPU image. Full-map zoom/pan and minimap rotation change transforms and source rectangles rather than painting/resampling a bitmap on the CPU. Labels use cached DirectWrite layouts; routes and markers are GPU primitives. The previous CPU renderer remains a fallback for older hosts without the GPU service.

The minimap draws on every presented frame rather than a 33 ms raster timer. Player position and shortest-path heading use a 30 ms, frame-rate-independent smoothing time constant. Teleports, world changes, focus loss and stale sample gaps reset smoothing. Routing uses raw player data before presentation smoothing. The calibrated heading/projection math is retained.

Full-map CPU raster versus GPU command preparation measured roughly 33 ms versus 0.3 ms in the 2560×1440 actual-DLL fixture on this computer. This excludes GPU execution and is not an in-game FPS claim. The first texture upload has a one-time cost; textures are recreated after graphics device reset. CPU source and recovery copies consume memory in exchange for eliminating ongoing rasterization/uploads.

Native inventory/settings/menu visibility still gates drawing in the same presentation frame. All map controls, settings, markers, minimap placement and routing remain available. This is an in-process DLL rendered into the game's own backbuffer.

## Validation

The author confirmed the GPU map's operation and performance improvement in game.
This is acceptance feedback, not a controlled FPS benchmark. Compare disabled and
enabled minimaps in a fixed scene to measure cost on another machine.
