# AnyLaser 0.1.0

A new standalone mod with two parts:

- **Laser Block:** shoots a thin red beam out of its lens, up to 100 m, using the game's own physics raycast.
  Outputs `hit` and `distance`; the `off` input switches it off (unconnected = on).
- **Laser Sensor:** outputs `lit`, `broken` and `distance` when a Laser Block's beam lands on its lens.
- Players walking through the beam break it.
- Sandbox and Creative: the sensor container. Survival: tier 3 mechanical loot (from the 2nd bunker).
- Single player and host. Every player needs the mod; joining players don't see the beam yet.
- Details: [AnyLaser](../mods/laser.md).

Publishing: upload `release.json` from [anylaser-0.1.0/](anylaser-0.1.0/release.json) with the ZIP
(`AnyAPI and Modding/mods/AnyLaser.dll` only).
