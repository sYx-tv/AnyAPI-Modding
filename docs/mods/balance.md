# AnyBalance

AnyBalance displays a creation's native client physics-shape centre while you equip the **Properties Tool** and aim at that creation. Switch tools, look away, open a menu or leave the game window to hide it.

The gold marker identifies the centre of mass. Coloured X/Y/Z guides show the build's local axes. A dashed guide connects the centre to the build's local lower bounds. The summary shows centre height and X/Z offsets from the bounds centre. Optional wireframe bounds help you interpret those offsets.

Open **Mod Settings → AnyBalance** to change marker size and opacity, or toggle axes, bounds, the height guide and summary. It works without AnyHelpers using the default display.

This is a separate mod. The local test build requires **AnyAPI 0.32.0**, Anymaker **0.1.23**, Steam build **25755694**. Live-world acceptance is pending; the public manager catalog remains unchanged.

## What the readings mean

The centre comes from the game's weighted client physics-shape construction. It follows the targeted vehicle body; it is not a geometric midpoint invented by the mod. Bounds and transforms come from the game's native getters.

Height and offsets use the build's local axes, including when the build is tilted. A high centre can help explain balance problems, but this display does not calculate a tipping threshold, tyre contact polygon or axle loads. It does not claim that server-only inventory cargo or fluid mass is included in the client shape centre.

If the client physics object provides a positive mass, the summary also shows **Physics body** mass. Static or kinematic bodies may not provide a useful mass, so that row is omitted. It is not presented as the total mass of an articulated creation.

## Local test

Equip Properties Tool directly or through AnyQuickWheel, aim at a creation and check that the marker follows it. Switch to another tool and confirm the display disappears. Try a second creation, move around it, and open/close inventory and settings. Adding or removing a heavy structural part should update the marker after the native physics shape rebuild.
