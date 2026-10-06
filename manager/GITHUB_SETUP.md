# Publish without learning Git

The current repository is **https://github.com/sYx-tv/AnyAPI-Modding**, private by the owner's request. Its source, catalog and releases are accessible only to authorized GitHub accounts. No collaborators are added by this setup. Keep the default branch name **main**.

The manager supports private repositories through the user's installed, signed-in GitHub CLI. Friends cannot download this private release unless given repository access. A future public repository would allow downloads without login; changing visibility requires the owner's explicit approval.

Send the repository URL to Codex. The publishing script fills in all download URLs and rebuilds the EXE so friends do not need to enter your repository address.

For the first release:

1. Go to **Releases → Create a new release**. Use tag **v0.25.0** and title **AnyAPI 0.25.0**. Create a README first if GitHub needs an initial main branch.
2. Attach **AnyAPI Manager.exe** and the five prepared ZIPs: AnyAPI, AnyHelpers, AnyInventory, AnyStorage and AnyMap. These are separate files. Publish the release.
3. Upload the prepared **catalog.json** to the repository's top level on main. Use **Add file → Upload files**.
4. Share the EXE's release download. Friends open it, install the API, then choose their mods.

Keep `catalog.json` on main current. The manager reads it whenever users check for updates. New mod entries appear without a new manager release. Existing mods update when their version, archive/file hashes and download URL change. Older verified API releases can remain in the Api array for users on older game builds.

For future API releases, first review the new Steam executable/GCL, rebuild and test the API, then prepare packages with the new fingerprints. Upload versioned ZIPs to a new Release, update the catalog, and rebuild the manager if its bundled API should change. Do not claim compatibility by editing only the version or game hashes.

The optional workflow in `.github/workflows/publish.yml` performs the release upload for a repository containing the manager source and prepared packages. It is manually triggered; it does not download Steam builds or pretend to fix compatibility automatically.

Developer command after the repository exists:

```powershell
powershell -File manager/publish.ps1 -Repository your-name/AnyAPI -Tag v0.25.0
```

Prepared upload files appear in `manager/ready-to-upload`. The catalog is published **after** the release assets exist so users never see links to missing downloads.

Official references: [upload files](https://docs.github.com/en/repositories/working-with-files/managing-files/adding-a-file-to-a-repository), [create releases](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository).
