#!/usr/bin/env python3
"""Prepare and verify one AnyAPI release from its draft GitHub release assets.

Used by .github/workflows/publish.yml. It also runs locally (Python 3.9+, standard
library only) so a release can be checked before anything is made public.

  prepare --tag TAG --assets DIR [--plan FILE] [--summary FILE] [--root REPO]
      Validates the downloaded draft assets for TAG and updates catalog.json
      (API and mod ZIPs) or manager-update.json (AnyAPI.Manager.exe) in place.
      Only the packages in this release change; every other entry keeps its URL.
  verify --plan FILE
      Downloads every published asset anonymously and checks SHA-256 and size.

Tags: vX.Y.Z (API release, optionally with mods), mods-YYYY.MM.DD[.N] (mods only),
manager-vX.Y.Z (manager EXE only). See docs/development/publishing.md.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = "release.json"
MANAGER_ASSET = "AnyAPI.Manager.exe"
MAX_PAYLOAD = 64 * 1024 * 1024
TAGS = {
    "api": re.compile(r"^v(\d+\.\d+\.\d+)$"),
    "mods": re.compile(r"^mods-\d{4}\.\d{2}\.\d{2}(\.\d+)?$"),
    "manager": re.compile(r"^manager-v(\d+\.\d+\.\d+)$"),
}
PACKAGE = re.compile(r"^([A-Za-z][A-Za-z0-9]{0,99})-(\d+(?:\.\d+){1,3})\.zip$")
SOURCE = re.compile(r"^[A-Za-z][A-Za-z0-9]*-\d+(?:\.\d+){1,3}-source\.zip$")
MOD_PATH = re.compile(r"^AnyAPI and Modding/mods/[A-Za-z0-9_.-]+\.dll$")
MOD_ID = re.compile(r"^[a-z][a-z0-9-]{0,47}$")
OVERRIDABLE = ("Id", "Revision", "MinimumApi", "Description", "GameBuilds")


class ReleaseError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def version_key(text):
    return tuple(int(part) for part in text.split("."))


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    # Repository JSON files use two-space indentation, CRLF and a final newline.
    text = json.dumps(value, indent=2, ensure_ascii=False) + "\n"
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def pe_image(data, what):
    """Return (machine, is_dll) for a PE image, or raise."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ReleaseError(f"{what} is not a Windows executable")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ReleaseError(f"{what} has no PE header")
    machine, = struct.unpack_from("<H", data, offset + 4)
    characteristics, = struct.unpack_from("<H", data, offset + 22)
    if machine != 0x8664:
        raise ReleaseError(f"{what} is not x64 (machine 0x{machine:04x})")
    return bool(characteristics & 0x2000)


def read_package(path, api):
    """Return (archive path, DLL bytes) for a schema 1 package ZIP."""
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        raise ReleaseError(f"{path.name} is not a ZIP file")
    with archive:
        entries = [entry for entry in archive.infolist() if not entry.is_dir()]
        if len(entries) != 1 or len(archive.infolist()) != 1:
            raise ReleaseError(f"{path.name} must contain exactly one file and no folders "
                               f"(found {', '.join(e.filename for e in archive.infolist()) or 'nothing'})")
        entry = entries[0]
        if api and entry.filename != "dinput8.dll":
            raise ReleaseError(f"{path.name} must contain exactly dinput8.dll, not {entry.filename}")
        if not api and not MOD_PATH.match(entry.filename):
            raise ReleaseError(f"{path.name} must contain one AnyAPI and Modding/mods/<Name>.dll, not {entry.filename}")
        if entry.file_size > MAX_PAYLOAD:
            raise ReleaseError(f"{path.name}: {entry.filename} is larger than 64 MiB")
        data = archive.read(entry)
    if not pe_image(data, f"{path.name}: {entry.filename}"):
        raise ReleaseError(f"{path.name}: {entry.filename} is an executable, not a DLL")
    return entry.filename, data


def release_kind(tag):
    for kind, pattern in TAGS.items():
        match = pattern.match(tag)
        if match:
            return kind, (match.group(1) if kind != "mods" else None)
    raise ReleaseError(f"{tag} is not a catalog release tag (vX.Y.Z, mods-YYYY.MM.DD[.N] or manager-vX.Y.Z)")


def download_url(repository, tag, name):
    return f"{repository.rstrip('/')}/releases/download/{tag}/{name}"


def prepare(tag, assets, root=ROOT):
    """Validate the release assets and update the repository files. Returns the plan."""
    kind, tag_version = release_kind(tag)
    files = sorted(path for path in assets.iterdir() if path.is_file())
    manifest_path = assets / MANIFEST
    manifest = read_json(manifest_path) if manifest_path.exists() else {}
    if not isinstance(manifest, dict):
        raise ReleaseError(f"{MANIFEST} must be a JSON object")
    unknown = set(manifest) - {"Packages", "Extra"}
    if unknown:
        raise ReleaseError(f"{MANIFEST}: unknown fields {', '.join(sorted(unknown))}")
    overrides = manifest.get("Packages") or {}
    extra = set(manifest.get("Extra") or [])
    plan = {"Tag": tag, "Kind": kind, "Assets": [], "Changes": []}

    if kind == "manager":
        return prepare_manager(tag, tag_version, files, extra, overrides, plan, root)

    catalog_path = root / "catalog.json"
    catalog = read_json(catalog_path)
    repository = catalog["Repository"]
    packages = []
    for path in files:
        if path.name == MANIFEST:
            continue
        if path.name in extra or SOURCE.match(path.name):
            plan["Assets"].append(asset_record(repository, tag, path))
            continue
        match = PACKAGE.match(path.name)
        if not match:
            raise ReleaseError(f"Unexpected asset {path.name}. Package ZIPs are named <Name>-<Version>.zip; "
                               f"name source archives <Name>-<Version>-source.zip and list any other download under \"Extra\" in {MANIFEST}")
        packages.append((match.group(1), match.group(2), path))
    if not packages:
        raise ReleaseError(f"{tag} has no package ZIPs")

    names = [name for name, _, _ in packages]
    if len(set(names)) != len(names):
        raise ReleaseError("A release can contain only one version of each package")
    unused = set(overrides) - set(names)
    if unused:
        raise ReleaseError(f"{MANIFEST} describes packages that are not in the release: {', '.join(sorted(unused))}")
    has_api = "AnyAPI" in names
    if kind == "api":
        api_version = dict((n, v) for n, v, _ in packages).get("AnyAPI")
        if api_version != tag_version:
            raise ReleaseError(f"{tag} must include AnyAPI-{tag_version}.zip")
    elif has_api:
        raise ReleaseError(f"{tag} is a mods-only tag; publish AnyAPI under vX.Y.Z")

    # Publish the API first so new mods can default to its game builds.
    packages.sort(key=lambda item: item[0] != "AnyAPI")
    for name, version, path in packages:
        api = name == "AnyAPI"
        section = catalog["Api"] if api else catalog["Mods"]
        current = next((entry for entry in section if entry.get("Name") == name), None)
        override = overrides.get(name) or {}
        bad = set(override) - set(OVERRIDABLE)
        if bad:
            raise ReleaseError(f"{MANIFEST}: {name} cannot set {', '.join(sorted(bad))}; those come from the ZIP and tag")
        archive_path, dll = read_package(path, api)
        zip_bytes = path.read_bytes()

        if current:
            if version_key(version) <= version_key(current["Version"]):
                raise ReleaseError(f"{name} {version} is not newer than the catalog's {current['Version']}")
            old_path = next(iter(current["FileHashes"]))
            if archive_path != old_path:
                raise ReleaseError(f"{name}: archive path {archive_path} differs from the catalog's {old_path}")
            entry = dict(current)
            entry["Revision"] = current["Revision"] + 1
        else:
            if api:
                raise ReleaseError("The catalog has no AnyAPI entry to update")
            missing = [field for field in ("Id", "MinimumApi", "Description") if field not in override]
            if missing:
                raise ReleaseError(f"{name} is a new mod; add its {', '.join(missing)} to {MANIFEST}")
            entry = {"Id": None, "Name": name, "Version": None, "Revision": 1, "MinimumApi": None,
                     "Description": None, "Url": None, "Sha256": None, "FileHashes": None,
                     "GameBuilds": catalog["Api"][0]["GameBuilds"]}
        entry.update(override)
        entry["Version"] = version
        entry["Url"] = download_url(repository, tag, path.name)
        entry["Sha256"] = sha256(zip_bytes)
        entry["FileHashes"] = {archive_path: sha256(dll)}
        check_entry(entry, current, api, catalog)

        if current:
            section[section.index(current)] = entry
            plan["Changes"].append(f"{name} {current['Version']} -> {version} (revision {entry['Revision']})")
        else:
            section.append(entry)
            plan["Changes"].append(f"{name} {version} added (revision {entry['Revision']})")
        plan["Assets"].append({"Name": path.name, "Url": entry["Url"], "Sha256": entry["Sha256"],
                               "Size": len(zip_bytes)})

    write_json(catalog_path, catalog)
    plan["Files"] = ["catalog.json"]
    return plan


def check_entry(entry, current, api, catalog):
    name = entry["Name"]
    if not MOD_ID.match(str(entry["Id"])) or (entry["Id"] == "anyapi") != api:
        raise ReleaseError(f"{name}: invalid Id {entry['Id']!r}")
    if not api and current is None and any(mod.get("Id") == entry["Id"] for mod in catalog["Mods"]):
        raise ReleaseError(f"{name}: Id {entry['Id']} is already used by another mod")
    if current and entry["Id"] != current["Id"]:
        raise ReleaseError(f"{name}: Id cannot change from {current['Id']}")
    if not isinstance(entry["Revision"], int) or entry["Revision"] < 1 or (current and entry["Revision"] <= current["Revision"]):
        raise ReleaseError(f"{name}: Revision must increase (catalog has {current['Revision'] if current else 'none'})")
    if not isinstance(entry["MinimumApi"], int) or entry["MinimumApi"] < 0:
        raise ReleaseError(f"{name}: MinimumApi must be a revision number")
    if not api and entry["MinimumApi"] > catalog["Api"][0]["Revision"]:
        raise ReleaseError(f"{name}: needs API revision {entry['MinimumApi']} but the catalog's API is "
                           f"revision {catalog['Api'][0]['Revision']}")
    if not isinstance(entry["Description"], str) or not entry["Description"].strip():
        raise ReleaseError(f"{name}: Description is empty")
    if not isinstance(entry["GameBuilds"], list) or not 1 <= len(entry["GameBuilds"]) <= 32:
        raise ReleaseError(f"{name}: needs 1 to 32 GameBuilds")


def prepare_manager(tag, version, files, extra, overrides, plan, root):
    if overrides:
        raise ReleaseError(f"{tag} is a manager release; it has no catalog packages")
    feed_path = root / "manager-update.json"
    feed = read_json(feed_path)
    repository = read_json(root / "catalog.json")["Repository"]
    exe = None
    for path in files:
        if path.name == MANIFEST:
            continue
        if path.name in extra:
            plan["Assets"].append(asset_record(repository, tag, path))
        elif path.name == MANAGER_ASSET:
            exe = path
        else:
            raise ReleaseError(f"Unexpected asset {path.name} in a manager release")
    if exe is None:
        raise ReleaseError(f"{tag} must include {MANAGER_ASSET}")
    data = exe.read_bytes()
    if pe_image(data, MANAGER_ASSET):
        raise ReleaseError(f"{MANAGER_ASSET} is a DLL, not an executable")
    if version_key(version) <= version_key(feed["Version"]):
        raise ReleaseError(f"Manager {version} is not newer than the feed's {feed['Version']}")
    old = feed["Version"]
    feed.update({"Schema": 1, "Version": version, "Url": download_url(repository, tag, MANAGER_ASSET),
                 "Sha256": sha256(data), "Size": len(data)})
    write_json(feed_path, feed)
    plan["Changes"].append(f"Manager {old} -> {version}")
    plan["Assets"].append({"Name": MANAGER_ASSET, "Url": feed["Url"], "Sha256": feed["Sha256"], "Size": len(data)})
    plan["Files"] = ["manager-update.json"]
    return plan


def asset_record(repository, tag, path):
    data = path.read_bytes()
    return {"Name": path.name, "Url": download_url(repository, tag, path.name), "Sha256": sha256(data), "Size": len(data)}


def verify(plan, attempts=6, opener=None):
    """Download every asset without credentials and compare it with the plan."""
    opener = opener or urllib.request.build_opener()
    failures = []
    for asset in plan["Assets"]:
        for attempt in range(attempts):
            try:
                request = urllib.request.Request(asset["Url"], headers={"User-Agent": "AnyAPI-publish-check"})
                with opener.open(request, timeout=60) as response:
                    data = response.read(MAX_PAYLOAD * 2 + 1)
                break
            except OSError as error:
                if attempt == attempts - 1:
                    failures.append(f"{asset['Name']}: download failed ({error})")
                    data = None
                else:
                    time.sleep(5 * (attempt + 1))
        if data is None:
            continue
        if len(data) != asset["Size"] or sha256(data) != asset["Sha256"]:
            failures.append(f"{asset['Name']}: public download does not match the draft asset")
        else:
            print(f"Verified {asset['Name']} ({asset['Size']} bytes)")
    if failures:
        raise ReleaseError("; ".join(failures))


def summary(plan):
    lines = [f"## {plan['Tag']}", ""]
    lines += [f"- {change}" for change in plan["Changes"]]
    lines += ["", "| Asset | Size | SHA-256 |", "|---|---|---|"]
    lines += [f"| {a['Name']} | {a['Size']} | `{a['Sha256']}` |" for a in plan["Assets"]]
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare")
    p.add_argument("--tag", required=True)
    p.add_argument("--assets", required=True, type=Path)
    p.add_argument("--plan", type=Path)
    p.add_argument("--summary", type=Path)
    p.add_argument("--root", type=Path, default=ROOT, help="repository to update (default: this checkout)")
    v = commands.add_parser("verify")
    v.add_argument("--plan", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            plan = prepare(args.tag, args.assets, args.root)
            text = summary(plan)
            print(text)
            if args.plan:
                args.plan.write_text(json.dumps(plan, indent=2), encoding="utf-8")
            if args.summary:
                with args.summary.open("a", encoding="utf-8") as handle:
                    handle.write(text)
        else:
            verify(json.loads(args.plan.read_text(encoding="utf-8")))
    except (ReleaseError, KeyError, ValueError, OSError) as error:
        print(f"::error::{error}" if "GITHUB_ACTIONS" in os.environ else f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
