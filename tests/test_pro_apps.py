# SPDX-License-Identifier: LGPL-2.1-or-later
"""FL Studio, Affinity and TikTok LIVE Studio: the profiles find the files
people actually get, and the titles say no more than was measured.

    python3 -I -B -m unittest discover -s tests
"""
import configparser
import fnmatch
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
WINDOWS = ROOT / "release" / "windows"
PROFILES = WINDOWS / "profiles"
TITLES = WINDOWS / "titles" / "50-raye.titles"
sys.dont_write_bytecode = True


def keys(name):
    out = {}
    for line in (PROFILES / (name + ".profile")).read_text(encoding="utf-8").splitlines():
        m = re.match(r'^([a-z]+)="(.*)"$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def matches(name, download):
    # the runner lowercases the file name before matching
    return any(fnmatch.fnmatchcase(download.lower(), p) for p in keys(name).get("match", "").split())


def titles():
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(TITLES, encoding="utf-8")
    return parser


class Affinity(unittest.TestCase):
    def test_finds_the_download(self):
        # downloads.affinity.studio serves "Affinity x64.exe"; the Store
        # saves it as affinity-x64.exe, a browser keeps the space
        for name in ("Affinity x64.exe", "affinity-x64.exe", "Affinity x64 (1).exe"):
            self.assertTrue(matches("affinity", name), name)
        self.assertFalse(matches("affinity", "affinity-photo-2.6.exe"))

    def test_gets_dotnet_and_the_builtin_renderer(self):
        k = keys("affinity")
        self.assertIn("dotnet48", k["verbs"].split())
        self.assertIn("win11", k["verbs"].split())
        env = dict(kv.split("=", 1) for kv in k["env"].split())
        overrides = env["WINEDLLOVERRIDES"]
        self.assertRegex(overrides, r"(^|;)[a-z0-9,]*\bd3d11\b[a-z0-9,]*=b")
        self.assertIn("dxgi", overrides)
        self.assertEqual(k["title"], "affinity")


class LiveStudio(unittest.TestCase):
    def test_finds_the_downloader(self):
        # the name one person reported from tiktok.com/studio/download
        self.assertTrue(matches("tiktok-live-studio", "tiktok_live_studio_downloader-v0.0.1-wid-abc123.exe"))
        self.assertTrue(matches("tiktok-live-studio", "TikTok LIVE Studio Setup.exe"))
        self.assertFalse(matches("tiktok-live-studio", "tiktok.exe"))
        self.assertEqual(keys("tiktok-live-studio")["title"], "tiktok-live-studio")


class Titles(unittest.TestCase):
    def test_nothing_is_claimed_before_a_real_pc(self):
        t = titles()
        for tid in ("fl-studio", "affinity", "tiktok-live-studio"):
            with self.subTest(tid):
                self.assertIn(tid, t)
                self.assertEqual(t[tid]["state"], "untested")
                self.assertTrue(t[tid]["why_en"].strip() and t[tid]["why_fr"].strip())
                self.assertNotRegex(t[tid]["why_en"] + t[tid]["why_fr"], r"(?i)\bwine\b")


if __name__ == "__main__":
    unittest.main()
