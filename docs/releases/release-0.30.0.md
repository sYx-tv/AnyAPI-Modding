# AnyAPI 0.30.0 and AnyClock 1.0.0

For Anymaker 0.1.23, Steam build 25755694, Windows x64.

AnyClock adds a temporary native game-time HUD. Press **O** to show the current game time in the top-right corner for four seconds. Press again to restart the timer. Menus, inventory and loss of game focus dismiss it immediately.

With AnyHelpers installed, configure the key in **Mod Controls → AnyClock**. In **Mod Settings → AnyClock**, change duration, corner/custom position, size, opacity, 12/24-hour format and optional seconds. Default behaviour works without AnyHelpers.

AnyAPI adds `anyapi.world_time` v1: a copied snapshot of the native day/night cycle, including Sandbox and replicated time overrides. It exposes no world-time setter, calendar or native pointers. Stale/unavailable samples are explicitly reported. The HUD draws through the existing GPU service with no external window.

Update the API to **0.30.0** before installing **AnyClock 1.0.0**. AnyGraphics stays at **0.29.0**; AnyHelpers, AnyInventory, AnyStorage and AnyMap stay at **0.27.0**. Existing packages and saved settings remain supported. Manager **1.3.1** discovers the new mod through the public catalog; no manager EXE update is required. Its offline bundled API remains 0.27.0.

All **42 native checks** passed. The author confirmed the clock in a loaded world and approved publication. Automated cases cover stale samples, clock normalization, binding changes, repeat suppression, popup expiry, menu/focus dismissal, placement and time formats. Future game builds still require native contract review.
