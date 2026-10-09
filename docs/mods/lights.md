# AnyLights

> AnyLights hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.23 / Steam build 25755694 and turns itself off on any other build.
> Prototype: not in the catalog yet.

AnyLights gives spot, omni and rotating lights a colour and a flash pattern.

## Properties Tool

Left click a light while holding the Properties Tool. Beside the Properties window an **AnyLights** panel
opens with:

- **Colour**: Stock (the light's own colour) or eleven swatches, plus red, green and blue sliders
- **Pattern**: Steady, Blink, Strobe, Double flash, Pulse, Alternate A, Alternate B, Colour cycle
- **Speed**: 0.25 to 6 Hz
- A live preview dot

Alternate A and B are opposite halves of the same flash, so two lights on A and B take turns. The choice is
saved in the light's **name** as a short tag on the end, for example `Left beacon ALff220032`. The game
saves names with the vehicle, so lights keep their colour and pattern. Pick Stock and Steady to remove it.

## Data port and microcontroller

On a host running AnyLights every light has extra data channels you can wire from a data port, a data link
or a microcontroller:

| Channel | Direction | Value |
| --- | --- | --- |
| `colour_r`, `colour_g`, `colour_b` | Input | 0-1 (or 0-255). Overrides the Properties colour while connected. All 0 means stock |
| `pattern` | Input | 0 Steady, 1 Blink, 2 Strobe, 3 Double flash, 4 Pulse, 5 Alternate A, 6 Alternate B, 7 Colour cycle |
| `pattern_speed` | Input | Hz; snaps to the nearest of 0.25, 0.5, 1, 1.5, 2, 3, 4, 6 |
| `pattern_lit` | Output | 1 while the pattern is in its on phase, else 0 |

Unconnected inputs leave the Properties choice in charge. The panel says when logic is driving the light.
The channels appear on lights built or loaded after AnyLights is ready.

## Co-op

The host turns each light's tag and data inputs into one number and sends it to every player, again every
few seconds for players who join later. **The host needs AnyLights.** Players with AnyLights see the colours
and patterns; players without it see stock lights and the tag in the light's name. Flash timing follows each
player's own clock, so patterns are not in step between players.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| Enable AnyLights colours and patterns | On |
| Add data channels to lights (host) | On |
| Show the panel with the Properties tool | On |

## Test plan

Build `AnyLights.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`. The lines
tagged `anylights` in `anymaker_modding.log` are what we need.

1. Load a world. Expect `Ready after N s`.
2. Open a spot light's Properties window. Expect the panel and a `panel:` line. Pick Red and Strobe: the
   light should flash red, the name should end in a tag, and the log should show `light name ->` and
   `host spot light: ... word`.
3. Repeat on an omni and a rotating light. Save and reload the vehicle: the patterns should come back.
4. Add a microcontroller, open its data connections and look for the AnyLights channels on a light (log:
   `light data channels:`). Wire `pattern` to a constant 4 and `pattern_lit` to an indicator.
5. Co-op: a second player with AnyLights joins and should see the same colours.
