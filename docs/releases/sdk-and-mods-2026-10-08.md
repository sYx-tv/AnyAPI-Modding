# SDK expansion and mod updates

The experimental SDK now opens discovered game internals to mod authors without
requiring a host update for each new binding. It includes 56,339 function
declarations, 854 anchored globals, full generated type layouts, native addresses,
hook routes, system documentation and eight compile-checked examples.

Download **AnyAPI-Experimental-SDK.zip** for the complete metadata reference and
pre-generated binding catalog. Start at `sdk/experimental/README.md`. Both game
hashes must match before the guarded resolver returns addresses. Experimental
access is not a promise that every operation is callable or gameplay-tested;
unresolved routes, ownership questions and network limits remain documented.

- **AnyMap 0.27.2:** corrects the full map's reflected orientation, player arrows,
  fixed-orientation minimap, zoom, dragging and waypoint selection. User confirmed
  the corrected map works in game.
- **AnyGraphics 0.29.3:** retries rejected scene/AA/lighting settings with a bounded
  delay instead of silently marking them applied. Local-light-only setups also
  display lighting readiness. Native scene rendering and clear-air beams retained.
- Existing API 0.32.0 and the other mods remain compatible. AnyBalance stays a local
  experiment pending its world test.

Validation: all 48 release checks passed; all eight SDK examples and the complete
57,193-entry generated binding header compile. Graphics rejection/recovery was
verified with the plugin fixture; this update has not had a new live-world visual
acceptance test. Manager update discovery continues to use the built-in catalog.

Packaging follow-up: corrected UTF-8 UI labels and documentation.
