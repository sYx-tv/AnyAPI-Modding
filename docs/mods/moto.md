# AnyMoto

> AnyMoto hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.27 / Steam build 26113663 and turns itself off on any other build.
> 0.0.1 is a test build: nothing below has been tried in game yet.

The game has a **Motorcycle Wheel** item, but no part accepts it. AnyMoto adds five parts for building a real
two-wheeler around it:

| Part | What it does |
| --- | --- |
| Motorcycle Fork | Front suspension. Takes the Motorcycle Wheel, steers up to 30 degrees from the steering links, brakes from the brake link, 14 cm of travel. The fork legs slide as it compresses. |
| Motorcycle Swingarm | Rear suspension. Takes the Motorcycle Wheel, brakes, 12 cm of travel. The arm swings and the shock compresses. Drive comes in through the torque face on its side at the pivot. |
| Lean Assist | Keeps the bike upright and leans it into turns. **Power** (0 to 1, default 0.6) is in the Properties tool. The data input `assist` switches it off at 0 (unconnected = on); outputs `lean` and `target_lean` in degrees. |
| Motorcycle Seat | A saddle with the normal seat control faces. |
| Handlebars | The steering wheel's links on bars that turn about a raked axis. Seated hands go to the grips. |

## Building a bike

1. Put the **fork** on the front face of the frame and the **swingarm** on the back face. Fit a Motorcycle Wheel
   on each.
2. Put a **pulley wheel** on the swingarm's side torque face and run a **belt** to a pulley on the engine's output
   (or a gearbox). This is how the rear wheel is driven: the swingarm passes torque from that face to the wheel,
   like a stock wheel's torque face.
3. Place the **seat**, and the **handlebars** on the seat's front control face (or anywhere a steering wheel would
   go). Link the handlebars' steering link to the fork's steering links and the brake pedal link to the brakes.
4. Place a **Lean Assist** anywhere on the frame with its arrow pointing forward.

## How it works

The stock wheel part is the suspension: the spring is a raycast inside the wheel, tuned by a code-side table the
game picks by part id. AnyMoto gives the fork and swingarm copies of the stock wheel's table with the Motorcycle
Wheel as the only allowed tyre, the tyre turned to run along the bike, and their own travel and steering lock. The
part definitions are written to `AnyAPI and Modding/AnyMoto` and their generated meshes to
`rom/meshes/components` (`anymoto_*.mesh`) when the game starts.

Lean Assist is a gyro sensor that sets the vehicle's roll rate toward a target lean (upright when going straight,
leaning into a turn by speed and turn rate). It gives up past 70 degrees, so a crashed bike stays down.

## Feasibility

| Piece | Status |
| --- | --- |
| Fork and swingarm accept and spin the Motorcycle Wheel, suspension, steering, brakes, drive through the torque face | Likely: the same game code as the stock wheel, with our tuning table |
| Moving fork legs, swingarm and shock | Likely |
| Lean Assist holding the bike up | Likely: needs tuning in game |
| Handlebars turning, hands on the grips | Unproven: the hand placement hook is new |
| A straddling rider pose | Not possible yet: the game has only two seated poses (Properties: Pose) |
| Settings saved with the world | Not yet: Lean Assist Power resets on load |

## Known limits

* Single player and host only for Lean Assist (it runs where the vehicle's physics runs).
* The Motorcycle Wheel is still a loot item (tier 2); AnyMoto does not change where it spawns.
