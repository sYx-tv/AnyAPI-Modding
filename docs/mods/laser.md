# AnyLaser

> AnyLaser hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> **Single player and host only** for now. In development: not in the catalog yet.

AnyLaser adds two parts, the **Laser Block** and the **Laser Sensor**. The Laser Block shoots a beam straight out of
its lens. The Laser Sensor tells you when a beam is on it. Both plug into data ports, data links and
microcontrollers, so you can build tripwires, door triggers, range finders and alarms.

Both parts are one cell (0.08 m) and have data ports on every face except the lens. They are small black boxes
on a grey base plate: the Laser Block has a red lens and a red status light on top, the Laser Sensor a red ring
around a dark target. The beam is a thin red line with a soft glow and a red spot where it lands.

## Getting them

- **Survival:** world loot from the 2nd bunker on (tech tier 3, mechanical loot, 0.2 spawn probability), like the
  stock sensors and the [GPS Sensor](gps.md).
- **Sandbox and Creative:** in the sensor container with the other sensors.

## Laser Block

The beam leaves the lens and runs up to 100 m until it hits something: terrain, buildings, vehicles, props or a
player. It is checked every server tick with the game's own physics raycast, the same one the wheels use.

| Channel | Direction | Value |
| --- | --- | --- |
| `hit` | Output | 1 when the beam lands on something, else 0 |
| `distance` | Output | Metres from the lens to what the beam hits (0 when nothing is hit) |
| `off` | Input | 1 switches the laser off. Leave it unconnected and the laser is always on |
| `north_angle` | Output | Unchanged from the Compass Sensor class both parts are built on |

## Laser Sensor

| Channel | Direction | Value |
| --- | --- | --- |
| `lit` | Output | 1 while a Laser Block's beam is on the sensor's lens, else 0 |
| `broken` | Output | 1 when no beam reaches the sensor (blocked or switched off), else 0. The opposite of `lit` |
| `distance` | Output | Metres from the laser block's centre to the sensor's lens while lit (0 when not lit) |
| `north_angle` | Output | Unchanged from the Compass Sensor |

The beam counts as on the sensor when it ends on the sensor's block and comes in from the lens side, at any angle
up to 90° off straight on. Aim the beam at the lens. A beam that hits the back or a side of the sensor does not
light it. Several lasers can light one sensor; `distance` then reads the nearest.

`lit` drops to 0 within about 0.15 s of the beam being blocked.

## Ideas

- **Tripwire:** a Laser Block across a doorway at shin height, aimed at a Laser Sensor. Wire `broken` to a horn
  or lights.
- **Range finder:** a Laser Block on its own. `distance` is the gap to whatever is in front of it.
- **Door trigger:** `lit` from a sensor into the door's logic, with the laser switched by `off` from a button.

## Limits

- The beam is drawn as a glowing line over the 3D view, so it shows through anything between you and the beam.
  The beam itself stops at the first thing it hits.
- Players block the beam (an upright capsule around each player). Zombies and creatures block it only where the
  game's physics raycast sees them (**unproven**).
- Co-op: the beam and the data run on the host. Every player needs AnyLaser installed, because the parts are new
  part definitions. Joining players don't see the beam yet (**unproven**).

## How it works (for modders)

Source: [`native/anylaser_mod.cpp`](../../native/anylaser_mod.cpp), logic in
[`native/anylaser_logic.h`](../../native/anylaser_logic.h) (tested by `native/tests/anylaser_logic_test.cpp`),
meshes in [`native/anylaser_mesh.h`](../../native/anylaser_mesh.h), bindings in
[`native/anylaser_bindings.h`](../../native/anylaser_bindings.h).

- The parts are new definitions of the stock `compass_sensor` class (the GpsPart route). They are written to
  `AnyAPI and Modding/AnyLaser/anylaser_parts.json` at start-up and added right after the game adds its own parts.
  Ids `anylaser_laser` and `anylaser_sensor` are saved in worlds, so they never change.
- The meshes are built at start-up from simple shapes, with the file header copied from the game's
  `compass_sensor_a.mesh`, and written beside it as `anylaser_laser_a.mesh` and `anylaser_sensor_a.mesh`.
- `server_scene.vehicle_component.compass_sensor.tick` and `.get_data_f64` are hooked. They only act on our two ids,
  so the Compass Sensor and the GPS Sensor keep working (the hooks chain).
- Each Laser Block casts `physics_util.raycast_distance(server_scene.m_physics, ray, 100)` from 4 mm past its lens.
  The ray starts outside the block's own cell, so it never hits itself.
- The beam is drawn with `anyapi.gpu_draw`, projected with the camera from `client_scene.get_camera` (the AnyCombat
  route).
