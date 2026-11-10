# AnyVehicleOverhaul 2.1.0

- **Race and drag wheels** (new `avowheels` piece): Race Wheel (1.5x grip), Drag Slick (2.5x grip) and Drag Front
  Runner (stock grip), each with a 5-spoke, 6-spoke or mesh rim: 9 tyre items that fit the stock wheel and tyre mount
  parts. Loot: tier 3 (race, front runner) and tier 4 (slick).
- **Mirrored race headers** (`header_1_1_m` to `header_6_1_m`) for the other bank of a V engine.
- **Co-op Dyno and ECU:** joiners now see the Dyno graph and the ECU panel, and their ECU changes reach the host.
  Everyone in the world needs this version.
- **Fixes:** the twin screw no longer stalls the engine when you tip in from idle (its bypass stays open below
  1,000 RPM and its belt load is capped); a twin screw log line every 2 s for stall reports.

Publishing: upload `release.json` from [anyvehicleoverhaul-2.1.0/](anyvehicleoverhaul-2.1.0/release.json) with the
ZIP. MinimumApi stays 49 (AnyAPI 0.42.1).
