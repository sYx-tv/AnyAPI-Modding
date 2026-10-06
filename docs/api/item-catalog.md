# AnyAPI item catalog v1

Include `anyapi_item_catalog_v1.h` and query `anyapi.item_catalog`, version 1, through AnyAPI_Services in AnyAPI_ModReady. This is a generic base-API service; AnyInventory owns the browser UI, filters and favorites. AnyHelpers owns the optional controls/settings registry.

The provider reads `rom/data/inventory_definitions.json`, `rom/languages_items.tsv` and `rom/languages_item_descriptions.tsv` at process startup. English localization falls back to authored text. Successful startup logs ITEM_CATALOG ready=1 and the definition count. A malformed/unavailable asset fails closed to an empty catalog, with a reason logged; no native inventory pointers or hooks are needed.

`count()` returns the immutable process catalog size. `copy(index, &item)` requires matching struct_size/version and returns an owned metadata copy. An invalid argument returns false. Check valid_fields before using width/height, tech_tier or default_stack. Strings are UTF-8, fixed capacity and NUL terminated; oversized authored metadata rejects the catalog instead of silently truncating it.

`definition_json(index, bytes, capacity, &required)` exposes every authored field in that record, including fields absent from convenience metadata. Query with null bytes and zero capacity first: it returns false and reports the required size including NUL. Allocate that many bytes and call again. Insufficient buffers receive no partial JSON; invalid index or missing required returns false. The JSON source is retained exactly for the selected definition. Indices are stable within a process; use IDs for persistent references across game updates.

The service/table and catalog remain valid for the process. No hot reload/unload is supported. The parser bounds assets at 32 MiB, recursion at 64 and node counts at 300,000; definitions are capped at 10,000. It checks JSON structure, duplicate keys/IDs, numeric grammar and escaped Unicode surrogate pairs. It does not claim a general UTF-8 validator for raw unescaped asset bytes.

This is authored item information, not owned inventory, live quantity, network state, recipe availability or permission to spawn. Default authored stack is not maximum stack. Native item icon rendering is not exposed. No game assets are bundled in the public source/SDK archives.

See `examples/item_catalog.cpp` for querying and copying a record and full JSON. `item_catalog_test` verifies the current October 5 Steam build assets and malformed parser cases. `inventory_browser_test` verifies the real DLL and AnyHelpers with the current catalog fixture; normal Steam startup evidence is recorded separately.

copied previews, native Add requests and generic screen offsets are separate services; see item-images.md, inventory-actions.md and screen-layout.md. Original JSON copying remains available; the browser export controls were removed. Current target is v0.1.21, Steam build 25725299.

## Component definitions

The provider also loads rom/data/vehicle_component_definitions.json, languages_components.tsv and languages_component_descriptions.tsv. This build has 372 inventory items and 598 components, 970 entries total. Components set ANY_ITEM_VEHICLE_COMPONENT in valid_fields and use component:<authored_id> as stable IDs, avoiding collisions. The original definition_json retains the unprefixed authored ID and every original field. Component inventory dimensions remain unspecified rather than inferred from world size. Search includes mechanical, engine, gas, torque and building categories.
