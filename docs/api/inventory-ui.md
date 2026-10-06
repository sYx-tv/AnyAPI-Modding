# anyapi.inventory_ui v1 / v2 / v3

Generic AnyAPI exposure; AnyStorage owns the toolbar and bulk/sort policy.
Resolve in AnyAPI_ModReady and register an owned native section. The draw
callback runs immediately after the inspected storage's native header inside
the native inventory builder. No pointers owned by the game cross the API.

Frames contain world/menu/storage context identity, monotonic milliseconds,
counts and a copied storage name. Enumerate copied items/grids with copy_item
and copy_grid only within the callback. Tokens are valid only in the current
world/context; item tokens follow the replicated item while moving between
storage and player. A UI generation change invalidates the old job context.

begin_row/end_row/button use native widgets, owner/context-prefixed IDs and
balanced containers. Disabled buttons use native disabled styling. Rows adapt
to storage-grid width. Owner exceptions disable that owner's section and close
its opened rows. No API-defined controls or sort algorithm live in the host.

same_type includes canonical mechanical component identity. can_stack delegates
to the reviewed native merge method and destination capacity. stack submits the
native stack event. quick_store submits native in/out events, with out restricted
to the inspected storage descriptor. accepts checks native filters and rotation
without occupancy; can_move adds dimensions, bounds and copied grid occupancy.
move submits a native location event using the actual actor drop transform.
The source ID layout is type, floor, vehicle, actor, component, item. Location
layout is type, floor, vehicle, actor, component, parent, grid, x, y, rotation,
secondary grid. Item sizes come from native inventory-definition offset 0x170.
Grid dimensions are offsets 0x04/0x08; display origins are 0x0c/0x10.
Native collection vectors have flags at 0x08, count at 0x0c, capacity at
0x10, and element stride at 0x14. The current layout replaces the previously
misidentified count field. Live inspection confirmed player and crate lists;
the regression fixture now uses independently written native bytes and runs
the actual container-header hook, including the player-only exclusion.

Mutation is callback-scoped, owner-scoped, native-inventory-state-scoped and
limited to one event per presentation frame. Submission is not server success.
Consumers must confirm later copied quantity/location changes before progressing.
Private snapshots exclude carried bag contents and their grids as independent
transfer candidates so bags move intact and scratch space never overlaps them.

Reviewed contracts: STORAGE_NATIVE_CONTRACTS.json and anyapi_storage_patterns.h.
51 exact native bodies document collection helpers, constructors, header, event
wrappers, compatibility, quantity/capacity/merge overrides and drop transforms.
The host replaces three reviewed dependency cells transactionally (inventory-
to-column, column-to-container, container-to-header); it preserves originals and
ordinary UI behavior. The inventory-to-column cell is 0xcea0 in the exact
reviewed inventory update body (record 26366), targeting record 26297.
The existing exact executable/GCL startup guard remains in force.

Version 2 (`anyapi_inventory_ui_v2.h`) retains version 1 as its `operations`
table and adds registration with an owned visibility predicate and reserved
native rows. Predicates receive copied authored item metadata (or a copied
native display name for unknown definitions). The mod owns exclusion policy.
The host expands borrowed per-frame column descriptors for cursor spacing,
then restores the original body height in a separate container descriptor.
Original descriptors, item data and grid dimensions are never edited. Version 1
consumers remain supported; AnyStorage uses version 3.

## Version 3 compact widgets

`anyapi_inventory_ui_v3.h` retains the v2 section table and adds compact rows
(up to three two-cell buttons per row), and `symbol_button` with copied hover text.
Symbols are limited to four UTF-8 bytes and tooltips to 240 bytes. Native button
hover/focus is separate from the native enter event; disabled actions cannot
activate. Hover text renders at the inventory end-screen boundary using guarded
native begin_screen, tooltip_entry and end_screen functions, then clears.
It does not occupy inventory cells or hold game pointers. API owns the widget
mechanism; AnyStorage owns its symbols and action descriptions.

## Native tooltip implementation

Compact symbols use the centered native tag-button widget. Cursor coordinates are
converted to bottom-origin native grid cells, offset and clamped to the viewport.
The popup uses a translucent background and sized text, consumes no inventory
rows, and clears when hover ends, focus is lost or inventory closes.

Tooltip text overloads are resolved through their verified dependency chain;
identical templated bodies cannot be distinguished safely by byte uniqueness
alone. Regression fixtures exercise dependency resolution, native cell alignment,
landscape/ultrawide coordinates, invalid positions and edge clamping. The author
confirmed the final native toolbar and tooltips in game.
