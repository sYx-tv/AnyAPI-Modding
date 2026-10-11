# GpsPart

> GpsPart hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.24 / Steam build 25826614 and turns itself off on any other build.
> GpsPart 1.0.0 requires AnyAPI 0.35.0 or newer on that build; install it from the manager. **Single player only** for now.

GpsPart adds a new part, the **GPS Sensor**. It looks like the stock Compass Sensor (same mesh, size and data
ports) and gives the data port and microcontroller everything a GPS needs.

A yellow arrow on top of the part and a yellow triangle on its front face show its forward axis, the axis
`heading` measures.

## Getting it

- **Survival:** world loot as soon as the 3rd bunker is triggered to explode. The game spawns loot by tech tier:
  a new world starts at loot level 1, and each bunker's end sequence raises the level by one and generates
  that level's loot in loaded buildings (and in every building loaded later). The GPS Sensor is tech tier 4,
  so it starts appearing with the 3rd bunker, like the other sensors (mechanical loot, 0.2 spawn probability).
  If you were already past the 3rd bunker before installing GpsPart, buildings you had loaded since then keep
  their old loot.
- **Sandbox and Creative:** in the sensor container with the other sensors.

## Data channels

Wire them like any other sensor channel, from a data port, a data link or a microcontroller.
Positions are the GPS Sensor block's own world coordinates. **y is up**.

| Channel | Direction | Value |
| --- | --- | --- |
| `x`, `y`, `z` | Output | The block's world position in metres |
| `altitude` | Output | World height in metres (same as `y`) |
| `heading` | Output | The stock compass north angle of the block's forward axis, 0-360° |
| `pitch` | Output | Nose up/down of the forward axis, -90 to 90° |
| `roll` | Output | Bank of the block's right axis, -90 to 90° |
| `speed_ms`, `speed_kmh` | Output | 3D speed in m/s and km/h |
| `ground_speed_ms`, `ground_speed_kmh` | Output | Horizontal speed |
| `vertical_speed_ms`, `vertical_speed_kmh` | Output | Climb (+) or descent (-) |
| `acceleration_ms2`, `acceleration_kmh_s` | Output | Change in speed per second, in m/s² and km/h per second |
| `waypoint_distance` | Output | Straight-line distance to the waypoint in metres |
| `waypoint_horizontal_distance` | Output | Distance ignoring height |
| `waypoint_bearing` | Output | Compass bearing to the waypoint, 0-360°, same convention as `heading` |
| `waypoint_relative_bearing` | Output | Turn needed to face the waypoint, -180 to 180° |
| `waypoint_height_difference` | Output | Waypoint height minus the block's height |
| `waypoint_x`, `waypoint_y`, `waypoint_z` | Input | The waypoint, in the same world coordinates as `x`, `y`, `z` |
| `north_angle` | Output | Unchanged from the Compass Sensor |

Speed and acceleration come from the block's movement, smoothed over about a quarter of a second.

## How it works

- The part definition is embedded in the DLL ([`gpspart_logic.h`](../../native/gpspart_logic.h)). At start-up it
  is written to `AnyAPI and Modding/GpsPart/gps_sensor.json` and added to the game's vehicle components right
  after the game adds its own, on both the server and the client scene. Its id is `gps_sensor`, class
  `compass_sensor`, `tech_tier` 4, category `sensor`.
- The part's mesh is the stock compass mesh plus the yellow arrows, built at start-up from the game's own
  `rom/meshes/components/compass_sensor_a.mesh` and written beside it as `gpspart_gps_sensor_a.mesh`. If the
  stock mesh is not in the expected format, the GPS Sensor uses the plain compass look.
- The compass sensor's server tick and `get_data_f64` are hooked. Stock compass sensors are untouched; only parts
  with the `gps_sensor` definition get the extra channels.
- Saves that contain a GPS Sensor need GpsPart installed to load it.

Source: [`gpspart_mod.cpp`](../../native/gpspart_mod.cpp), bindings in
[`gpspart_bindings.h`](../../native/gpspart_bindings.h), tests in
[`tests/gpspart_logic_test.cpp`](../../native/tests/gpspart_logic_test.cpp).
