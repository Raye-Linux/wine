# SPDX-License-Identifier: LGPL-2.1-or-later
"""rekordbox: its setup finds its profile, the profile alone turns on the
hidden version exports, and the patch that does it stays off for the rest.

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
BASE_PROFILES = ROOT.parent / "iso" / "packages" / "raye-base" / "release" / "windows"
TITLES = WINDOWS / "titles" / "50-raye.titles"
PATCHES = ROOT / "patches" / "base"
PATCH = "0020-hide-version-exports.patch"
HIDDEN = ("wine_get_version", "wine_get_build_id", "wine_get_host_version")
sys.dont_write_bytecode = True


def keys(path):
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r'^([a-z]+)="(.*)"$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def env(path):
    return dict(kv.split("=", 1) for kv in keys(path).get("env", "").split())


def added(name):
    """the lines the patch adds to one file"""
    out, inside = [], False
    for line in (PATCHES / PATCH).read_text(encoding="utf-8").splitlines():
        if line.startswith("diff --git "):
            inside = line.endswith(" b/" + name)
        elif inside and line.startswith("+") and not line.startswith("+++"):
            out.append(line[1:])
    return "\n".join(out)


class Profile(unittest.TestCase):
    def test_finds_the_setup(self):
        match = keys(PROFILES / "rekordbox.profile")["match"].split()
        # the Store takes Install_rekordbox_x64_7_2_20.exe out of the zip;
        # the runner lowercases the name before matching
        for name in ("Install_rekordbox_x64_7_2_20.exe", "Install_rekordbox_x64_7_3_0 (1).exe"):
            self.assertTrue(any(fnmatch.fnmatchcase(name.lower(), p) for p in match), name)
        self.assertFalse(any(fnmatch.fnmatchcase("rekordbox.exe", p) for p in match))

    def test_hides_the_version_exports_for_itself_only(self):
        self.assertEqual(env(PROFILES / "rekordbox.profile").get("WINEHIDEEXPORTS"), "1")
        self.assertEqual(keys(PROFILES / "rekordbox.profile")["title"], "rekordbox")
        others = [p for p in PROFILES.glob("*.profile") if p.stem != "rekordbox"]
        if BASE_PROFILES.is_dir():
            others += list((BASE_PROFILES / "profiles").glob("*.profile")) + [BASE_PROFILES / "default.profile"]
        for p in others:
            with self.subTest(p.name):
                self.assertNotIn("WINEHIDEEXPORTS", env(p))


class Patch(unittest.TestCase):
    def test_is_applied_after_the_older_base_patches(self):
        series = [l for l in (PATCHES / "series").read_text().splitlines() if l and not l.startswith("#")]
        older = [p for p in series if p < PATCH]
        self.assertEqual(series[:len(older)], older)
        self.assertEqual(series[len(older)], PATCH)

    def test_hides_the_three_exports_from_getprocaddress(self):
        code = added("dlls/ntdll/loader.c")
        for name in HIDDEN:
            self.assertIn("(const void *)" + name, code)
        self.assertIn("if (is_hidden_export( proc )) proc = NULL;", code)
        # read once, at process start, before any program code runs
        self.assertIn("load_hide_exports_option();", code)

    def test_is_off_unless_asked(self):
        code = added("dlls/ntdll/loader.c")
        self.assertRegex(code, r"static BOOL hide_exports;")
        self.assertIn('L"WINEHIDEEXPORTS"', code)
        self.assertIn('L"HideWineExports"', code)
        self.assertIn('L"Software\\\\Wine\\\\AppDefaults\\\\"', code)
        self.assertNotIn("hide_exports = TRUE", code)

    def test_declares_what_it_compares(self):
        header = added("dlls/ntdll/ntdll_misc.h")
        for name in HIDDEN:
            self.assertIn(name, header)


class Title(unittest.TestCase):
    def test_says_only_what_was_measured(self):
        t = configparser.ConfigParser(interpolation=None)
        t.read(TITLES, encoding="utf-8")
        self.assertIn("rekordbox", t)
        r = t["rekordbox"]
        self.assertEqual(r["state"], "untested")
        self.assertEqual(r["profile"], "rekordbox")
        self.assertTrue(r["why_en"].strip() and r["why_fr"].strip())
        self.assertNotRegex(r["why_en"] + r["why_fr"], r"(?i)\bwine\b")


if __name__ == "__main__":
    unittest.main()
