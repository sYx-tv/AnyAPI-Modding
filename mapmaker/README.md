# Anymaker Map Maker

A Windows map editor for Anymaker. It saves map packs into the game folder; the
[CustomMap mod](../docs/mods/custom-map.md) plays them (pick the map on CustomGame's Custom Game page).

**Status: 0.2.1.** Open the stock world, a map pack, or a new blank island, then edit it with the tools on the left:

| Tool | What it does |
| --- | --- |
| Terrain | Paints terrain pieces onto triangles (flat land, hills, mountains, rivers, shallows, harbours...), or ocean to remove land |
| Height | Raises a triangle 32 m per click (Ctrl lowers); everything on it moves with it |
| Road | Click points to lay a road or railway (road, road_b, road_c, road_d, train tracks) as smooth curves |
| Fence | Drag a line: wooden, picket, security fences or road dividers, posts and panels spaced like the game's |
| Prop | Any of the game's 136 props: street lights (`lamp_post_a`), benches, signs, barriers, containers... |
| Building | Building spots (house, town, industrial, farm, gas station...); the game picks the building when a world is made |
| Loot / Enemy | Loot spots and enemy spawn points |
| Erase | Removes things and roads under the brush |
| Select | Shows a tile's raw data for hand edits |

Ctrl+Z / Ctrl+Y undo and redo. Two crosshairs mark where the game starts new players (Creative/Sandbox and
Survival; fixed in the game). They turn red when there is no land under them. A new blank island is centred on the
Creative/Sandbox start. Terrain is made of the game's own authored pieces, so there is no free-form
grass or dirt painting; buildings come from the game's own set.

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
& "mapmaker\output\Anymaker Map Maker.exe" --edit-test
& "mapmaker\output\Anymaker Map Maker.exe" --self-test "C:\Program Files (x86)\Steam\steamapps\common\Anymaker"
& "mapmaker\output\Anymaker Map Maker.exe" --render "C:\Program Files (x86)\Steam\steamapps\common\Anymaker" world.png 4096 4096
```

`--edit-test` runs every editing tool on a small island and checks undo and a save round trip.
`--self-test` loads every stock tile, checks each tile's position against the grid maths, saves a
temporary map pack and reloads it, and fails if anything changed. `--render` writes the map view to a PNG.

How the game stores its world, and the map pack format, are in
[docs/development/map-format.md](../docs/development/map-format.md).

`tools/survey_world_data.py` summarises the installed game's world data (schemas and id lists, no
assets) and is how the format notes are checked against each game build.
