# CustomMap

> CustomMap hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> **Prototype (0.1.0):** not yet tested in game. The first runs are diagnostic; read `anymaker_modding.log`.

CustomMap lets you play worlds built from your own maps. You make a map with the **Anymaker Map Maker**
(`mapmaker/`, a separate program), which saves it as a **map pack** in `<game>\AnyAPI and Modding\maps\<map name>\`.
CustomMap then builds new worlds from that pack instead of the normal world.

## Using it

1. Make or edit a map in the Map Maker and save it. It goes into the game's `maps` folder.
2. In game, open **New Game → Custom Game** (needs the CustomGame mod) and pick your map in the
   **Map** row.
3. Press **Start game**. The world is built from the map pack.

The map is remembered with the world, so loading that save later uses the same map. Existing saves keep the props
they already stored, so map changes show fully only in new worlds. Every player in a multiplayer game needs the
same map pack.

## How it works

The world is one small file per triangle of the world grid in `rom/data/tiles/0`. See
[map-format.md](../development/map-format.md) for the format.

- CustomMap hooks the call cells of the game's file functions (`file.read_string`, `file.read_binary`,
  `file.get_is_exists` and the folder listings). While a map is selected, paths under the game's `data/tiles`
  are answered from the map pack. The pack's `tiles/0` is the whole world (a missing tile is ocean). Trees and
  bushes come from the game only for tiles whose terrain the Map Maker left unchanged (`stock_vegetation` in
  `map.json`), so edited or new land starts bare. Nothing in the game folder is written.
- Other mods choose the map through the `custommap.maps` service ([custommap_v1.h](../../sdk/include/custommap_v1.h)):
  `count`, `name`, `select` and `selected`. CustomGame calls `select` when a world starts or loads.
- On start it checks which of the game's path stores opens an absolute path, by asking whether `game.exe`
  exists, and logs the first tile paths the game reads.
