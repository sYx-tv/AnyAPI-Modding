# AnyAPI Manager 1.2.1 candidate

Recognizes checksum-matched installed API profiles even when the online catalog
has not published that build yet. Play with mods uses the installed profile's
game fingerprints; mismatches still block launching. A newer installed mod is
shown in preference to an older catalog entry instead of appearing as an update
or different build. Newer online releases still take precedence.

The bundled API and offline guide remain the published 0.25.0 profile. This
manager correction does not publish untested 0.27.0 downloads. Native 0.27.0
packages and the matching developer guide are separate compatibility candidates.

Validation includes the complete manager test suite, with regressions for an
installed API absent from the current catalog, a mismatching game fingerprint,
and discovery of a newer installed mod.
