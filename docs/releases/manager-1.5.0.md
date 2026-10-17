# AnyAPI Manager 1.5.0

Gets ready for the mod bundles in AnyAPI 0.39.0. Update the manager before installing them.

- **Bundles replace the mods they carry.** A catalog entry can list the mods it replaces (`Replaces`). Installing it deletes their DLLs, keeping a backup, and leaves their settings in place. A replaced mod still in the mods folder shows the bundle as "Update / different build", so Update everything picks it up.
- **Steadier self-update.** The updater now reads the old manager's path straight from the process, so an update no longer stops at random with "The update parent is not this manager".
- The offline developer guide matches the manager's bundled API revision again, and it covers the bundle header.

Managers from 1.4.0 offer this update at launch.
