# AnyAPI 0.35.1 and AnyBalance 1.2.0 for Anymaker 0.1.24

AnyAPI 0.35.1 fixes AnyBalance on Anymaker 0.1.24 (Steam build 25826614). In 0.35.0 the
centre-of-mass overlay never appeared: the Properties tool's hover code changed by one byte
in 0.1.24, and the pattern AnyAPI used to find it matched the microcontroller tool instead.
AnyAPI 0.35.1 finds the Properties tool again. Nothing else changes.

Update AnyAPI in the manager, then restart the game.

This release also contains **AnyBalance 1.2.0**, which adds the creation's size (X by Z,
then height) to its summary card, under the mass. Guide: [AnyBalance](../mods/balance.md).

AnyAPI 0.35.1 also logs why the AnyBalance overlay has nothing to draw (`BALANCE
copy_reason=` and `capture_stage=` lines in `anymaker_modding.log`), which is how this was
found. The game update audit (`native/tools/game_update_audit.py`) now reports a pattern
whose bytes match a different function than in the previous build, so this is caught
before the next release.
