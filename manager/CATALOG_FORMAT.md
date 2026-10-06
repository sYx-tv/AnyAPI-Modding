# Catalog schema 1

`catalog.json` sits at the repository root on `main`. The manager accepts a GitHub repository URL or `owner/repository` in Settings. Its request is `https://raw.githubusercontent.com/owner/repository/main/catalog.json`. Downloads point to versioned GitHub Release ZIP assets. No sign-in is required for a public repository.

Top-level fields: `Schema` (1), `Repository` (GitHub URL used by a newly built manager), `Api` (verified API releases), `Mods` (one entry per browsable mod).

Each package has `Id`, `Name`, numeric dotted `Version`, `Revision`, `MinimumApi`, short `Description`, HTTPS `Url`, ZIP `Sha256`, `FileHashes`, and `GameBuilds`. A game build has `Version`, `SteamBuild`, `ExeSha256`, `GclSha256`. Both game hashes must match before installation. A mod requires at least `MinimumApi` and a verified game match.

An API ZIP contains exactly `dinput8.dll`. A mod ZIP contains exactly one `AnyAPI and Modding/mods/<Filename>.dll`. `FileHashes` maps that exact archive path to its SHA-256. Assets, configuration migrations and multi-DLL packages are not supported in schema 1. Their support should be introduced explicitly in a later schema; never include user settings in a package.

Limits: 32 API releases, 256 mod entries, 32 verified game fingerprints per package, 2 MiB catalog, 64 MiB download/expanded DLL. Archives are rejected for undeclared/duplicate paths, wrong checksums, wrong executable architecture, executable rather than DLL content, linked destination paths, or a running game. Versions cannot downgrade a newer managed installation.

The current four mods are independent. AnyHelpers provides editable settings/controls, but the other mods retain their own defaults if it is absent. There is no forced mod dependency or bundled-mod install.

Checksums protect integrity relative to the chosen catalog. The repository is the user's trust source; these checksums are not a separate publisher signature. Configure a repository you trust. The manager does not run install scripts or fetch arbitrary executable installers.

To add a mod: package the tested x64 DLL under its exact loader path, calculate archive and DLL hashes, add its verified game fingerprints and minimum API revision, upload the ZIP as a release asset, then add one entry under Mods. The browser discovers it on refresh.
