# Publishing mods and API updates

The official library is [sYx-tv/AnyAPI-Modding](https://github.com/sYx-tv/AnyAPI-Modding).
Players browse and download through HTTPS without a GitHub account. Publishing
requires repository write access.

## Publish a mod

1. Build and validate the Windows x64 DLL against its required API and game build.
2. Create a ZIP containing its allowed installation path under
   `AnyAPI and Modding/mods`.
3. Upload the immutable ZIP to a versioned GitHub release.
4. Add or update its entry in root `catalog.json`: ID, display name, version,
   minimum API, archive/file hashes, download URL and verified game fingerprints.
5. Verify the anonymous download and checksums before publishing the catalog entry.

The manager reads the catalog when users refresh or check for updates. New entries
appear without editing or rebuilding the manager. Uploading an unlisted DLL to
GitHub alone does not add a library entry. Handmade DLLs still appear locally when
imported or placed in the game's mods folder.

See [Catalog format](../../manager/CATALOG_FORMAT.md) for the exact schema.
Packages contain DLLs; saved settings and user data are excluded.

## Publish an API build

Review native integration for the target game build, run the full suite and complete
the necessary gameplay checks. Create an API-only package and matching source/docs.
Upload assets before updating the catalog so clients never receive broken links.
Rebuild the manager separately if its bundled API and offline guide should change.

`manager/publish.ps1` and the manually triggered GitHub workflow publish prepared,
verified resources. They do not download Steam builds or repair hooks automatically.
The currently prepared resources are the stable 0.25.0 packages; replacing them
with a new profile requires matching fingerprints, evidence, hashes and guide data.

## Publish a manager update

1. Increase `manager/AssemblyInfo.cs`, build the manager and run its fixture checks.
2. Upload the verified EXE as `AnyAPI.Manager.exe` to a versioned release such as
   `manager-v1.3.0`. Use the same version in the assembly and release feed.
3. Verify the public download's SHA-256 and size against the built EXE.
4. Update root `manager-update.json` only after the release asset is available.
   Its fields are `Schema` (1), `Version`, `Url`, `Sha256` and `Size` in bytes.
   The URL must be an HTTPS release download in this repository with the exact
   asset name `AnyAPI.Manager.exe`.
5. Commit and push the feed and matching source/documentation. Verify the raw
   public feed and mark the manager release latest for the direct download link.

Settings reads this separate feed on demand. Publishing a manager update does not
require changing `catalog.json` or replacing users' API/mod installations. Keep
the bundled API package and guide consistent when rebuilding the EXE.
