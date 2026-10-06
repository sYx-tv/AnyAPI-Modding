# AnyHelpers

AnyHelpers adds **Mod Controls** and **Mod Settings** as native settings tabs.
It shares the game's scrolling and Apply, Cancel and Reset behavior. The base
API exposes the menu mechanisms; these tabs exist only when AnyHelpers is installed.

## For players

Change a setting or keybind, then select **Apply Changes**. Cancel discards pending
edits. Reset stages defaults for review before Apply. Escape cancels key capture;
focus loss or leaving the menu ends capture. Saved options remain when a mod is removed.

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
