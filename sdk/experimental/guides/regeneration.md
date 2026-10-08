# Regenerating the SDK (Windows)

## Dependencies

| Tool | Version used | For |
|---|---|---|
| Python | 3.12.3 | every generator |
| capstone | 5.0.7 | disassembly (`pip install capstone`) |
| pefile | 2024.8.26 | game.exe parsing |
| numpy | 2.5.3 | fast native string cross-reference scan |
| Visual Studio 2026 (C++ x64 tools) | MSVC 14.51 | `validate_compile.py`, `validation/calltest/build.cmd` |

```bat
python -m venv .venv
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

## One command

```bat
.venv\Scripts\python tools\regenerate.py --game-dir "C:\Program Files (x86)\Steam\steamapps\common\Anymaker" --out .
```

`regenerate.py` runs these steps in order. Each step can also run alone with explicit `--json` / `--out` paths.

| Step | Command | Output |
|---|---|---|
| 1 analyse | `tools/sdk_analyze.py --game-dir … --out json` | `json/build_identity, types, functions, globals, natives, files, anchors, symbols` |
| 2 ABI facts | `tools/abi_facts.py --gcl … --json json` | `json/abi_facts.json` |
| 3 merge runtime | `validation/merge_runtime.py --json json --runtime validation/runtime_*.json` | runtime blocks in `natives.json`, `native_bindings.json`, `runtime_located.json`, `runtime_evidence.json` (only results whose process hashes match) |
| 4 header | `tools/sdk_header.py --json json --out include/anymaker_sdk_types.hpp` | packed layouts with static_asserts |
| 5 symbols | `tools/sdk_codegen.py --json json --out include/anymaker_sdk_symbols.hpp` | per-build routes, RVAs, offsets, enums |
| 6 systems | `tools/sdk_systems.py --json json --docs docs` | `json/systems.json`, `json/capabilities.json`, `docs/systems/*`, `docs/capability_matrix.md` |
| 7 reference | `tools/sdk_docs.py --json json --docs docs` | `docs/reference/**` |
| 8 index | `tools/sdk_query.py --json json --db json/sdk_index.sqlite build` | searchable SQLite with FTS5 |
| 9 compile test | `tools/validate_compile.py --sdk . --out validation/compile_result.json` | header and examples compiled with MSVC |
| 10 diff | `tools/sdk_diff.py --old <previous json> --new json --out reports/diff-<a>-to-<b>` | structured comparison |

## After a game update

1. Copy the old `json/` folder aside (or keep the previous SDK folder).
2. Run `regenerate.py` against the updated install. If step 5 or 6 fails, a curated symbol no longer exists or
   lost its locator route. The error names it. Fix `tools/sdk_codegen.py` / `tools/systems_spec.py` (the update
   changed the API surface).
3. Run `sdk_diff.py` old → new and read `README.md`:
   - `moved_only` natives and `padding_only` / `constants_tail` functions need nothing.
   - `layout_changed`, `vtable_shifted` and `values_renumbered` types break code that hard-codes offsets, slots
     or enum values. The regenerated headers fix code that uses them.
   - `body_changed: instructions` means behaviour may differ. Re-check hooks on those functions.
4. Start the game and run `validation/probe.py --json json --out validation/runtime_<phase>.json --phase menu`
   (and `world-host` while hosting a world). Then re-run step 3 so runtime evidence matches the new build. The
   probe only reads memory.
5. Optional: rebuild and run `validation/calltest` (needs the DLL in `AnyAPI and Modding/mods`).

Inputs are never copied into the SDK. The tools read game.exe and game.gcl from where they are, and builds kept
for comparison live outside `sdk/` (`builds/`, excluded from any package).
