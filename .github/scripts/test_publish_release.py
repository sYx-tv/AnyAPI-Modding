#!/usr/bin/env python3
"""Tests for publish_release.py against copies of the real catalog and feed."""
import json
import shutil
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import publish_release as pr  # noqa: E402

REPO = pr.ROOT
DOWNLOAD = "https://github.com/sYx-tv/AnyAPI-Modding/releases/download/"


def pe(dll=True, machine=0x8664):
    data = bytearray(0x200)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x84, machine)
    struct.pack_into("<H", data, 0x80 + 22, 0x2022 if dll else 0x0022)
    return bytes(data)


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp())
        self.root = self.temp / "repo"
        self.assets = self.temp / "assets"
        self.root.mkdir()
        self.assets.mkdir()
        for name in ("catalog.json", "manager-update.json"):
            shutil.copy(REPO / name, self.root / name)
        # Pin the versions the tests release against, so publishing a real
        # release (which moves the live catalog forward) doesn't break them.
        catalog = self.catalog()
        pinned = {"AnyAPI": ("0.33.0", 33, 0), "AnyClock": ("1.0.0", 1, 30), "AnyBalance": ("1.0.0", 1, 33)}
        for entry in catalog["Api"] + catalog["Mods"]:
            if entry["Name"] in pinned:
                entry["Version"], entry["Revision"], entry["MinimumApi"] = pinned[entry["Name"]]
        pr.write_json(self.root / "catalog.json", catalog)
        feed = json.loads((self.root / "manager-update.json").read_text(encoding="utf-8"))
        feed["Version"] = "1.3.1"
        pr.write_json(self.root / "manager-update.json", feed)

    def tearDown(self):
        shutil.rmtree(self.temp)

    def package(self, name, path, data=None):
        with zipfile.ZipFile(self.assets / name, "w") as archive:
            archive.writestr(path, pe() if data is None else data)

    def manifest(self, value):
        (self.assets / pr.MANIFEST).write_text(json.dumps(value), encoding="utf-8")

    def catalog(self):
        return json.loads((self.root / "catalog.json").read_text(encoding="utf-8"))

    def prepare(self, tag):
        return pr.prepare(tag, self.assets, self.root)

    def fails(self, tag, text):
        before = (self.root / "catalog.json").read_bytes()
        with self.assertRaises(pr.ReleaseError) as caught:
            self.prepare(tag)
        self.assertIn(text, str(caught.exception))
        self.assertEqual(before, (self.root / "catalog.json").read_bytes(), "catalog changed on failure")

    def test_mod_update_changes_only_that_entry(self):
        old = self.catalog()
        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/AnyClock.dll")
        plan = self.prepare("mods-2026.10.09")
        new = self.catalog()
        clock = next(m for m in new["Mods"] if m["Id"] == "anyclock")
        self.assertEqual(clock["Version"], "1.1.0")
        self.assertEqual(clock["Revision"], 2)
        self.assertEqual(clock["Url"], DOWNLOAD + "mods-2026.10.09/AnyClock-1.1.0.zip")
        self.assertEqual(clock["Description"], next(m for m in old["Mods"] if m["Id"] == "anyclock")["Description"])
        others = lambda c: [m for m in c["Mods"] if m["Id"] != "anyclock"]
        self.assertEqual(others(old), others(new))
        self.assertEqual(old["Api"], new["Api"])
        self.assertEqual([m["Id"] for m in old["Mods"]], [m["Id"] for m in new["Mods"]])
        self.assertEqual(plan["Assets"][0]["Url"], clock["Url"])
        self.assertTrue((self.root / "catalog.json").read_bytes().endswith(b"}\r\n"))

    def test_unchanged_catalog_round_trips_byte_for_byte(self):
        original = (REPO / "catalog.json").read_bytes()
        pr.write_json(self.root / "catalog.json", json.loads(original.decode("utf-8")))
        self.assertEqual(original, (self.root / "catalog.json").read_bytes())

    def test_api_release_with_mod(self):
        self.package("AnyAPI-0.34.0.zip", "dinput8.dll")
        self.package("AnyBalance-1.1.0.zip", "AnyAPI and Modding/mods/AnyBalance.dll")
        self.manifest({"Packages": {"AnyBalance": {"MinimumApi": 34, "Description": "Centre of mass and tipping."}}})
        self.prepare("v0.34.0")
        catalog = self.catalog()
        self.assertEqual(catalog["Api"][0]["Version"], "0.34.0")
        self.assertEqual(catalog["Api"][0]["Revision"], 34)
        balance = next(m for m in catalog["Mods"] if m["Id"] == "anybalance")
        self.assertEqual((balance["MinimumApi"], balance["Revision"]), (34, 2))
        self.assertEqual(balance["Description"], "Centre of mass and tipping.")

    def test_new_mod_needs_manifest(self):
        self.package("AnyCompass-1.0.0.zip", "AnyAPI and Modding/mods/AnyCompass.dll")
        self.fails("mods-2026.10.09", "add its Id, MinimumApi, Description")
        self.manifest({"Packages": {"AnyCompass": {"Id": "anycompass", "MinimumApi": 33, "Description": "Compass."}}})
        self.prepare("mods-2026.10.09")
        compass = self.catalog()["Mods"][-1]
        self.assertEqual((compass["Id"], compass["Revision"]), ("anycompass", 1))
        self.assertEqual(compass["GameBuilds"], self.catalog()["Api"][0]["GameBuilds"])

    def test_rejections(self):
        self.package("AnyClock-1.0.0.zip", "AnyAPI and Modding/mods/AnyClock.dll")
        self.fails("mods-2026.10.09", "not newer")
        (self.assets / "AnyClock-1.0.0.zip").unlink()

        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/Clock.dll")
        self.fails("mods-2026.10.09", "differs from the catalog")
        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/AnyClock.dll", pe(machine=0x14C))
        self.fails("mods-2026.10.09", "not x64")
        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/AnyClock.dll", pe(dll=False))
        self.fails("mods-2026.10.09", "not a DLL")
        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/AnyClock.dll", b"text")
        self.fails("mods-2026.10.09", "not a Windows executable")
        with zipfile.ZipFile(self.assets / "AnyClock-1.1.0.zip", "w") as archive:
            archive.writestr("AnyAPI and Modding/mods/AnyClock.dll", pe())
            archive.writestr("AnyAPI and Modding/mods/settings.json", "{}")
        self.fails("mods-2026.10.09", "exactly one file")
        self.package("AnyClock-1.1.0.zip", "AnyAPI and Modding/mods/AnyClock.dll")

        self.fails("v0.34.0", "must include AnyAPI-0.34.0.zip")
        self.fails("sdk-reference-2026.10.09", "not a catalog release tag")
        self.manifest({"Packages": {"AnyClock": {"MinimumApi": 40}}})
        self.fails("mods-2026.10.09", "needs API revision 40")
        self.manifest({"Packages": {"AnyClock": {"Url": "x"}}})
        self.fails("mods-2026.10.09", "cannot set Url")
        self.manifest({"Packages": {"AnyMap": {}}})
        self.fails("mods-2026.10.09", "not in the release")
        (self.assets / pr.MANIFEST).unlink()

        (self.assets / "notes.txt").write_text("x")
        self.fails("mods-2026.10.09", "Unexpected asset notes.txt")
        self.manifest({"Extra": ["notes.txt"]})
        plan = self.prepare("mods-2026.10.09")
        self.assertEqual(sorted(a["Name"] for a in plan["Assets"]), ["AnyClock-1.1.0.zip", "notes.txt"])

    def test_api_zip_under_mods_tag(self):
        self.package("AnyAPI-0.34.0.zip", "dinput8.dll")
        self.fails("mods-2026.10.09", "mods-only tag")

    def test_manager_release(self):
        (self.assets / pr.MANAGER_ASSET).write_bytes(pe(dll=False))
        self.prepare("manager-v1.3.2")
        feed = json.loads((self.root / "manager-update.json").read_text(encoding="utf-8"))
        self.assertEqual(feed["Version"], "1.3.2")
        self.assertEqual(feed["Url"], DOWNLOAD + "manager-v1.3.2/AnyAPI.Manager.exe")
        self.assertEqual(feed["Size"], 0x200)
        with self.assertRaises(pr.ReleaseError):
            pr.prepare("manager-v1.3.1", self.assets, self.root)

    def test_verify_compares_downloads(self):
        data = b"zip bytes"
        plan = {"Assets": [{"Name": "a.zip", "Url": "https://example.invalid/a.zip",
                            "Sha256": pr.sha256(data), "Size": len(data)}]}

        class Response:
            def __init__(self, body):
                self.body = body
            def __enter__(self):
                return self
            def __exit__(self, *exc):
                return False
            def read(self, limit):
                return self.body

        class Opener:
            def __init__(self, body):
                self.body = body
            def open(self, request, timeout):
                return Response(self.body)

        pr.verify(plan, opener=Opener(data))
        with self.assertRaises(pr.ReleaseError):
            pr.verify(plan, opener=Opener(b"other"))


if __name__ == "__main__":
    unittest.main()
