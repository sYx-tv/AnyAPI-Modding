# AnyCombat

> AnyCombat hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.27 / Steam build 26113663 and turns itself off on any other build.
> It is a prototype: built from source, not in the catalog yet.

AnyCombat adds hit feedback to fights: a hit marker around the crosshair and floating damage numbers.

## Using it

There is nothing to press. Whenever you hit a zombie, creature or other player:

- an **X** flashes around the crosshair, and
- the **damage** pops up where the hit landed, rises and fades. Quick hits on the same target (shotgun pellets,
  bursts, a flurry of melee hits) add up into one number.

When your hit **kills** the target, the X flashes red and a little larger, and the number turns red.

Damage to your own character is never shown. Numbers only appear for hits that you dealt, so in co-op you see your
own hits, not your teammates'.

With [AnyHelpers](helpers.md) installed, **Settings → AnyCombat** has:

| Setting | Default | Meaning |
| --- | --- | --- |
| Hit marker | On | The X around the crosshair |
| Damage numbers | On | The floating numbers at the hit point |
| Number size | 1.0 | Scales the numbers (0.5 to 2.5) |

Without AnyHelpers the mod works with these defaults.

## How it works

When the local player hits something, the game's client reports the hit to the server through
`client_peer.data.push_event_actor_take_damage`: the actor's id, the damage, the hit point, the surface normal and
the damage type. Melee stabs (`item_world.melee.tick_stab`) and bullets and other projectiles
(`projectile.apply_impact_effects`) all go through it, so a hook there sees exactly the hits you dealt and where they
landed. Hits on the local player's own actor (`client_scene.actor.container.get_peer_actor`) are skipped.

For kills, every target that was still up when you hit it is looked up by id
(`client_scene.actor.container.get_actor_by_id`) each frame for 1.5 seconds, reading its replicated "incapacitated"
flag (`client_scene.actor.get_is_incapacitated`, which the server sets when a zombie, creature or player goes down).
If it goes down inside that window, the hit counts as your kill. Hitting a target that is already down never does.

The reported hit point is relative to the target, so the number is pinned to the target's own position
(`client_scene.actor.m_transform`) plus that offset, and follows the target while it is on screen.

The camera comes from a hook on `client_scene.get_camera`, which builds the exact camera the frame renders with
(its transform and vertical field of view, after hand-held sway and shake; its direction fields are still empty at that
point, so the basis comes from the transform). Each frame the
numbers are projected from their world hit point with that camera and drawn with [GPU drawing](../api/gpu-drawing.md).

The pure logic (projection, merging rapid hits, timing, formatting) is in `native/anycombat_logic.h` and covered by
the `anycombat_logic` test.

## Limits and open questions

- **Damage scale.** The number is the game's own damage value as reported. The first 20 hits and kills of each
  session are logged with their damage and type.
- **Shared kills:** if a teammate lands the killing blow within 1.5 seconds of your hit, it still shows red for you.
- **Joining clients and dedicated servers:** the hook is on the client side, so it should work for a joining player
  too, but only single player / host has been considered so far. Untested.
- Vehicle rams: whether hitting zombies with a vehicle goes through the same report is not known yet; the log shows
  the damage type of every early hit.
