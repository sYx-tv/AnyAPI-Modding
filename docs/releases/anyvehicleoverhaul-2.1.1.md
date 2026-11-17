# AnyVehicleOverhaul 2.1.1

- **Knock fix:** a turbo engine with no intercooler no longer cooks itself idling. Knock damage now scales with the
  throttle (none at 20% or under, about a tenth at 45%, all of it flat out), and knocking under 5% past the engine's
  rating does no damage, so an intercooled engine on its built-in wastegate spring is safe. Flat out on hot boost air
  still blows an engine.

Publishing: upload `release.json` from [anyvehicleoverhaul-2.1.1/](anyvehicleoverhaul-2.1.1/release.json) with the
ZIP. MinimumApi stays 49 (AnyAPI 0.42.1).
