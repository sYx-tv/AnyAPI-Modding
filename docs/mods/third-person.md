# ThirdPerson

> ThirdPerson hooks a game function directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.27 / Steam build 26113663 and turns itself off on any other build.
> ThirdPerson 0.1.3 requires AnyAPI 0.35.0 or newer on that build; install it from the manager.

ThirdPerson adds a third-person camera with an adjustable follow distance.

## Using it

| Control | What it does |
| --- | --- |
| **F5** | Switch between first and third person |
| **Alt + mouse wheel** | In third person, move the camera closer or further (1 to 20 m, 0.5 m per notch) |

A short popup in the lower middle of the screen shows the camera mode and distance after each change.
With [AnyHelpers](helpers.md) installed, F5 can be rebound in **Controls → ThirdPerson**, and **Settings → ThirdPerson** has:

| Setting | Default | Meaning |
| --- | --- | --- |
| Start in third person | Off | Use third person from the moment the game starts |
| Follow distance (m) | 4.0 | The distance used when you switch to third person. Alt + wheel changes it until you next apply settings |
| Steady camera | On | Smooths the chase camera on foot and in vehicles; while seated it also keeps the horizon level through bumps, hills and crashes |

Without AnyHelpers the mod still works with F5 and a 4 m default distance.

## How it works

Anymaker 0.1.25 already contains a third-person camera that only its developer tools switch on: the client
scene has a "camera first person" flag, a "render the player in third person" flag and a third-person camera
distance, and `client_scene.get_camera_transform` (used by rendering, the crosshair and interaction rays)
applies them. ThirdPerson hooks that function and sets those three values, so the game draws your character's
body and aims interactions from the camera with its own rules. The game also hides your own head unless a
developer flag is set, so the mod sets that flag while you are in third person.

For the first frames in third person the mod checks the camera's offset against your character's facing. If the
game's offset points forward it flips the distance; if the game does not move the camera at all, the mod moves it
back along the view itself. Both cases are written to `anymaker_modding.log`.

## Steady camera

Seated, the game's view turns with the vehicle, so in third person every bump, hill and crash used to throw the
camera around. With **Steady camera** on (the default), ThirdPerson builds a chase camera instead: it follows
where you look through a short smoothing filter and keeps the horizon level. In vehicles it also sits a little
higher than your head and gets much calmer while the vehicle is tilted or tumbling. On foot the same smoothing
applies, so looking around with the mouse in third person has a slight, deliberate lag. Turn the setting off for
the game's exact view.

## Known limits

- The camera does not avoid walls yet, so it can clip through them in tight spaces. Lower the distance indoors.
- Aiming and interactions use the camera, as in the game's own third-person mode, so very close objects can be
  harder to target than in first person.
- Only your own view changes. Other players are not affected and do not need the mod.
- The client-scene field offsets come from the 0.1.25 SDK reference; after a game update, re-check them as well as
  the binding in `native/thirdperson_bindings.h`.
