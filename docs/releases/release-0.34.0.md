# AnyAPI 0.34.0 and AnyBalance 1.1.0

AnyBalance now counts the fluid in your tanks. The game simulates tank contents as a separate weight tied to each tank; AnyBalance adds that weight and its live position to the centre of mass and the total. When fluid is counted, the summary reads **Creation mass ... kg incl. fluid**. Filling a tank moves the marker toward it, and draining it moves the marker back.

Update AnyAPI and AnyBalance in the manager, then restart the game. Fluid mass works in locally hosted worlds; on a remote server AnyBalance shows the previous reading without fluid. AnyBalance 1.1.0 still runs on API 0.33.0, without fluid.

All 51 native checks passed on Windows, including new fixtures for fluid sampling and stale-sample rejection. The author confirmed in game that fluid mass and marker movement behave as expected. Cargo in storage does not add physics mass in the game, so it is not included.
