# AnyAPI 0.35.2 for Anymaker 0.1.24

AnyAPI 0.35.2 fixes a crash on Anymaker 0.1.24 (Steam build 25826614): opening a storage
container's inventory closed the game with `EXCEPTION_ACCESS_VIOLATION` in
`client_ui._push_element<frontend_ui_inventory.ui_container_title>`. It affects AnyAPI 0.35.0 and 0.35.1.

When AnyAPI was ported to 0.1.24, the inventory title function moved one call slot, and the
check followed it but the hook did not. AnyAPI hooked the helper next to it and called the
title function with the wrong arguments. 0.35.2 hooks the slot it checked and refuses to hook
a slot that leads anywhere else. Nothing else changes.

Update AnyAPI in the manager, then restart the game.
