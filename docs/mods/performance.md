# AnyPerformance

> AnyPerformance hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.27 / Steam build 26113663 and turns itself off on any other build.
> It is a prototype: built from source, not in the catalog yet. It is a standalone mod, not part of a bundle.

AnyPerformance finds out where hitches come from and smooths out the ones it can.

## Slow motion and catch-up

The world (vehicles included) is simulated by the game's server at 60 ticks a second, on its own thread, in solo play
too. What you see is drawn from those ticks by a playback clock in `client.tick`: it aims to show the world one tick
behind the newest one. When ticks stop arriving (the server is busy), the clock slows the whole world down; when they
arrive in a burst afterwards, it speeds up, up to 4x, to catch up. That is the "everything slows down, then slams into
high gear" effect, and it is separate from frame rate.

**Sim smoothing** (off by default) changes two constants of that clock in the game's code:

| Level | Buffer (ticks behind) | Fastest catch-up | Extra delay on screen |
| --- | --- | --- | --- |
| Off (game) | at least 1 | 4x | none |
| Light | at least 2 | 2x | about 17 ms |
| Strong | at least 4 | 1.5x | about 50 ms |

A bigger buffer absorbs short server stalls so the world never slows for them; a lower catch-up cap turns the rush
after a long stall into a gentler one. The mod checks the exact instructions that read each constant before changing
it, and leaves the game alone if they differ.

Every slow-motion episode is logged with the worst server tick inside it and what that tick spent its time on:

```
[anyperformance] Slow motion: 0.42x for 0.9 s, catch-up 2.60x for 0.6 s, buffer up to 1.0 ticks | worst server tick 240 ms, mostly bunkers/elevators: loading 3.1 (tiles 1.2), simulation 231.0 (bunkers 198.0, items 12.4, vehicles 6.0, entities 2.2, physics step 9.1, other 3.3), network 1.8, autosave 0.0
```

The server parts are `server_scene.tick_loading` (terrain tiles and dungeon layers), inside it the terrain streaming,
`server_scene.tick` (all simulation), inside it `physics.scene.update` (the physics engine's step: native code in
`game.exe`, hooked through its call cell, which is read from `server_scene.tick` and checked first), `dungeon_manager.tick`
(bunkers and elevators), loose items, vehicles and entities (each with their post-physics step),
`replication.master.replicate_peer` (packing the world for each player) and `server.autosave_game`.

When the worst tick spent time loading a bunker or streaming terrain, the line ends with its biggest steps (inclusive times, so a
step's nested steps count inside it too), for example `| server steps: bunker static load 2412 ms, objects into the
world 2100 ms, bunker save file parse 280 ms`. The steps are the pieces of `server_scene.dungeon_manager.tick_loading`:
entrance load/create/physics, the bunker's static load (save file parse, objects into the world), physics shape,
navmesh, objects load, layers (load and populate with loot, creatures and vehicles), and the saves and unloads when a
player leaves. Terrain streaming steps are the tile's objects and loot zones going into the world, the tile's terrain, path and
tree bodies going into physics (with `physics bodies added` counting every body the server adds), creature spawn zones,
sleeping creatures, keycards and dead-drop loot in wrecks, wreck loot (with its loot tables, required loot, floor loot and furniture loot timed on their own, and the furniture's loot table, placement, item creation and storing), navmesh tiles, and the save and unload of tiles
left behind.

## Wreck loot on new terrain

When a terrain tile loads on the host, the game fills every wreck on it with loot (`server_scene.vehicle.generate_loot`:
required, floor and furniture items) in one server tick, about 7 ms a wreck. A tile with 80 wrecks held the server for
over 600 ms, the biggest cause of slow motion while driving. **Spread wreck loot** (on by default) lets 8 ms of wreck
loot run per server tick and queues the rest by vehicle id, filling it over the next ticks (80 wrecks take about 1.3 s,
on tiles at the edge of the loaded area). A wreck that is gone by then is skipped. A world save on the server thread
fills the whole queue first, and a tile save fills that tile's own waiting wrecks (logged as `tile X,Y,Z was saved`). One wreck can cost 100-280 ms on its own in towns; every millisecond over the budget is paid back at 4 ms a tick before the next queued wreck runs, so heavy wrecks are spaced out and the world catches up between them. Only a save from the pause menu in that second can miss queued loot; the log
says so when it happens. Each batch logs `Spread wreck loot: N wrecks filled over M server ticks`.

## Fast navmesh cleanup

When a terrain tile unloads, the server checks every navmesh cell around it, about 8,300 of them, and each check
first lists the terrain tiles in range (56 ms for one unload in a town log). Most cells have no navmesh tile, and then
the check changes nothing. With **Fast navmesh cleanup** on, the mod keeps a set of the cells that have one and answers
the empty ones itself; the rest go to the game as before. Every 30 s the log shows
`Fast navmesh cleanup: N empty cells answered by the mod, M left to the game`.

## Distant container items

Every frame the game places every item inside every container of every loaded vehicle, including wrecks full of
loot far away. In the 0.1.2 profile that was 13% of the main thread. With **Distant container items** on, a
container on a vehicle more than 50 m from you that has not moved is updated every 8th frame, with the skipped
frame time passed on; anything near you or moving is updated every frame. Every 30 s the log shows
`Distant container items: N container updates, S skipped (P% saved), F far ones run, X with no player position`.

## Frame hitches

**Hitch logger.** Every frame slower than the hitch threshold (50 ms by default) is written to
`anymaker_modding.log` along with what that frame spent its time on:

```
[anyperformance] Hitch 87 ms, mostly terrain tile attach: client tick 71.2 (simulation+physics 9.8, streaming 58.3: 1 tile attach 55.1, vehicle rebuild 0.4), rendering/UI 15.8 | last server tick 2.1 ms
```

When the client tick spent time outside the simulation and streaming, the biggest of its other pieces follow
`; other:` (network update, network state, UI update, players, player data, authority update).

Every 30 seconds it also writes a summary:

```
[anyperformance] 30 s: 58.2 fps avg, 1% low 41.0 fps, 1790 frames, 3 hitches (worst 142 ms) | server 60.0 ticks/s, 1.9 ms avg, 6.1 max, spacing jitter 0.4 ms, 0 late, 0 bunched | tiles 4 attached (max 11.2 ms) | timer 1.0 ms
```

followed by where an average frame and an average server tick went in that window (not only the slow ones), with the
biggest timed steps per frame or tick:

```
[anyperformance] Frame average (1650 frames): 18.2 ms = client tick 8.1 (simulation+physics 6.4, streaming 0.3), rendering/UI 10.1 | steps: vehicles 2.2 ms, creatures and players 1.4 ms
[anyperformance] Server tick average (1800 ticks): 22.5 ms = loading 1.1 (tiles 0.9), simulation 18.0 (bunkers 0.0, items 6.1, vehicles 2.4, entities 0.0, physics step 2.0, other 7.5), network 3.1, autosave 0.0 | steps: creatures and players 4.2 ms, creature pathfinding 1.8 ms
```

The simulation steps are the children of `server_scene.tick` (creatures spawning, creature pathfinding, creatures and
players, their physics, projectiles, ocean, weather, pseudo objects, collision events, explosions, radio transmissions)
and of `client_scene.tick` (vehicles, creatures and players, particles, environment, lights, weather particles,
inventories, daylight, interaction prompts, name tags and markers). A server tick over 16.7 ms on average means the host
cannot keep 60 ticks a second and the world runs slow for everyone.

| Part of a frame | Game function timed | What it is |
| --- | --- | --- |
| Client tick | `client.tick` | Everything the client updates in a frame |
| Simulation and physics | `client_scene.tick` | Actors, vehicles, props, particles and the client physics step |
| Streaming | `client_scene.tick_loading` | Choosing terrain tiles to load, plus the two items below |
| Tile attach | `client_scene.environment_tile_task.integrate` | A terrain tile built on the background "Client Tile Worker" thread is added to physics and uploaded to the GPU, on the main thread |
| Vehicle rebuild | `client_scene.vehicle.container._update_tasks` | Vehicle meshes rebuilt on the "Client Vehicle Worker" thread are swapped in |
| Rendering and UI | (the rest of the frame) | Drawing, menus, audio, waiting on the GPU |
| Server tick | `server.tick` | One tick of the server (solo and host only), on its own thread |

The functions are only timed: each hook calls the game's own function unchanged.

**CPU per thread.** With each summary, a CPU line shows how busy each part of the game was (100% = one core fully
used):

```
[anyperformance] CPU (100% = one core): game total 210% (16 logical cores = 1600%) | main thread 92% | server thread 71% | physics workers (15) 30% total, busiest 4% | tile/vehicle workers 6% | other threads 11%
```

The physics workers are the physics engine's (Jolt) job pool, threads named "Worker N". Everything else the server
simulates (items, vehicles, bunkers, network, autosave) runs one step after another on the single server thread, so a
server thread near 100% is the bottleneck behind slow motion.

**Precise timer.** In solo play and when you host, the game's server loop runs 60 ticks a second and waits between
ticks with `Sleep(1)`. On Windows' default 15.6 ms timer that wait can last up to 15.6 ms, so ticks arrive late and
then bunch up to catch up, which shows as stutter on vehicles and physics. The mod asks Windows for a 1 ms timer
(`timeBeginPeriod(1)`), as most games do. The summary's `spacing jitter`, `late` and `bunched` numbers show the
difference: switch the setting off and on to compare.

**FPS overlay** (off by default): FPS, 1% low, the last server tick time, the world speed (orange while it is off
normal) and the cause of the last hitch, in the top-left corner.

## Settings

With [AnyHelpers](helpers.md) installed, **Settings → AnyPerformance** has:

| Setting | Default | Meaning |
| --- | --- | --- |
| Sim smoothing | Off (game) | Off, Light or Strong, as above. Applies at once |
| Precise timer | On | The 1 ms Windows timer described above. Applies at once |
| Hitch threshold (ms) | 50 | Frames slower than this are logged (20 to 1000) |
| FPS overlay | Off | The live readout in the top-left corner |
| Spread wreck loot | On | Fills wrecks on newly loaded terrain a few at a time, as above |
| Fast navmesh cleanup | On | Answers the game's check of empty navmesh cells around unloading terrain itself, as below |
| Distant container items | On | Parked vehicles over 50 m away update the items in their containers every 8th frame, as below |
| Profiler (for testing) | Off | Samples where the main and server threads spend their time, as below |

Without AnyHelpers the mod works with these defaults.

## Profiler

**Profiler (for testing)** is off by default and only measures. When on, a background thread pauses the main and
server threads every 2 ms for a few microseconds, copies the top of the stack and walks it. Every 120 s it logs the
25 functions with the most time spent in them (`self`) and the 25 with the most time under them (`total`):

```
[anyperformance] Profile server thread: 60000 samples in 120 s (every 2 ms). ...
[anyperformance] Profile server self #1: self 12.0% total 30.0% | gcl size 648 code 48 89 5c 24 ...
```

Game logic functions are placed in memory at run time, so the log names them by size and first bytes.
`native/tools/map_profile.py <log> <game.gcl>` turns those into function names offline (add `--db` with the SDK
reference's `sdk_index.sqlite` to name game.exe natives too). It costs about 1% of one core while on.

## Limits

- Slow motion while a menu is open is not logged: a solo world pauses in menus.
- Frames with a gap over 2 s (alt-tab, a dialog) are not counted. Hitches while the game reports it is loading are
  counted separately as "on loading screens".
- When you join someone else's game there is no local server, so the server numbers are absent and the precise timer
  only affects your own client.
- The physics engine itself (Jolt) is compiled into `game.exe`; the mod times its step but does not change it.
- At most 300 hitch and slow-motion lines are logged per session; summaries continue.
