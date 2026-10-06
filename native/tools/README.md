# Native inspection tools

These utilities support reviewed native contracts. The normal build needs only
`native/generate_road_graph.py`; see [Building](../../docs/development/building.md).

| Tool | Purpose | Required inputs |
| --- | --- | --- |
| `parse_gcl.py` | Parse native code records | Compatible game.gcl and output path |
| `inspect_native.py` | Decode selected native bodies | Game data and parsed records |
| `verify_menu_contracts.py` | Check menu dependency patterns | Game data and parsed records |
| `verify_inventory_contracts.py` | Check inventory layouts and patterns | Game data and parsed records |
| `verify_player_math.py` | Check reviewed camera/player math | Game data and reviewed contract records |
| `audit_current_build.py` | Gather build evidence | Framework, executable, game data and records |
| `review_log.py` | Summarize runtime logs | Log and output path |
| `review_network_evidence.py` | Review captured network observations | Logs and a matching build identity |
| `build_manual.py` | Reconcile historical API audits | Historical audit/coverage datasets |
| `package_source.py` | Create a source-only archive | Destination ZIP path |

Some inspection tools require Python Capstone and locally generated audit datasets.
Those datasets and proprietary game files are not included. Historical audit tools
are reference utilities, not automated compatibility repair or active API services.
Pass explicit inputs where supported and inspect each script's argument declarations
before running it. None of these checks substitutes for gameplay validation.
