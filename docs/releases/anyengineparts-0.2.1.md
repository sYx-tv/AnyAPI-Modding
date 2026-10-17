# AnyEngineParts 0.2.1 and GpsPart 1.0.3

**Fix:** starting a world now and then crashed the game. As the game started, Turbo could pick up the wrong game
function for the one that adds part definitions (the one that loads a definitions file by name), and when the world
started the game was handed Turbo's parts as a file name. The Rotary Engine and the Dyno already checked for this;
Turbo and the GPS Sensor now do too. A part that finds the wrong function looks again, and if it never finds the
right one it stays off and says so in the log instead of crashing.

**Loot tiers:** in Survival, the Turbo 3x3 and the small supercharger, wastegate, blow-off valve and air filter still
drop after the 2nd bunker (tech tier 3). Every other AnyEngineParts part now drops after the 3rd bunker (tech tier
4): the Turbo 6x6, the large supercharger, wastegate, blow-off valve and air filter, both intercoolers, the Rotary
Engine and the Dyno. Parts you already have stay yours, and Sandbox and Creative are unchanged.

Parts, settings and saves are otherwise as in AnyEngineParts 0.2.0, and the GPS Sensor is as in GpsPart 1.0.2.
