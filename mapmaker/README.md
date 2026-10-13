# Anymaker Map Maker

A Windows map editor for Anymaker. It saves map packs into the game folder; the
[CustomMap mod](../docs/mods/custom-map.md) plays them (pick the map on CustomGame's Custom Game page).

**Status: 0.1.0.** Opens the stock world or a map pack as a top-down map (terrain, roads, junctions, props,
building spots, loot, spawns), lets you inspect and edit any tile's data, and saves map packs.

## Build and run

```powershell
powershell -ExecutionPolicy Bypass -File mapmaker\build.ps1
& "mapmaker\output\Anymaker Map Maker.exe"
```

The editor finds the game in the default Steam folder, or asks for it once (File > Choose game folder).
It only reads the game folder. Map packs are saved to `<game>\AnyAPI and Modding\maps\<map name>`.

Controls: scroll to zoom, drag with the right or middle mouse button to pan, left click a tile to
inspect it. Edit the tile's JSON on the right and press **Apply tile changes**.

Headless checks (used by the laptop test):

```powershell
& "mapmaker\output\Anymaker Map Maker.exe" --self-test "C:\Program Files (x86)\Steam\steamapps\common\Anymaker"
& "mapmaker\output\Anymaker Map Maker.exe" --render "C:\Program Files (x86)\Steam\steamapps\common\Anymaker" world.png 4096 4096
```

`--self-test` loads every stock tile, checks each tile's position against the grid maths, saves a
temporary map pack and reloads it, and fails if anything changed. `--render` writes the map view to a PNG.

How the game stores its world, and the map pack format, are in
[docs/development/map-format.md](../docs/development/map-format.md).

`tools/survey_world_data.py` summarises the installed game's world data (schemas and id lists, no
assets) and is how the format notes are checked against each game build.
