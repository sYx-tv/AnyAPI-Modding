# CustomGame

> CustomGame hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.24 / Steam build 25826614 and turns itself off on any other build.
> CustomGame 0.1.0 requires AnyAPI 0.36.0 or newer on that build; install it from the manager.

CustomGame adds a **Custom Game** button to the New Game screen. It opens a settings page; **Start game** then
creates a normal Career, Creative (dome) or Sandbox world with your settings applied. The settings are saved with
that world and come back every time you load it.

## Using it

1. Click **New Game** in the main menu. The **Custom Game** button appears at the bottom middle of the screen.
2. Pick settings on the four tabs. Click **◀ / ▶** (or click the value: left click goes up, right click goes down).
   Changed settings are highlighted and each tab shows how many it has. **Esc** or **Back** closes the page.
3. Click **Start … game**. The world is created exactly like the matching native button, then the settings apply.

The page remembers the last settings you started with. The **Presets** tab saves and loads three slots.

| Tab | Setting | Range | Notes |
| --- | --- | --- | --- |
| General | Game mode | Career, Creative, Sandbox | |
| | Day cycle speed | Normal, Frozen, 0.25x to 8x | Normal keeps the game's rules (Sandbox time stands still) |
| | Start time | Game default, 00:00 to 23:00 | |
| | Starting loot tier | 1 to 5 | Set once when the world starts; finishing bunkers still raises it |
| | Max players | 2 to 20 | The game's own limit is 6; above 6 is untested |
| Survival | Hunger, thirst, stamina drain, clothes soak speed | Off, 25% to 400% | Off means it never happens |
| | Stamina recovery, clothes dry speed | 25% to 400% | |
| | Breath underwater | 25% to 500% | Air and drowning time |
| | Bodies stay for, your lost body stays for | 25% to 800% | Shown in minutes |
| | Coldest / hottest temperature | −30 to +30 °C | Shifts the game's temperature limits |
| Danger | Zombies, creatures | On, Off | |
| | Zombie horde size | 0% to 300% | |
| | Max enemies around | 25% to 300% | Zombies, creatures and survivors |
| | Enemy sight range, enemy hearing range | 25% to 300% | Zombies, creatures and survivors |
| | No-spawn zone | 0% to 300% | How close to players enemies may spawn |
| World | Loot amount | 25% to 500% | |
| | Weather | Normal, Always clear, cloudy, rain, storm, fog | |
| | Rain and snow density | 0% to 100% | Looks only |
| | Joint strength | 25% to 500%, Unbreakable | Hinges, latches, pins and rails |

## Multiplayer

Only the host needs CustomGame for the gameplay settings, the day cycle and the player limit. Weather and rain
density are worked out on each player's computer, so players without the mod see the game's normal weather.

## Files

Everything lives in `<mods folder>\CustomGame\`:

- `worlds\<world seed>.cfg`: one file per custom world. Deleting it makes that world play with normal rules.
- `last.cfg` and `preset1.cfg` to `preset3.cfg`: the page's remembered settings and preset slots.

The files are plain `key=value` text. Hand edits snap to the nearest menu step.

## How it works

- The button and page are drawn with `anyapi.gpu_draw`. The mod knows the New Game screen is open because it hooks
  `frontend_ui._update_ui_new_game`, and **Start game** calls the game's own `frontend_ui._start_new_game` with the
  chosen mode.
- `server_scene._generate_new_game` binds the settings to the new world's seed. Each time the host runs a world
  (`server_scene.tick_loading` / `tick`), the file for its seed is applied, or the game's defaults are restored.
- Most settings scale the game's own tuning globals (hunger seconds, AI sight distance, loot capacity and so on)
  from the values read at start-up. The values are written to `anymaker_modding.log`.
- Day speed and start time drive the server time of day's replicated override, which every client already obeys.
  The override value also carries the weather lock and rain density in its lowest bits, so other players with
  CustomGame see the same weather. Weather is locked by hooking `weather_manager.get_weather_data`.
- The starting tier calls `server_scene.set_loot_level` once, on the first tick of a new world.

## Known limits

- More than 6 players is untested: the game, its networking and its UI were built for 6.
- A world started with a higher tier skips the loot of the earlier tiers, the same way finishing bunkers does.
- With a fixed day speed or start time, the game's developer time controls are overridden.
- The field offsets come from the 0.1.24 SDK reference; after a game update, re-check them as well as the
  bindings in `native/customgame_bindings.h`.
