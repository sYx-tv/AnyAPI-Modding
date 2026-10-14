# AnySuspension

> AnySuspension hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> **Prototype, single player only** for now: built from source, not in the catalog yet.

AnySuspension adds two pairs of parts for building your own running gear, like Trailmakers' bearing and straight
suspension.

| Part | Pairs with | What it does |
| --- | --- | --- |
| **Bearing** | Bearing Ring | Like the Hinge Pin and Hinge Knuckle with no stops: the ring's body spins freely all the way round. |
| **Suspension Rail** | Suspension Slider | Like the Rail and Rail Slider, but the slider is sprung: it springs back to where you placed it. |

They are built and connected exactly like the stock pairs. Place the ring on the bearing (or the slider on the rail)
in the editor to start the new body, and interact with the bearing or the slider to connect or release it. Both are
`tech_tier 2` loot in the mechanical group, like the stock rail.

## Suspension

1. Place a **Suspension Rail** on the chassis, pointing along the travel you want (usually up and down). Stretch it:
   its length is the suspension's travel.
2. Place a **Suspension Slider** on it where you want the wheel side to sit. That spot is the slider's rest
   position, and it is saved with the world.
3. Build the wheel side on the slider's body.

The slider's joint has a spring and damper that pull it back to its rest position, and it stops at the rail's ends.
The load makes it settle a little past the rest position: about `9.81 / (2 pi f)^2`, 6 cm at the default 2 Hz.
Every vehicle sags by about the same amount, whatever its weight. The spring is set from the heavier of the two
bodies, shared between all the suspension sliders holding it up.

**Mod Settings** (AnyHelpers):

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| Suspension stiffness (Hz) | 2.0 | 0.5 to 6 | How fast a sprung body bounces. 1 Hz is soft (25 cm sag), 4 Hz a race car (1.5 cm). |
| Suspension damping | 0.5 | 0.05 to 2 | How quickly bouncing dies away. 1 stops without overshoot. |

Without AnyHelpers the defaults apply.

## How it works

- The game's parts are JSON definitions. The bearing pair is the hinge pin pair with its swing limits opened to
  -180 to 180 degrees, which the physics engine (Jolt) treats as no limits. The suspension pair is the rail pair.
  The definitions are written to `AnyAPI and Modding/AnySuspension/anysuspension_parts.json` and added at start-up.
- The game's slider joint has a motor (the Rail Ballscrew Slider uses it to hold position). On every server tick of
  a Suspension Slider, the mod sets that motor to a position spring: the target is the rest position, the stiffness
  `k = m (2 pi f)^2` and the damping `2 zeta sqrt(k m)`, where `m` is the sprung mass share.
- The rest position is kept along the rail, so it does not drift when the game rebuilds the joint on load. It is
  stored in `%APPDATA%\Anymaker\working\anysuspension_rest.json`, which the game copies into each save.

Stock meshes for now: the bearing looks like the hinge, the suspension like the rail.
