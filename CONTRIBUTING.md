# Contributing

Thanks for helping with AnyAPI and its mods. This page covers how changes get
made and what has to be checked before anything reaches players.

## Ground rules

- **The stable catalog only carries released, validated packages.** Never point
  `catalog.json` at a development or candidate build. Upload release assets first,
  then change the catalog ([Publishing](docs/development/publishing.md)).
- **Never bump a game fingerprint without reviewing the hooks.** A new Anymaker
  build needs its native bodies, dependencies and layouts reviewed against
  [`BUILD_MANIFEST.json`](native/BUILD_MANIFEST.json). Changing a hash alone does
  not make a build compatible.
- **Service tables are ABI.** Do not change the layout of an existing `*_vN.h`
  header in `sdk/include`. Add fields only in a new version (`*_vN+1.h`) and keep
  the old version working.
- **Copies, not pointers.** Public services return copied data and owned tokens.
  Do not expose game-owned pointers through the supported API; that is what the
  [experimental SDK](sdk/experimental/README.md) is for.
- **Say what was tested.** "Submitted" is not "server confirmed", fixtures are not
  gameplay, and local host is not multiplayer. Write docs and release notes that
  way ([Validation](docs/development/validation.md)).
- Never commit game files, generated road caches, credentials or saved settings.

## Making a change

1. Branch from `main`, make the change and open a pull request.
2. Run the checks that fit the change:

   | Change | Check |
   | --- | --- |
   | Docs, catalog, manager feed | `python .github/scripts/check_repo.py` (also runs in CI) |
   | Public headers or examples | CI syntax-checks every file in `native/examples` and the tutorial mod |
   | Native code or mods | Full Windows build and `ctest` ([Building](docs/development/building.md)); the suite has 51 checks today |
   | Gameplay behavior | Test in a world on the pinned game build and note what you saw |
   | Manager | `powershell -File manager/build.ps1`, then the EXE's `--self-test` |
   | Game update | `python native/tools/game_update.py` ([Porting to a new game build](docs/development/game-updates.md)), then the full build, `ctest` and a gameplay test |

3. Update the docs in the same pull request. Each mod has a player guide in
   `docs/mods/` and each service a contract page in `docs/api/`.

Native builds and gameplay tests only run on Windows with the matching game
installed, so CI cannot cover them. Say in the pull request which of them you ran.

## Writing docs

- Lead with what the reader does or gets, then the details.
- Put the version a feature arrived in next to it, for example "(0.31.0)".
- Keep [`catalog.json`](catalog.json) as the source of truth for current versions
  and link to it rather than repeating numbers in many places.
- Historical release notes in `docs/releases/` are records; do not rewrite them.
- Save files as UTF-8. CI rejects text that was mis-decoded as Windows-1252,
  such as an arrow `→` that turned into three accented characters.

## Reporting problems

Use the issue templates. For a crash or a mod not loading, attach
`anymaker_modding.log` from the game folder (beside `game.exe`) and say which
API and mod versions the manager shows.

## License

By contributing, you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
