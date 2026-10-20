# AnyHelpers 1.0.1

AnyHelpers 1.0.1: fixes a crash when pasting a creation from the Duplicate tool's Favorites tab, and the Workshop tab
not opening from Favorites.

The crash was a divide by zero in the game's paste screen (`frontend_ui_duplicate.update_ui_duplicate_paste`) after the
Favorites tab had swapped the list. Everything else is as in AnyHelpers bundle 1.0.0. For Anymaker 0.1.25 with AnyAPI 0.39.0
or newer. See [AnyHelpers](../mods/helpers.md#duplicate-tool-favorites).
