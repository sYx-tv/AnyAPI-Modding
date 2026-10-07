# AnyAPI 0.32.0 and AnyGraphics 0.29.1

Sun shafts and local-light beams now use scattering independent of the Volumetric fog density. With added fog Off, beams no longer dim the whole scene. Fog alone controls added ambient haze and extinction. Headlight brightness, native shadow occlusion, depth limits and rendering before the HUD are retained.

Update AnyAPI and AnyGraphics in the manager, then restart the game. For clearer air, set Volumetric fog to Off and keep Sun shafts or Local light beams enabled. Native base fog is a separate setting.

All 47 automated checks passed, including GPU regressions for fog-independent sun beams and unchanged shadowed scenery. Visual retesting in a native world remains pending; publication was requested by the user. AnyBalance remains experimental and is not included in this release.
