# AnyAPI Manager 1.5.1

Fixes a rollback some players could hit right after AnyAPI 0.39.0 was published.

- **No more rolling back to an old library.** Right after a release, GitHub's file cache can still serve the previous mod library for a few minutes. The manager now asks for a fresh copy each time. If the copy it gets is older than the one it already has, it keeps the newer one and says so.
- **No more downgrading AnyAPI.** If the installed AnyAPI is newer than the one the library lists (installed by hand, or from a release the library hasn't caught up with), Install API and Update everything leave it alone. Before, the manager offered to "update" it to the older release and to switch off every other mod while doing so.
- **Bundled mods stay bundled.** A mod that a bundle now carries (for example EngineSound inside AnyEngineParts) can't be installed again on its own while the bundle is installed. Switch it on or off in Mod Settings instead.

If your mods were rolled back, open the manager and choose Update everything. It installs AnyAPI 0.39.0 and the four bundles and removes the old separate mods. Then re-enable AnyStorage and AnyQuickWheel if they show as Disabled.
