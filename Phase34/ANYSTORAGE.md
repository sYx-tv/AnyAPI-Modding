# AnyStorage

Revision 23 fixes activation of the centered toolbar by following the native
tooltip text overload dependency chain, rather than selecting identical text
and element-constructor bodies. The startup resolver has a dependency-cell
regression check. In-game activation is checked separately from widget tests.

Revision 22 centers symbols using the native tag-button widget, so `A-Z` stays
on one line. Short hover text appears beside the mouse in a small translucent
dark native popup, clamped inside the game window. Leaving the button, losing
focus, or closing inventory clears the popup. The tooltip uses native text and
background widgets rather than a solid-white inventory tooltip entry.
Five focused storage/UI regression checks passed; in-game appearance awaits testing.

Revision 21 replaces wrapping text buttons with two compact native rows.
`<-` withdraws, `->` deposits, `<<` and `>>` stack matching types, `S` sorts,
and `A-Z` / `#` switches the sort order. Hover for a full explanation.
A small `i` button holds the latest result or progress; `X` cancels an active job.
Details appear in a native tooltip screen and consume no storage grid rows.
This revision requires inventory UI v3; v1 and v2 remain available to other mods.

Revision 20 corrects the inventory layout resolver introduced in revision 19.
The earlier resolver incorrectly searched the settings/give owner, preventing
toolbar activation; it now uses the exact inventory update body (record 26366).
The compiled resolver pattern is verified against the installed game assets.

Revision 19 hides all storage-tool rows on clothing, weapons, pouches, torches,
angled torches and signal detectors. Backpacks and other storage remain eligible.
Native reserved column space keeps toolbars from overlapping adjacent containers.
Transfers and matching transfers fill compatible existing stacks from the top,
then place remaining items in the first free top-left location. Alphabetical and
quantity sorting pack from top to bottom. Equipped roots remain excluded from
bulk transfers; API filters and fit/rotation checks remain authoritative.
Requires AnyAPI revision 19's optional inventory UI v2 service.

Revision 18 fixes missing crate buttons caused by a misidentified native vector
count field. The correction also applies to copied item/grid lists and the
destination vector sent with native Deposit events. Native byte-layout and real
container-header dispatch are now covered by the regression fixture. The user confirmed the native crate buttons after this correction.

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

Validation: 26 automated checks passed, including three new storage checks.
The actual mod DLL is tested for transfer directions, matching-only behavior,
stack filling, replicated sort, cancellation, timeout/no duplicates and storage
changes. The host fixture exercises native collection copies, current scalar
getter bodies, item/grid layouts, namespaces, variant identity, source/target
IDs and menu gating. Current GCL bodies and dependencies are reviewed and pinned.
Revision 19 in-game layout and top-first transfer/sort confirmation remains pending.


Revision 24 fixes the native hover popup layout. A live native UI-tree read
confirmed the hover string and text widget existed, but the popup's grid origin
was (29,38), outside its 32-row screen. Vertical content alignment 0 added a
31-row offset to the positioned child. Alignment 2 retains bottom-origin grid
coordinates. Mouse conversion now uses square cells scaled by client height,
inverts Windows' top-origin Y, and uses the viewport width instead of the fixed
42-cell inventory content width. The popup stays beside the cursor, clamps to
screen edges, consumes no inventory rows, and clears when hover ends.
Regression checks cover alignment, landscape/ultrawide coordinates, invalid
mouse positions, edge clamping, and one-frame hover text consumption.
Visual acceptance of revision 24 remains pending an in-game hover test.
