# AnyBalance

AnyBalance displays a creation's native client physics-shape centre while you equip the **Properties Tool** and aim at that creation. Switch tools, look away, open a menu or leave the game window to hide it.

The gold marker identifies the centre of mass. Coloured X/Y/Z guides show the build's local axes. A dashed guide connects the centre to the build's local lower bounds. The summary shows centre height and X/Z offsets from the bounds centre. Optional wireframe bounds help you interpret those offsets.

Open **Mod Settings â†’ AnyBalance** to change marker size and opacity, or toggle axes, bounds, the height guide and summary. It works without AnyHelpers using the default display.

This is a separate mod. The local test build requires **AnyAPI 0.33.0**, Anymaker **0.1.23**, Steam build **25755694**. Live-world acceptance is pending; the public manager catalog remains unchanged.

## What the readings mean

The centre comes from the game's weighted client physics-shape construction. In a locally hosted world, connected hinges, mounts, latches, sliders, hydraulic connectors and tow connections are followed to combine the bodies into one mass-weighted creation. The heaviest body supplies a consistent local frame, so aiming at a door or chassis gives the same centre. It is not a geometric midpoint invented by the mod. Bounds and transforms come from the game's native getters.

Height and offsets use the build's local axes, including when the build is tilted. A high centre can help explain balance problems, but this display does not calculate a tipping threshold, tyre contact polygon or axle loads. It does not claim that server-only inventory cargo or fluid mass is included in the client shape centre.

The summary shows **Creation mass** for a verified connected assembly, or **Body mass** for one body. Static or kinematic bodies may not provide a useful mass, so that row is omitted. Assemblies wait for complete positive body masses rather than displaying a partial or unweighted result.

Connection topology is currently captured from the locally hosted server. A remote multiplayer server does not expose that topology to this client service, so the display describes the targeted body there. Server-only fluid and cargo mass remain outside this calculation.

## Local test

Equip Properties Tool directly or through AnyQuickWheel, aim at a creation and check that the marker follows it. Switch to another tool and confirm the display disappears. Aim at the bed, cab, hinged door and wheels of the same vehicle and verify that the centre stays attached to one world position. Try a second creation, move around it, and open/close inventory and settings. Adding or removing a heavy structural part should update the marker after the native physics shape rebuild and the next 200 ms mass refresh. Verify that equipping Properties does not slow simulation, and that changing view angle leaves the marker anchored to the same point on the creation.

The local test API is versioned separately from the public graphics release. Its manager receipt records the matching DLL hash so the manager can recognize it and reject a downgrade to the public API during testing.
