# AnyClock

AnyClock adds a brief game-time display to the native HUD. Press **O** while in a world to show it for four seconds in the top-right corner. Press again to restart the timer. Opening inventory or menus, or losing game focus, dismisses it immediately.

## Settings and controls

With AnyHelpers installed, open **Mod Controls → AnyClock → Check game time** to change the key. Open **Mod Settings → AnyClock** to change:

- Display duration, from 1 to 15 seconds.
- Screen corner, or a custom horizontal and vertical position.
- Display size and background opacity.
- 24-hour or 12-hour format, and optional seconds.
- Whether the mod is enabled.

Choose **Apply** to save changes. Custom placement lets you keep the popup clear of the minimap. Without AnyHelpers, the default key and presentation still work.

## Time source and compatibility

Requires **AnyAPI 0.30.0** on the reviewed Anymaker 0.1.23 build. The framework observes the game's native day/night-cycle getter and copies its result, including Sandbox and replicated time overrides. It does not change the world's time or use your computer clock. The normalized cycle is presented as a 24-hour day; there is no exposed calendar or day counter.

The popup uses the existing GPU HUD drawing service and has no separate window or executable. An unavailable or stale native sample displays **Time unavailable**, rather than inventing a time.

**Status:** released as AnyClock 1.0.0 with AnyAPI 0.30.0. All 42 native checks pass, and the author confirmed the live game-time popup works.
