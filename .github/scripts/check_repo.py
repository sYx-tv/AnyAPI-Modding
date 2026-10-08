"""Repository consistency checks that run without Windows or game files.

- Relative Markdown links and image targets must exist.
- Markdown and source text must not contain UTF-8 text that was decoded as Windows-1252.
- catalog.json and manager-update.json must match the manager's schema rules.
"""
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SKIP = {".git", "build", "generated", "__pycache__", "third_party"}
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
# Typical sequences left behind when UTF-8 is read as cp1252 (for example "â†’" for "→").
MOJIBAKE = re.compile("â€|â†|Ã[\u0080-¿]|Â[ -¿]")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
VERSION = re.compile(r"^\d+(\.\d+){1,3}$")
REPO_DOWNLOAD = "https://github.com/sYx-tv/AnyAPI-Modding/releases/download/"

errors = []


def fail(path, message):
    errors.append(f"{path.relative_to(ROOT).as_posix()}: {message}")


def markdown_files():
    for path in ROOT.rglob("*.md"):
        if not SKIP.intersection(path.relative_to(ROOT).parts):
            yield path


def check_markdown(path):
    text = path.read_text(encoding="utf-8")
    in_fence = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if MOJIBAKE.search(line):
            fail(path, f"line {number}: mis-decoded UTF-8 text")
        if in_fence:
            continue
        for target in LINK.findall(line):
            if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
                continue
            relative = unquote(target.split("#", 1)[0])
            if relative and not (path.parent / relative).exists():
                fail(path, f"line {number}: missing link target {target}")


def check_package(path, package, game_required=True):
    name = package.get("Id", "?")
    for field in ("Id", "Name", "Version", "Revision", "MinimumApi", "Description", "Url", "Sha256", "FileHashes", "GameBuilds"):
        if field not in package:
            fail(path, f"{name}: missing {field}")
    if not VERSION.match(str(package.get("Version", ""))):
        fail(path, f"{name}: version is not numeric dotted")
    if not str(package.get("Url", "")).startswith(REPO_DOWNLOAD):
        fail(path, f"{name}: URL is not a release download in this repository")
    if not SHA256.match(str(package.get("Sha256", ""))):
        fail(path, f"{name}: Sha256 is not a lowercase SHA-256")
    hashes = package.get("FileHashes") or {}
    if len(hashes) != 1:
        fail(path, f"{name}: schema 1 packages contain exactly one DLL")
    for file, digest in hashes.items():
        if not SHA256.match(str(digest)):
            fail(path, f"{name}: {file} hash is not a lowercase SHA-256")
    builds = package.get("GameBuilds") or []
    if game_required and not builds:
        fail(path, f"{name}: no verified game builds")
    for build in builds:
        if not (SHA256.match(str(build.get("ExeSha256", ""))) and SHA256.match(str(build.get("GclSha256", "")))):
            fail(path, f"{name}: game build {build.get('Version')} has an invalid fingerprint")
    return hashes


def check_catalog(path):
    catalog = json.loads(path.read_text(encoding="utf-8"))
    if catalog.get("Schema") != 1:
        fail(path, "Schema must be 1")
    apis = catalog.get("Api") or []
    if not apis:
        fail(path, "no API release")
    for api in apis:
        hashes = check_package(path, api)
        if list(hashes) != ["dinput8.dll"]:
            fail(path, f"{api.get('Id')}: an API package contains exactly dinput8.dll")
    newest_api = max((api.get("Revision", 0) for api in apis), default=0)
    seen = set()
    for mod in catalog.get("Mods") or []:
        hashes = check_package(path, mod)
        if mod.get("Id") in seen:
            fail(path, f"{mod.get('Id')}: duplicate mod ID")
        seen.add(mod.get("Id"))
        for file in hashes:
            if not re.match(r"^AnyAPI and Modding/mods/[^/\\]+\.dll$", file):
                fail(path, f"{mod.get('Id')}: {file} is not under AnyAPI and Modding/mods")
        if mod.get("MinimumApi", 0) > newest_api:
            fail(path, f"{mod.get('Id')}: needs API revision {mod.get('MinimumApi')} but the newest listed is {newest_api}")


def check_manager_feed(path):
    feed = json.loads(path.read_text(encoding="utf-8"))
    if feed.get("Schema") != 1:
        fail(path, "Schema must be 1")
    if not VERSION.match(str(feed.get("Version", ""))):
        fail(path, "Version is not numeric dotted")
    if not (str(feed.get("Url", "")).startswith(REPO_DOWNLOAD) and str(feed.get("Url")).endswith("/AnyAPI.Manager.exe")):
        fail(path, "Url must be this repository's AnyAPI.Manager.exe release download")
    if not SHA256.match(str(feed.get("Sha256", ""))):
        fail(path, "Sha256 is not a lowercase SHA-256")
    if not isinstance(feed.get("Size"), int) or feed["Size"] <= 0:
        fail(path, "Size must be a positive byte count")


TEXT_SUFFIXES = {".cs", ".cpp", ".h", ".hpp", ".inc", ".hlsl", ".json", ".py", ".ps1", ".txt"}
for path in ROOT.rglob("*"):
    if path.suffix in TEXT_SUFFIXES and path != Path(__file__).resolve() and path.is_file() \
            and not SKIP.intersection(path.relative_to(ROOT).parts):
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            fail(path, "not valid UTF-8")
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if MOJIBAKE.search(line):
                fail(path, f"line {number}: mis-decoded UTF-8 text")

for markdown in markdown_files():
    check_markdown(markdown)
check_catalog(ROOT / "catalog.json")
check_catalog(ROOT / "manager/publishing/catalog.json")
check_manager_feed(ROOT / "manager-update.json")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("Repository checks passed.")
