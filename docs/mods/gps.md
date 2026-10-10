# AnyGPS

> AnyGPS hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.24 / Steam build 25826614 and turns itself off on any other build.
> Prototype: not in the catalog yet. **Single player only** for now.

AnyGPS adds a new part, the **GPS Sensor**. It looks like the stock Compass Sensor (same mesh, size and data
ports) and gives the data port and microcontroller everything a GPS needs.

## Getting it

- **Survival:** world loot from tech tier 4, the tier unlocked by the tier 3 bunker. It drops like the other
  sensors (mechanical loot, 0.2 spawn probability). Buildings whose tier 4 loot was already generated before
  you installed AnyGPS do not get it retroactively.
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

- The part definition is embedded in the DLL ([`anygps_logic.h`](../../native/anygps_logic.h)). At start-up it
  is written to `AnyAPI and Modding/AnyGPS/gps_sensor.json` and added to the game's vehicle components right
  after the game adds its own, on both the server and the client scene. Its id is `gps_sensor`, class
  `compass_sensor`, `tech_tier` 4, category `sensor`.
- The compass sensor's server tick and `get_data_f64` are hooked. Stock compass sensors are untouched; only parts
  with the `gps_sensor` definition get the extra channels.
- Saves that contain a GPS Sensor need AnyGPS installed to load it.

Source: [`anygps_mod.cpp`](../../native/anygps_mod.cpp), bindings in
[`anygps_bindings.h`](../../native/anygps_bindings.h), tests in
[`tests/anygps_logic_test.cpp`](../../native/tests/anygps_logic_test.cpp).
