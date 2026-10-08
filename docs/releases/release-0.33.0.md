# AnyAPI 0.33.0 and AnyBalance 1.0.0

AnyBalance is a new optional mod. Equip the **Properties Tool** and aim at a creation to see its native centre of mass, local X/Y/Z axes, an optional bounds box and a height/offset summary. Connected bodies in a locally hosted world are combined into one mass-weighted centre. Settings are under **Mod Settings → AnyBalance**. See the [mod guide](../mods/balance.md).

AnyAPI 0.33.0 adds the `anyapi.creation_balance` v1 service that AnyBalance uses ([API contract](../api/creation-balance.md)). Existing mods are unchanged and keep working with 0.33.0.

Update AnyAPI and install AnyBalance in the manager, then restart the game. Both target Anymaker 0.1.23 / Steam build 25755694.

All 51 native checks passed on Windows. In a locally hosted world the author confirmed that the marker stays anchored as the camera moves; that aiming at a bare chassis, a plate edge, a door handle and a tyre all resolve the same creation; that the display hides when looking away or switching tools; and that ordinary Properties actions and simulation speed are unaffected.

Limits: remote multiplayer servers do not expose connection topology to the client, so there the display describes the targeted body only. Server-only cargo and fluid mass are not included.
