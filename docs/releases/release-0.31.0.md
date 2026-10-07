# AnyAPI 0.31.0 and AnyQuickWheel 1.1.0

For Anymaker 0.1.23, Steam build 25755694, Windows x64.

AnyQuickWheel adds an automatic construction-tool wheel. Hold **Q**, point at a tool and release to equip it. Tools come from your carried inventory, including backpack contents. Picking up or dropping tools changes the available slices; duplicate copies share a slice. Weapons, clothing, torches and physical parts are excluded. More than twelve tools use mouse-wheel pages.

Tools stay in their containers. An unassigned tool uses one empty hotbar slot, which the wheel reuses for later selections. Existing manual hotbar assignments are preserved. Configure the binding in **Mod Controls → AnyQuickWheel** and the wheel's size, opacity, centre radius and icons in **Mod Settings → AnyQuickWheel**. Menus and focus loss cancel the wheel.

AnyAPI adds `anyapi.equipment` v1/v2 for copied hotbar/tool snapshots and queued native equipment selection. Inventory assignments use the game's replicated hotbar event and wait for the matching item to appear before selecting it. Existing API services remain supported.

Update the API to **0.31.0**, then install **AnyQuickWheel 1.1.0** from the manager's mod browser. Manager **1.3.1** discovers the new package through the catalog; no manager executable update is required. Other mod packages remain unchanged.

All **47 native checks** passed. The author confirmed the inventory-driven wheel and equipping in a loaded world and approved this release. The optional native HUD control-hint provider is included, but its wheel hint did not appear in the author's live test. It is documented as experimental and is not required to use the wheel.
