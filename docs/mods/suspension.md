# AnySuspension

> AnySuspension hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> **Prototype, single player only** for now: built from source, not in the catalog yet.

AnySuspension adds parts for building your own running gear, like Trailmakers' bearing and straight suspension:
two pairs of joint parts, a wheel hub with no suspension of its own, and a torque link that stays joined.

| Part | Pairs with | What it does |
| --- | --- | --- |
| **Bearing** | Bearing Ring | Like the Hinge Pin and Hinge Knuckle with no stops: the ring's body spins freely all the way round. |
| **Suspension Rail** | Suspension Slider | Like the Rail and Rail Slider, but the slider is sprung: it springs back to where you placed it. |
| **Rigid Wheel Hub** | | The stock Wheel hub with almost no suspension travel (2 cm): same wheel items, drive, steering and brake. |
| **Drive Coupler** | Drive Coupler or Torque Interface | The Torque Interface that never disconnects: drive keeps flowing as the bodies move. |

They are built and connected exactly like the stock pairs. Place the ring on the bearing (or the slider on the rail)
in the editor to start the new body, and interact with the bearing or the slider to connect or release it. All are
`tech_tier 2` loot in the mechanical group, like the stock rail.

## Suspension

1. Place a **Suspension Rail** on the chassis, pointing along the travel you want (usually up and down). Stretch it:
   its length is the suspension's travel.
2. Place a **Suspension Slider** on it where you want the wheel side to sit. That spot is the slider's rest
   position. It and the slider's Properties Tool values are saved with the world.
3. Build the wheel side on the slider's body.

The slider's joint has a spring and damper that pull it back to its rest position, and it stops at the rail's ends.
The load makes it settle a little past the rest position: about `9.81 / (2 pi f)^2`, 4 cm at the default 2.5 Hz.
Every vehicle sags by about the same amount, whatever its weight. The spring is set from the heavier of the two
bodies, shared between all the suspension sliders holding it up.

**Properties Tool**: click a Suspension Slider, like a stock wheel, to set its own rows (0 to 1 in steps of 0.1):

| Row | Default | Effect |
| --- | --- | --- |
| Stiffness | 0.5 | Ride frequency from 1 Hz (0, soft, 25 cm sag) to 4 Hz (1, race car, 1.5 cm sag); 0.5 is 2.5 Hz. |
| Damping | 0.5 | How quickly bouncing dies away: 0.1 of critical at 0 (bouncy) to critical at 1 (no overshoot). |

## Driven, steered suspension

1. Put a Suspension Rail and Slider on the chassis, as above.
2. Build a **Rigid Wheel Hub** on the slider's body and fit a wheel to it. Steering and brake links work as on the
   stock hub. Its own Stiffness and Damping rows (Properties Tool, like any wheel) barely matter with 2 cm of travel:
   the Suspension Slider is the suspension.
3. To drive it, run the engine's shaft to a **Drive Coupler** on the chassis, face to face with a Drive Coupler on
   the slider's body, and shaft from there to the hub's torque face. The pair joins like stock Torque Interfaces
   (faces touching) and then stays joined however the suspension moves.

## How it works

- The game's parts are JSON definitions. The bearing pair is the hinge pin pair with its swing limits opened to
  -180 to 180 degrees, which the physics engine (Jolt) treats as no limits. The suspension pair is the rail pair.
  The definitions are written to `AnyAPI and Modding/AnySuspension/anysuspension_parts.json` and added at start-up.
- The game's slider joint has a motor (the Rail Ballscrew Slider uses it to hold position). On every server tick of
  a Suspension Slider, the mod sets that motor to a position spring: the target is the rest position, the stiffness
  `k = m (2 pi f)^2` and the damping `2 zeta sqrt(k m)`, where `m` is the sprung mass share, `f` comes from
  Stiffness and `zeta` from Damping.
- The rest position is kept along the rail, so it does not drift when the game rebuilds the joint on load. It is
  stored with the slider's Stiffness and Damping in `%APPDATA%\Anymaker\working\anysuspension_sliders.json`, which the game copies into each save.

- A wheel's travel, spring and steering range come from tuning in the game code, chosen by the hub's id; an id the
  game does not know gets none. The mod gives the Rigid Wheel Hub a copy of the stock Wheel's tuning with the travel
  cut to 2 cm, on the server and on the client. If that cannot be set up the hub is left out of the parts.
- A joined Torque Interface's server tick only checks the gap between the faces and disconnects past 4 cm. While
  either side is a Drive Coupler, the mod skips that check.

Stock meshes for now: the bearing looks like the hinge, the suspension like the rail, the hub like the Wheel and
the coupler like the Torque Interface.
