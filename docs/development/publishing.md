# Publishing mods and API updates

The official library is [sYx-tv/AnyAPI-Modding](https://github.com/sYx-tv/AnyAPI-Modding).
Players browse and download through HTTPS without a GitHub account. Publishing
requires repository write access.

Every release goes through the same three steps:

1. Build, test and package on Windows against the pinned game build.
2. Upload the packages to a **draft** GitHub release under a new tag. Nothing is
   public yet.
3. Run the **Publish a release** workflow for that tag. It checks the packages,
   publishes the release, downloads every asset anonymously to confirm it, and
   then updates root `catalog.json` (or `manager-update.json`) on `main`.

The catalog only ever changes after its downloads are public and verified, so
clients never receive a broken link.

## Release tags

Each release gets its own tag, and each catalog entry points at the tag it was
published under. Entries for packages that are not in a release keep their
existing URLs.

| Tag | Contains |
|---|---|
| `vX.Y.Z` | `AnyAPI-X.Y.Z.zip`, plus any mods released alongside it |
| `mods-YYYY.MM.DD[.N]` | Mod ZIPs only. Add `.1`, `.2` for a second release on the same day |
| `manager-vX.Y.Z` | `AnyAPI.Manager.exe` only |
| `sdk-reference-YYYY.MM.DD` | Research downloads. Not in the catalog; publish these by hand |

Release tags and their assets are immutable once a catalog entry points at them.
Publish a new tag instead of replacing an asset.

## 1. Build and package

1. Build and validate the Windows x64 DLLs against their required API and game
   build (see [building](building.md) and [validation](validation.md)).
2. Create one ZIP per package, named `<Name>-<Version>.zip` exactly as the
   catalog `Name` is spelled (for example `AnyClock-1.1.0.zip`):
   - an API ZIP contains exactly `dinput8.dll`;
   - a mod ZIP contains exactly one DLL, `AnyAPI and Modding/mods/<Name>.dll`,
     with no folder entries, settings or other files.
3. Write the release notes. Earlier notes are in [docs/releases](../releases/README.md).

See [Catalog format](../../manager/CATALOG_FORMAT.md) for the exact schema.

### Catalog details (`release.json`)

A package that is already in the catalog needs nothing extra: it keeps its `Id`,
`Description`, `MinimumApi` and `GameBuilds`, and its `Revision` goes up by one.
To change any of those, or to add a new mod, upload a `release.json` beside the
ZIPs:

```json
{
  "Packages": {
    "AnyAPI": { "Description": "Native mod framework with ..." },
    "AnyCompass": { "Id": "anycompass", "MinimumApi": 33,
                    "Description": "Shows a compass strip at the top of the HUD." }
  },
  "Extra": ["AnyAPI-Experimental-SDK.zip"]
}
```

- `Packages` maps a package `Name` to any of `Id`, `Revision`, `MinimumApi`,
  `Description` and `GameBuilds`. A new mod must give `Id`, `MinimumApi` and
  `Description`; its `Revision` starts at 1 and its `GameBuilds` default to the
  API's. Version, URL and hashes always come from the ZIP and the tag.
- `Extra` lists any other download in the release that is not a catalog
  package. Source archives named `<Name>-<Version>-source.zip` are allowed
  without listing them.
- The API's description usually changes with each API release, so set it here.

The workflow removes `release.json` from the release before publishing it.

## 2. Create the draft release

With [GitHub CLI](https://cli.github.com/) signed in:

```powershell
gh release create v0.34.0 --draft --title "AnyAPI 0.34.0 and AnyClock 1.1.0" `
  --notes-file notes.md AnyAPI-0.34.0.zip AnyClock-1.1.0.zip release.json
```

Or use **Releases > Draft a new release** on GitHub, enter the new tag, attach
the files and choose **Save draft**.

## 3. Run the workflow

```powershell
gh workflow run publish.yml -f tag=v0.34.0                 # check only
gh workflow run publish.yml -f tag=v0.34.0 -f dry_run=false # publish
```

Or open **Actions > Publish a release > Run workflow**. `dry_run` is on by
default: the run checks the draft and shows the catalog change in its summary
without publishing anything. Run it again with `dry_run` off to publish.

The workflow stops before publishing when any package fails a check:

- the tag does not match its contents (an API ZIP under a `mods-` tag, or a
  `vX.Y.Z` tag without `AnyAPI-X.Y.Z.zip`);
- a ZIP holds anything other than its one allowed path, or the DLL is not an
  x64 DLL, or it is larger than 64 MiB;
- a version is not newer than the catalog's, a mod changes its archive path or
  `Id`, or a mod needs a newer API revision than the catalog lists;
- an asset is neither a package nor listed in `Extra`;
- the updated catalog fails the [repository checks](../../.github/scripts/check_repo.py).

If the anonymous download check fails after publishing, the release is public
but the catalog is unchanged. Fix the asset under a new tag. If `main` refuses
the workflow's push, it opens a pull request with the catalog change instead.

The same checks run locally before you upload anything:

```powershell
python .github/scripts/publish_release.py prepare --tag v0.34.0 --assets <folder with the ZIPs>
git diff catalog.json
git checkout catalog.json   # the workflow makes the real change
```

API and mod releases are not marked "latest" on GitHub, so the latest release
stays the manager's.

## Publish an API build

Review native integration for the target game build, run the full suite and
complete the necessary gameplay checks. Package the API, publish it under
`vX.Y.Z` as above, and add `AnyAPI-X.Y.Z-source.zip` if you ship matching
source. The catalog lists one API entry, which the release replaces. Rebuild the
manager separately if its bundled API and offline guide should change.

## Publish a manager update

1. Increase `manager/AssemblyInfo.cs`, build the manager and run its fixture checks.
2. Draft a release tagged `manager-vX.Y.Z`, using the same version as the
   assembly, with the verified EXE attached as exactly `AnyAPI.Manager.exe`.
3. Run the workflow for that tag. It checks that the EXE is an x64 executable
   and newer than the feed, publishes the release as latest, verifies the
   public download's SHA-256 and size, and then updates root
   `manager-update.json` (`Schema`, `Version`, `Url`, `Sha256`, `Size`).
4. Commit the matching manager source and documentation.

Managers from 1.4.0 read this separate feed on every launch and offer the
update in one click; 1.3.x reads it from Settings. Publishing a manager update does not
require changing `catalog.json` or replacing users' API/mod installations. Keep
the bundled API package and guide consistent when rebuilding the EXE; the
manager's embedded catalog is `manager/publishing/catalog.json`, which the
workflow does not change.

## How the manager finds updates

The manager reads the catalog when users refresh or check for updates. New
entries appear without editing or rebuilding the manager. Uploading an unlisted
DLL to GitHub alone does not add a library entry. Handmade DLLs still appear
locally when imported or placed in the game's mods folder. Packages contain
DLLs; saved settings and user data are excluded.
