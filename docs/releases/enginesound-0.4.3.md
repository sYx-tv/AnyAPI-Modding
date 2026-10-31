# EngineSound 0.4.3

**No crash when an engine sound stops (Anymaker 0.2.0).** The 0.2.0 port pointed the call that frees a finished
sound at the game's set-volume function, which could crash the game when an engine's sound was released. It calls the
game's own release again.
