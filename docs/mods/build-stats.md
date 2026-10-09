# AnyBuildStats

> AnyBuildStats reads the vehicle through the [experimental SDK](../../sdk/experimental/README.md), so its
> part counts only work on Anymaker 0.1.23 / Steam build 25755694. Prototype: not in the catalog yet.

AnyBuildStats shows a spec sheet for the creation you aim the **Properties Tool** at, in a card above the
[AnyBalance](balance.md) card (or at the bottom left):

- Mass, size (length, width and height) and centre of mass, from AnyAPI's creation balance service
- Part counts: components, edges, plates and nodes
- Electric motor and alternator power, and motor power to weight
- The most common part categories

Press **F9** (rebindable in AnyHelpers' Mod Controls tab) to copy the sheet to the clipboard as text, ready
to paste into chat or a post. For creations with several bodies, part counts are for the heaviest body.

AnyBuildStats only reads; nothing is sent to other players, and no other player needs it.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| Show build stats with the Properties tool | On |
| Show part categories | On |
| Card position | Right, above AnyBalance |
| Card opacity (%) | 90 |

## Test plan

Build `AnyBuildStats.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`. The lines
tagged `anybuildstats` in `anymaker_modding.log` are what we need.

1. Load a world. Expect `Ready after N s`.
2. Equip the Properties Tool and aim at a creation. Expect the card with mass, size and part counts, and the
   first `sampled vehicle` lines in the log.
3. Add and remove a few parts. The counts should follow within half a second.
4. Press F9 and paste into a text editor.
