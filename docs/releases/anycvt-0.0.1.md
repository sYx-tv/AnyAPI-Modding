# AnyCVT 0.0.1

New standalone mod: a Small, Medium and Large belt CVT, one for each engine size.

- Each one is the stock gearbox of its size, with the same case, engine face, ports and weight, so it bolts onto the
  engine where the stock gearbox goes. The medium and large get the small gearbox's shifter connector on top.
- A lever on the shifter steps **R, N, D**. In D the ratio slides between the low and high ratio to hold the engine at
  the target RPM, and a centrifugal clutch lets the engine idle with the vehicle stopped.
- Properties: Low ratio (24), High ratio (1), Target RPM (6,800), Response (5) and Engage RPM (1,500), saved with the
  part.
- Microcontroller outputs: `ratio`, `engine_rpm`, `target_rpm`, `selector`, `clutch_factor` and `current_gear`
  (-1 R, 0 N, 1 D, like the stock gearbox).
- Loot: tech tier 2, 3 and 4 (small, medium, large), spawn chance 0.15, 0.15 and 0.075.

A tester drove the Small CVT on 2026-10-29. The medium and large were not tried in game before publishing, at Nate's
request. For Anymaker 0.1.28 with AnyAPI 0.41.0 or newer. See [AnyCVT](../mods/cvt.md).
