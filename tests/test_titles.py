# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks of the per-title answers the Store shows before a download:

    python3 -I -B -m unittest discover -s tests

The one rule worth the whole file: a state that tells somebody a game
plays here has to carry the machine it played on and the day it did.
A launcher that opened is not a game that played, and "it probably
works" in front of a forty-gigabyte download is a lie with a progress
bar. So `works` and `offline-only` are refused without tested_on and
tested_at, and nothing in the tree can write them by accident.

Also checked: every state word is one of the four; `blocked` carries the
publisher's refusal and a source, so a no is never just an opinion;
every reason is one short line in both languages; every profile= names a
profile that exists; the generated list cannot bury a verified answer;
and the reader resolves the installer names that were measured, not
remembered.
"""
import configparser
import datetime
import importlib.machinery
import importlib.util
import os
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
WINDOWS = ROOT / "release" / "windows"
TITLES = WINDOWS / "titles"
PROFILES = WINDOWS / "profiles"
READER = WINDOWS / "raye-windows-titles"
sys.dont_write_bytecode = True

# The four words, and whether each is a positive claim about this system.
POSITIVE = {"works": True, "offline-only": True, "blocked": False, "untested": False}


def reader():
    loader = importlib.machinery.SourceFileLoader("raye_windows_titles", str(READER))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def files():
    return sorted(TITLES.glob("*.titles"))


def parse(path):
    parser = configparser.ConfigParser(interpolation=None)
    with open(path, encoding="utf-8") as f:
        parser.read_file(f, str(path))
    return parser


class Shape(unittest.TestCase):
    def test_there_is_at_least_one_file_and_all_of_them_parse(self):
        self.assertTrue(files(), "no *.titles in %s" % TITLES)
        for path in files():
            parse(path)                      # raises on a malformed file

    def test_every_state_is_one_of_the_four_words(self):
        for path in files():
            for tid, section in parse(path).items():
                if tid == "DEFAULT":
                    continue
                state = section.get("state", "")
                self.assertIn(state, POSITIVE, "%s [%s]" % (path.name, tid))

    def test_every_title_says_why_in_both_languages_on_one_line(self):
        for path in files():
            for tid, section in parse(path).items():
                if tid == "DEFAULT":
                    continue
                for key in ("why_en", "why_fr"):
                    why = section.get(key, "")
                    where = "%s [%s] %s" % (path.name, tid, key)
                    self.assertTrue(why.strip(), where)
                    self.assertNotIn("\n", why, where)
                    self.assertLessEqual(len(why), 200, where)
                self.assertTrue(section.get("name", "").strip(),
                                "%s [%s] has no name" % (path.name, tid))


class APositiveStateNeedsAWitness(unittest.TestCase):
    """works and offline-only are claims somebody has to stand behind."""

    def records(self):
        for path in files():
            for tid, section in parse(path).items():
                if tid != "DEFAULT":
                    yield path, tid, section

    def test_works_and_offline_only_carry_a_machine_and_a_date(self):
        for path, tid, section in self.records():
            state = section.get("state", "")
            if not POSITIVE.get(state):
                continue
            where = "%s [%s] state=%s" % (path.name, tid, state)
            on = section.get("tested_on", "").strip()
            at = section.get("tested_at", "").strip()
            self.assertTrue(on, where + ": no tested_on. A launcher that "
                                        "opened is not a game that played.")
            self.assertTrue(at, where + ": no tested_at")
            try:
                when = datetime.date.fromisoformat(at)
            except ValueError:
                self.fail(where + ": tested_at=%r is not a YYYY-MM-DD date" % at)
            self.assertLessEqual(when, datetime.date.today(),
                                 where + ": tested_at is in the future")

    def test_blocked_carries_the_refusal_and_its_source(self):
        for path, tid, section in self.records():
            if section.get("state") != "blocked":
                continue
            where = "%s [%s]" % (path.name, tid)
            self.assertEqual(section.get("upstream", ""), "refused",
                             where + ": blocked without upstream=refused is an "
                                     "opinion, not a fact")
            self.assertTrue(section.get("source", "").strip(),
                            where + ": blocked with no source")


class Links(unittest.TestCase):
    def test_every_profile_reference_names_a_profile_that_exists(self):
        have = {p.stem for p in PROFILES.glob("*.profile")}
        for path in files():
            for tid, section in parse(path).items():
                if tid == "DEFAULT":
                    continue
                ref = section.get("profile", "").strip()
                if ref:
                    self.assertIn(ref, have, "%s [%s] names profile %r" %
                                  (path.name, tid, ref))

    def test_a_generated_list_cannot_bury_a_verified_answer(self):
        # The order is the mechanism: files are read by name and the last
        # one wins, so anything generated has to sort before 50-raye.
        names = [p.name for p in files()]
        self.assertIn("50-raye.titles", names)
        for name in names:
            if name == "50-raye.titles":
                continue
            text = (TITLES / name).read_text(encoding="utf-8")
            if "do not edit by hand" in text:
                self.assertLess(name, "50-raye.titles",
                                "%s is generated but sorts after the verified "
                                "file, so it would override it" % name)

    def test_the_hand_file_wins_where_both_name_a_title(self):
        both = set(parse(TITLES / "50-raye.titles").sections())
        others = [p for p in files() if p.name != "50-raye.titles"]
        overlap = set()
        for path in others:
            overlap |= both & set(parse(path).sections())
        self.assertTrue(overlap, "nothing overlaps, so this proves nothing: "
                                 "the generated list no longer names roblox?")
        mod = reader()
        env = str(TITLES)
        os.environ["RAYE_WINDOWS_TITLES_PATH"] = env
        try:
            loaded = mod.load()
        finally:
            del os.environ["RAYE_WINDOWS_TITLES_PATH"]
        for tid in overlap:
            self.assertEqual(loaded[tid]["from"], str(TITLES / "50-raye.titles"), tid)


class Reader(unittest.TestCase):
    def run_reader(self, *args):
        env = dict(os.environ, RAYE_WINDOWS_TITLES_PATH=str(TITLES),
                   RAYE_WINDOWS_PROFILES_PATH=str(PROFILES))
        env.pop("LANG", None)
        return subprocess.run([sys.executable, "-I", "-B", str(READER)] + list(args),
                              capture_output=True, text=True, env=env)

    def test_it_resolves_the_installer_names_that_were_measured(self):
        # setup.rbxcdn.com served all three (HTTP 200, 2026-11-06), and the
        # OBS one is the asset name of release 33.0.1. Guessing these is how
        # a profile silently never matches.
        for name, want in (("RobloxStudioLauncherBeta.exe", "roblox-studio"),
                           ("RobloxStudioInstaller.exe", "roblox-studio"),
                           ("RobloxPlayerLauncher.exe", "roblox"),
                           ("RobloxPlayerInstaller.exe", "roblox"),
                           ("OBS-Studio-33.0.1-Windows-x64-Installer.exe", "obs"),
                           ("MinecraftInstaller.exe", "minecraft")):
            r = self.run_reader("--file", name)
            self.assertEqual(r.returncode, 0, name + ": " + r.stderr)
            self.assertTrue(r.stdout.startswith(want), "%s -> %s" % (name, r.stdout))

    def test_a_download_nobody_claims_is_not_answered_for(self):
        r = self.run_reader("--file", "TotallyUnknownThing.exe")
        self.assertEqual(r.returncode, 3, r.stdout)

    def test_an_unknown_title_is_untested_and_says_so_by_its_exit(self):
        r = self.run_reader("--state", "no-such-title-at-all")
        self.assertEqual(r.stdout.strip(), "untested")
        self.assertEqual(r.returncode, 3, "an unknown title must be "
                                          "distinguishable from a known one")

    def test_a_blocked_title_answers_blocked_in_both_languages(self):
        self.assertEqual(self.run_reader("--state", "roblox").stdout.strip(), "blocked")
        en = self.run_reader("--lang", "en", "roblox").stdout
        fr = self.run_reader("--lang", "fr_CA.UTF-8", "roblox").stdout
        self.assertNotEqual(en, fr, "the French answer is the English one")
        self.assertIn("blocked", en)

    def test_the_reader_names_the_two_folders_it_reads_in_order(self):
        r = subprocess.run([sys.executable, "-I", "-B", str(READER), "--where"],
                           capture_output=True, text=True,
                           env={k: v for k, v in os.environ.items()
                                if k != "RAYE_WINDOWS_TITLES_PATH"})
        self.assertEqual(r.returncode, 0, r.stderr)
        mod = reader()
        self.assertEqual(mod.directories(),
                         ["/usr/share/raye/windows-titles",
                          "/var/lib/raye/windows-titles"],
                         "the drop-in folder is how a title changes without an "
                         "image rebuild; losing it loses the mechanism")


if __name__ == "__main__":
    unittest.main()
