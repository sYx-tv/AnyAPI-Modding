"""Verify checked-in native menu patterns against a pinned GCL and parser output.

Usage: python tools/parse_gcl.py <game.gcl> <records.json>
       python tools/verify_menu_contracts.py <game.gcl> <records.json>

Read-only: this tool never installs hooks, updates DLLs, or changes game files.
New builds require an explicit native-contract review before patterns are changed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("game_gcl", type=Path)
parser.add_argument("records", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
contracts = json.loads((root / "MENU_NATIVE_CONTRACTS.json").read_text())
patterns = (root / "anyapi_menu_patterns.h").read_text()
blob = args.game_gcl.read_bytes()
assert hashlib.sha256(blob).hexdigest() == contracts["game_gcl_sha256"], "Unmatched game build"
records = {record["index"]: record for record in json.loads(args.records.read_text())}
for name, contract in contracts["functions"].items():
    record = records[contract["index"]]
    assert record["signature"] == contract["signature"], name
    assert record["code_size"] == contract["code_size"], name
    body = blob[record["body_offset"]:record["body_offset"] + record["code_size"]]
    assert hashlib.sha256(body).hexdigest() == contract["sha256"], name
    match = re.search(r"MENU_" + re.escape(name) + r"\[\]\s*=\s*\{([^}]+)\}", patterns)
    assert match, name
    expected = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match[1]))
    assert body == expected, name
    dependencies = {hex(record["code_size"] + i * 8): dep[1]
                    for i, dep in enumerate(record["dependencies"])}
    assert dependencies == contract["dependencies"], name
update = records[contracts["functions"]["UPDATE"]["index"]]
state = int.from_bytes(blob[update["body_offset"] + 0xe78:update["body_offset"] + 0xe7c], "little")
assert state == contracts["options_state"] == 11
print(f"Verified {len(contracts['functions'])} exact native menu bodies, dependency identities and Options state.")
