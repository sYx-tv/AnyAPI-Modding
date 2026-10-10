# AnyBalance

AnyBalance displays a creation's native client physics-shape centre while you equip the **Properties Tool** and aim at that creation. Switch tools, look away, open a menu or leave the game window to hide it.

The gold marker identifies the centre of mass. Coloured X/Y/Z guides show the build's local axes. A dashed guide connects the centre to the build's local lower bounds. The summary shows centre height, X/Z offsets from the bounds centre and mass. From 1.2.0 (not yet released) it also shows size (X by Z, then height, from the build's local bounds). Optional wireframe bounds help you interpret those offsets.

Open **Mod Settings → AnyBalance** to change marker size and opacity, or toggle axes, bounds, the height guide and summary. It works without AnyHelpers using the default display.

This is a separate mod. AnyBalance **1.1.0** requires **AnyAPI 0.33.0** (fluid mass needs **0.34.0**), Anymaker **0.1.23**, Steam build **25755694**; install both from the manager. It does not work on Anymaker 0.1.24 with AnyAPI 0.35.0 yet; a fix follows. Marker anchoring and bare-body targeting (chassis, plate edge, door handle and tyre) were confirmed in game before release.

## What the readings mean

The centre comes from the game's weighted client physics-shape construction. In a locally hosted world, connected hinges, mounts, latches, sliders, hydraulic connectors and tow connections are followed to combine the bodies into one mass-weighted creation. The heaviest body supplies a consistent local frame, so aiming at a door or chassis gives the same centre. It is not a geometric midpoint invented by the mod. Bounds and transforms come from the game's native getters.

Height and offsets use the build's local axes, including when the build is tilted. A high centre can help explain balance problems, but this display does not calculate a tipping threshold, tyre contact polygon or axle loads.

The summary shows **Creation mass** for a verified connected assembly, or **Body mass** for one body. **incl. fluid** after the mass means tank contents are counted (see below). Static or kinematic bodies may not provide a useful mass, so that row is omitted. Assemblies wait for complete positive body masses rather than displaying a partial or unweighted result.

Connection topology is currently captured from the locally hosted server. A remote multiplayer server does not expose that topology to this client service, so the display describes the targeted body there.

**Fluid.** The game simulates fluid weight outside the tank part itself: each liquid tank creates a separate server-side physics box, linked to the tank by a distance constraint, and sets that box's mass to the current fluid mass every tick (minimum 0.01 kg). With AnyAPI 0.34.0, AnyBalance copies each tank's fluid mass and live position on the locally hosted server and adds them to the centre and total, shown as **incl. fluid**. Sloshing moves the fluid box, so the centre shifts slightly as the creation accelerates. On a remote server the fluid boxes are not visible to the client, so fluid is left out and the label is not shown.

**Cargo.** No code path was found that adds stored item mass to a vehicle body, so items in storage do not change the centre or mass in the game's physics either.

## Manual test

Equip Properties Tool directly or through AnyQuickWheel, aim at a creation and check that the marker follows it. Switch to another tool and confirm the display disappears. Aim at the bed, cab, hinged door and wheels of the same vehicle and verify that the centre stays attached to one world position. Try a second creation, move around it, and open/close inventory and settings. Adding or removing a heavy structural part should update the marker after the native physics shape rebuild and the next 200 ms mass refresh. Verify that equipping Properties does not slow simulation, and that changing view angle leaves the marker anchored to the same point on the creation.

## Bare vehicle targeting correction

The native Properties tick clears its component target IDs to zero when the hover is not a component. AnyBalance now resolves a zero or negative tool target from the first native hover entry, including vehicle bodies, edges and plates. It rejects non-vehicle hover entries rather than looking through them. Native tool callbacks, assembly sampling and camera projection are unchanged.

Regression coverage includes zero and negative target IDs, empty and wrapped hover vectors, non-vehicle targets and invalid vehicle IDs.
