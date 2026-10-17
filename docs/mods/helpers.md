# AnyHelpers

AnyHelpers adds **Mod Controls** and **Mod Settings** as native settings tabs.
It shares the game's scrolling and Apply, Cancel and Reset behavior. The base
API exposes the menu mechanisms; these tabs exist only when AnyHelpers is installed.

## For players

Change a setting or keybind, then select **Apply Changes**. Cancel discards pending
edits. Reset stages defaults for review before Apply. Escape cancels key capture;
focus loss or leaving the menu ends capture. Saved options remain when a mod is removed.

## Duplicate tool favorites

When you paste with the Duplicate tool, the load screen gets a third tab, **Favorites**, after Local and
Workshop. It lists only your starred local saves, with the game's own rows, thumbnails and Load.
Hover a save and press **F** (rebind it under Mod Controls, AnyHelpers) to star or unstar it. Starred saves
show `* ` before their name in Local. Stars are kept by save name in `AnyAPI and Modding/mods/AnyHelpers/favorites.tsv`,
so renaming a save file outside the game drops its star. Workshop items can't be starred yet.
This part uses the experimental SDK and is tied to the game build; after a game update it stays off until it is re-checked.

## For mod authors

Register settings through `anyhelpers.settings` v1 and actions through
`anyhelpers.controls` v1 or its compatible `modcontrols.bindings` alias.
Use stable IDs, readable labels and defaults. Read committed values and binding
keys in your implementation. Arbitrary mod variables are not discovered automatically.

The optional [settings v2 presentation service](../api/helper-settings.md) supports
conditional rows, including compact menus with advanced tuning. It preserves
v1 consumers and saved identities. Keep defaults if AnyHelpers is absent.

Public declarations: `anyhelpers_settings_v1.h`, `anyhelpers_settings_v2.h` and
`mod_controls_v1.h` in [sdk/include](../../sdk/include/).
Examples: [native/examples](../../native/examples/).
Saved options: `AnyAPI and Modding/mods/AnyHelpers/settings.tsv`.
