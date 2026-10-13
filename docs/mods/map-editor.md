# MapEditor

> MapEditor hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.24 / Steam build 25826614 and turns itself off on any other build.
> **Prototype (0.1.0):** not yet tested in game. The first runs are diagnostic; read `anymaker_modding.log`.

MapEditor lets you make custom maps inside the game. It opens Anymaker's own world editor, the one the
developers use to build the world, and keeps each custom map as a **map pack** that the game loads in place of
the normal world.

## Using it

1. In **Settings → MapEditor**, type a name in **Map pack** (for example `My Island`) and apply. A new name
   starts as a copy of the normal world, saved in `<game>\AnyAPI and Modding\maps\My Island\`.
2. Create a new world (Creative is easiest) and press **F7**. The editor's menu bar appears at the top of the
   screen; open its windows (terrain chunks, tiles and junctions, props, loot, spawns...) from there.
3. The mouse cursor is free while the editor is open. Hold the **right mouse button** to look around.
4. Saving in the editor writes the changed tiles into the map pack, never into the game's own files.
5. Press **F7** again to close the editor.

To play a map, keep its name in **Map pack** and create a new world. Clear the setting to go back to the normal
world. Existing saves keep the props they already stored, so map changes show fully only in new worlds. Every
player in a multiplayer game needs the same map pack.

| Control | What it does |
| --- | --- |
| **F7** | Open or close the map editor (in a world; rebind it in **Controls → MapEditor**) |
| **Right mouse (hold)** | Look around while the editor is open |

MapEditor needs [AnyHelpers](helpers.md) to choose a map pack. Without it the editor stays closed.

## How it works

The world is one small file per triangle of the world grid in `rom/data/tiles/0`. See
[map-format.md](../development/map-format.md) for the format.

- **Editor:** the game still contains its developers' world editor, and the renderer still draws it, but the
  release build never calls its entry point (`update_ui` in `main.gc`). MapEditor calls it after the game's
  in-world UI each frame while the editor is open, and releases the mouse cursor (`frontend_ui.get_is_mouse_capture_requested`).
  If the call ever crashes, MapEditor switches the editor off for the session and logs it.
- **Map packs:** MapEditor hooks the call cells of the game's file functions (`file.read_string`,
  `file.read_binary`, `file.write_string`, `file.get_is_exists` and the folder listings). Paths under the game's
  `data/tiles` are sent to the chosen map pack instead. With no pack chosen, writes to the game's tiles are
  blocked. While the editor is open, its writes to any other game data file are blocked too.
- On start it checks which of the game's path stores opens an absolute path, by asking whether `game.exe`
  exists, and logs the paths the game reads so the format can be checked.
