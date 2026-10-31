# AnyEngineParts 1.0.6

AnyEngineParts bundle 1.0.6 (Turbo, Rotary and EngineSound 0.4.3): fixes a crash on Anymaker 0.2.0 when a part's
sound stops.

- **Before:** the 0.2.0 port pointed the call that frees a finished sound at the game's set-volume function instead.
  Removing a turbo, supercharger, blow-off valve or rotary, or any of their sounds or an EngineSound engine sound going
  quiet, could crash the game (null read in the client vehicle tick) or leave the sound's handle unreleased.
- **Now:** it calls the game's own release again (`effect_3d.reset`, game.exe 0x13f980 on 0.2.0).
- The other pieces are unchanged.
