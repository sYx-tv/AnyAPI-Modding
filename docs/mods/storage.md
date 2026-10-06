# AnyStorage

Separate Windows x64 DLL mod for Anymaker 0.1.21 / Steam build 25725299.
Requires the current AnyAPI host. AnyInventory is optional; AnyHelpers provides
optional settings. The toolbar is built with native game widgets under each
external storage header. It scrolls with that inventory and shares its layout
and hit testing. There is no render canvas, external window, or mod executable.

| Button | Behavior |
| --- | --- |
| Withdraw All | Move stored contents to player storage, filling compatible stacks and top-left slots. |
| Deposit All | Move player-stored contents into the inspected storage. Equipped items are excluded. |
| Stack In | Withdraw only types already in player storage; fill compatible stacks first. |
| Stack Out | Deposit only types already in the inspected storage; fill compatible stacks first. |
| Sort | Rearrange inspected storage using the selected order. |
| Alphabetical / Quantity | Switch sorting order. Quantity sorts largest stacks first. |
| Cancel | Stop further submissions in the current job. An already submitted event may finish. |

Stack Existing follows Minecraft matching-type behavior: it can create a new
stack of an existing type after existing stacks fill. Mechanical component
definitions are compared individually. Different components are not treated
as one generic component item. Carried bags move as a whole, including contents.

Sorting plans collision-free native grid moves before rearranging. It respects
item dimensions, grid filters and permitted rotation. A full-container cycle
may need free space in player storage as a temporary buffer. If no safe plan
fits, it stops without rearrangement. Cancelling or closing during a buffered
sort can leave the buffered item safely in player storage. No item is deleted
or deliberately dropped. Native server rules remain authoritative.

The mod submits at most one event per rendered frame, waits for replicated
quantity/location changes, and does not repeat an unconfirmed event. A 2.5-second
timeout skips blocked bulk-transfer items or stops sorting. Changing storage,
closing the menu or changing world stops further submissions. Inventory closure
removes buttons through the native UI lifecycle. Simultaneous manual changes
can stop a planned sort. A server rejection leaves success unconfirmed.

Mod Settings offers: enable toolbar, show Stack Existing buttons, default sort
order, permitted item rotation, transfer interval (60-500 ms), combine stacks
before sorting. Defaults work without AnyHelpers. Only native external-storage
callbacks show the toolbar; player-only inventory never displays these buttons.

AnyInventory's native root shift automatically includes this toolbar because
it is inside the same native hierarchy. Without AnyInventory, game layout is
unchanged apart from the storage's additional rows.

## Validation

Native transfers, matching stacks, top-first placement, sorting and tooltip behavior
are covered by automated fixtures. The toolbar and hover presentation were confirmed
working in host gameplay. Joining-client operations remain a separate validation
requirement. See [Inventory UI](../api/inventory-ui.md) for the versioned contract.
