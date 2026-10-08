# API service reference

The declarations in [sdk/include](../../sdk/include/) are the exact public ABI.
Resolve services with `AnyAPI_Services()->query(name, version)` during
`AnyAPI_ModReady`. Validate returned pointers, versions and structure sizes.
Versions in parentheses are the AnyAPI release that introduced the service; rows
without one have been available since 0.27.0 or earlier. For a worked example that
uses several services together, see the [mod tutorial](../development/mod-tutorial.md).

| Surface | Contract | Header |
| --- | --- | --- |
| DLL lifecycle, input, player copies and canvas | [SDK overview](../../sdk/README.md) | `anyapi_mod_v1.h` |
| Named services and input filters | [Examples](../../native/examples/) | `anyapi_services_v1.h` |
| Settings tabs and menu extensions | [Menu integration](menu-extensions.md) | `anyapi_menu_v1.h`, `anyapi_menu_v2.h` |
| Native settings rows | [Settings widgets](settings-widgets.md) | `anyapi_menu_v3.h` |
| Current game UI state | [UI state](ui-state.md) | `anyapi_ui_state_v1.h` |
| Local hotbar, inventory tools and native selection (0.31.0) | [Equipment](equipment.md) | `anyapi_equipment_v1.h`, `anyapi_equipment_v2.h` |
| Experimental native control hints (0.31.0) | [HUD hints](hud-hints.md) | `anyapi_hud_hint_provider_v1.h` |
| World day/night time (0.30.0) | [World time](world-time.md) | `anyapi_world_time_v1.h` |
| Session/game mode | [Session](session.md) | `anyapi_session_v1.h` |
| Item and component definitions | [Item catalog](item-catalog.md) | `anyapi_item_catalog_v1.h` |
| Item previews | [Item images](item-images.md) | `anyapi_item_images_v1.h` |
| Queued item requests | [Inventory actions](inventory-actions.md) | `anyapi_inventory_actions_v1.h`, `anyapi_inventory_actions_v2.h` |
| Native storage callbacks and widgets | [Inventory UI](inventory-ui.md) | `anyapi_inventory_ui_v1.h` through `anyapi_inventory_ui_v3.h` |
| Shared inventory layout | [Screen layout](screen-layout.md) | `anyapi_screen_layout_v1.h` |
| GPU UI drawing | [GPU drawing](gpu-drawing.md) | `anyapi_gpu_draw_v1.h` |
| GPU effect passes | [Post-processing](post-processing.md) | `anyapi_post_process_v1.h` |
| Native scene rendering controls (0.28.0) | [Scene controls](scene-controls.md) | `anyapi_scene_controls_v1.h`, `anyapi_scene_controls_v2.h` |
| Scene SMAA before HUD (0.28.0) | [Scene antialiasing](scene-antialiasing.md) | `anyapi_scene_antialiasing_v1.h` |
| HDR fog, sun shafts and local beams (0.29.0) | [Scene lighting](../scene-lighting.md) | `anyapi_scene_lighting_v1.h`, `anyapi_scene_lighting_v2.h` |
| Optional editable settings | [Helper settings](helper-settings.md) | `anyhelpers_settings_v1.h`, `anyhelpers_settings_v2.h` |
| Optional keybind registry | [AnyHelpers](../mods/helpers.md) | `mod_controls_v1.h` |
| Creation centre of mass (0.33.0, development) | [Creation balance](creation-balance.md) | `anyapi_creation_balance_v1.h` |

GPU post-processing requires API 0.26.0 or newer. Conditional settings presentation
requires the updated AnyHelpers source. Both v1 settings and prior public API
tables remain supported. The stable catalog provides API 0.32.0; the manager's
bundled offline copy is API 0.27.0.

Native contract snapshots remain beside implementation files in `native/`.
They identify reviewed game bodies and layouts; they do not grant compatibility
with another game build or advertise disabled legacy hooks as active APIs.

## Framework foundations

- [Build information](build-info.md)
- [Client task scheduling](client-tasks.md)

## Experimental game access

[Full reference and binding tools](../../sdk/experimental/README.md) expose known
functions, layouts, globals and hook routes, including unvalidated pathways.
Generated bindings run inside mods and use the existing host build-check service.

## Development: creation balance

[Creation balance snapshots](creation-balance.md) document the AnyAPI 0.33.0 development service used by [AnyBalance](../mods/balance.md). Bare-body targeting still awaits a live-world check.
