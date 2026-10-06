# Native settings tabs

Include `anyapi_menu_v2.h` and query `anyapi.menu`, version 2, during
`AnyAPI_ModReady`. Check the immutable table's version and structure size before
calling it. Public declarations are in [sdk/include](../../sdk/include/).

Register an `AnyMenuTabV2` with a stable ID, display title, order, supported icon
and draw/event callbacks. Optional dirty/default callbacks cooperate with the
native Apply and Reset buttons. The registration belongs to the calling DLL;
game pointers do not cross the interface. Read each registration result.

Draw callbacks run inside the native settings UI. Use the v2 key rows or the
[v3 typed settings widgets](settings-widgets.md) there; do not create widgets from
a worker thread or cache native UI objects. Tabs share native scrolling, layout
and selection. A registered tab does not move or replace unrelated vanilla pages.

OPEN begins draft editing. APPLY commits the mod's pending values. CANCEL ends
editing and releases input capture; it may follow a successful Apply, so saved
values must remain committed. RESET stages defaults. The mod owns persistence
and behavior; the API only supplies the native extension mechanisms.

AnyHelpers uses this service for Mod Controls and Mod Settings. See
[AnyHelpers](../mods/helpers.md) to register options without implementing a whole tab.
