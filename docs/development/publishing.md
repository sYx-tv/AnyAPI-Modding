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

### Release tags in use

Each release gets its own tag, and each catalog entry points at the tag it was
published under: `vX.Y.Z` for API releases (with any mods released alongside),
`mods-YYYY.MM.DD[.N]` for mod-only updates, `manager-vX.Y.Z` for the manager and
`sdk-reference-YYYY.MM.DD` for research downloads. Release tags and their assets
are immutable once a catalog entry points at them; publish a new tag instead of
replacing an asset.

### The publish workflow is out of date

`manager/publish.ps1` and the manually triggered **Publish verified AnyAPI
packages** workflow (`.github/workflows/publish.yml`) come from the first
releases. They upload every package under one tag (default `v0.25.0`) and rewrite
every catalog URL to that tag, and they require each package to be present in
`manager/publishing/assets/`. That folder holds the 0.25.0 to 0.31.0 packages but
not API 0.32.0, AnyMap 0.27.2 or AnyGraphics 0.29.3, so a run today stops at
"Missing release package" before uploading anything. Recent releases did not
use it: their catalog URLs point at per-release tags. Do not run the workflow until it is
updated for per-release tags.

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
