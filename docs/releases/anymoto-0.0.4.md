# AnyMoto 0.0.4

Fixes a crash on Anymaker 0.2.0 when you look at (or sit on) the Motorcycle Seat.

- **Before:** 0.2.0 gives every seat part a code-side seat definition (where the rider sits), picked by the part's
  id. It has none for `moto_seat`, so the seat's pointer stayed empty and the game read through it as soon as it
  asked for the seat position: the HUD's sit hint did that the moment you looked at the seat (crash report
  2026-11-02: null read in `seat.get_seat_position`, called from `seat.get_context_available_bindings`).
- **Now:** after the game's own pass, the Motorcycle Seat gets the stock seat's definition on the host and on
  every client. That puts the rider at (0, 5, 0) cells, the same spot the seat used before 0.2.0.
- AnyMoto stays off (and logs why) if it cannot find the two seat functions or the stock seat definition, rather
  than adding a seat that would crash.
- Hooks `vehicle_element_definition.seat.assign_definition` on both sides (names stripped in 0.2.0:
  `#d091751d315791f1` server, `#2e96716ef41de931` client). The other parts are unchanged.
