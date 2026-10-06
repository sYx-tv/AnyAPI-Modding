# AnyMap

Press **M** to open Anymap. The full map supports smooth zoom/pan, named saved
markers, a marker list, player names and positions, waypoints and road guidance.
Follow the on-screen hints to place, edit, route to or remove a marker.

The square minimap can rotate with your view and supports movable placement,
size, range, opacity and visibility settings through AnyHelpers. Native inventory
and menu state suppress map drawing immediately.

Terrain remains on the GPU while panning, zooming and rotating. Labels are cached;
routes and markers use GPU drawing. See [Rendering implementation](map-rendering.md)
for the resource lifecycle, presentation smoothing and measurement limits.

Routes follow authored roads; they do not account for live traffic or obstacles.
The mod reads compatible installed game assets. Saved markers and options are
preserved by manager updates and removals. An external map executable is not
needed for normal play.
