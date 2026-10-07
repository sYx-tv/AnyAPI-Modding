# Native control hints

The AnyAPI 0.31.0 local candidate accepts optional hint providers published as `hud.hints.<mod_id>`, version **1**. The ABI is declared in [anyapi_hud_hint_provider_v1.h](../../sdk/include/anyapi_hud_hint_provider_v1.h).

Publish a process-lifetime `AnyHudHintProviderV1` during `AnyAPI_ModReady`. Its `copy` callback fills one `AnyHudHintV1` with a Windows virtual key and a short UTF-8 label, then returns true. Return false to hide the row, including when the mod is disabled, the action is unbound or the gameplay context is inappropriate. Callbacks run during native UI building; use thread-safe copied state and avoid engine calls.

The framework appends up to eight provider rows to the existing bottom-left control panel. Rows use the game's keyboard icons, text, spacing and background. The panel grows by the native row height. The game's control-hint visibility setting remains authoritative. Faulted mod owners are excluded.

The integration resolves `frontend_ui._update_ui_control_hints` through the unique `frontend_ui.update_client_ui` caller. Only that caller's update cell and the hint builder's screen/end-container cells are replaced. The additional rows are inserted before the hint background closes. This does not change native input bindings or the game's localization/action enums.
