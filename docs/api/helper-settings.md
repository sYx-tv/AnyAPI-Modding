# AnyHelpers settings presentation v2

Query `anyhelpers.settings`, version 2, and validate the structure version and
size. This is an optional service implemented by AnyHelpers.dll. The base AnyAPI
does not own settings or implement this menu. The v1 service remains available
without changes, and its saved-value identities and file format are preserved.

`visibility(row, first, first_value, second, second_value)` makes a registered row
visible only when both conditions match the corresponding **draft** numeric values.
A zero condition token disables that condition. Zero conditions on both sides
restore unconditional visibility. Registration rejects invalid, self-referencing,
nonfinite or different mod-family conditions. A family is the mod ID prefix before
its first dot, allowing related sections such as `anygraphics.general` and
`anygraphics.bloom` to share one controlling setting. This is presentation metadata,
not a security boundary or a way to hide a setting from v1 consumers.

Hidden settings remain registered, saved and readable; hiding never resets values.
Draft visibility changes immediately while browsing. Apply/Cancel continue to
control committed values. Settings sharing an identical display name form one
heading, ordered by their declared order, with mod ID and setting ID tie breaks.
Persisted identities still use the original mod ID plus setting ID.

AnyGraphics uses Look=Custom as the first condition, and Advanced tuning=On as
the second for detailed rows. Existing map, inventory and storage registrations
remain unconditional. Tests exercise compact/advanced row counts, staged visibility,
cancel behavior, all five presets and restoration of saved custom values.
