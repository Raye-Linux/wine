# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks of the profiles, against the way the runner actually reads them:

    python3 -I -B -m unittest discover -s tests

raye-run-windows reads a profile with sed, one line at a time:

    sed -n 's/^match="\\(.*\\)"$/\\1/p'

so a leading space, a trailing comment, or a key split over two lines is
not a profile with a small flaw -- it is a key the runner never sees, and
a program that quietly gets a bare space instead of the one it needs. The
checks below use that same expression rather than a friendlier parser, so
they fail for the reasons the runner would.

Two silent-failure shapes get their own test:

- the runner lowercases the file name before matching, so a pattern with
  a capital in it can never match anything, ever, and nothing says so;
- the runner takes the FIRST profile whose pattern matches, in file-name
  order, so two profiles that can match one name are a coin toss decided
  by glob order -- and glob order depends on the locale's collation.

And one packaging shape: a profile of the same name in raye-base is not a
second profile, it is the same one, and which copy wins depends on which
package was unpacked last.
"""
import fnmatch
import os
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
WINDOWS = ROOT / "release" / "windows"
PROFILES = WINDOWS / "profiles"
TITLES = WINDOWS / "titles"
REPLACES = WINDOWS / "REPLACES"
# Where the rest of the layer keeps its own profiles. Read, never written.
RAYE_BASE = pathlib.Path("/srv/raye/iso/packages/raye-base/release/windows/profiles")
sys.dont_write_bytecode = True

# The keys raye-run-windows looks for. Anything else in a profile is
# ignored by today's runner -- which is fine for title=, deliberately,
# but a typo in one of these is invisible.
KEYS = ("name", "match", "verbs", "env", "native", "title", "opens", "files", "game", "launcher")


def value(path, key):
    """Exactly what the runner's sed would pull out, or '' for nothing."""
    pattern = re.compile(r'^%s="(.*)"$' % re.escape(key), re.M)
    found = pattern.findall(path.read_text(encoding="utf-8"))
    return "\n".join(found)


def profiles():
    return sorted(PROFILES.glob("*.profile"))


class Format(unittest.TestCase):
    def test_there_are_profiles_and_each_one_names_itself(self):
        self.assertTrue(profiles(), "no *.profile in %s" % PROFILES)
        for path in profiles():
            self.assertTrue(value(path, "name"), "%s: no name= the runner can "
                                                 "read" % path.name)

    def test_every_key_appears_at_most_once(self):
        # Two name= lines become one value with a newline in it, which the
        # runner then puts in a dialog.
        for path in profiles():
            for key in KEYS:
                got = value(path, key)
                self.assertNotIn("\n", got, "%s: %s= appears twice" %
                                 (path.name, key))

    def test_no_line_sets_a_key_in_a_way_the_runner_cannot_read(self):
        # A line that looks like a setting but does not match the sed is
        # the worst case: it reads as configured and does nothing.
        looks_like = re.compile(r'^\s*([a-z_]+)\s*=\s*(.*)$')
        exact = re.compile(r'^[a-z_]+="[^"]*"$')
        for path in profiles():
            for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if line.startswith("#") or not line.strip():
                    continue
                m = looks_like.match(line)
                self.assertIsNotNone(m, "%s:%d is neither a comment nor a "
                                        "setting: %r" % (path.name, n, line))
                self.assertIn(m.group(1), KEYS, "%s:%d unknown key %r" %
                              (path.name, n, m.group(1)))
                self.assertRegex(line, exact, "%s:%d the runner's sed will not "
                                              "read this: %r" % (path.name, n, line))


class Matching(unittest.TestCase):
    def all_patterns(self):
        out = []
        for path in profiles():
            for pattern in value(path, "match").split():
                out.append((path.name, pattern))
        return out

    def test_no_pattern_has_a_capital_in_it(self):
        # raye-run-windows does `tr 'A-Z' 'a-z'` on the file name first, so
        # a capital here is a pattern that can never match anything.
        for name, pattern in self.all_patterns():
            self.assertEqual(pattern, pattern.lower(),
                             "%s: pattern %r can never match: the runner "
                             "lowercases the file name first" % (name, pattern))

    def test_two_profiles_can_never_match_the_same_download(self):
        # The runner takes the first match in glob order, and glob order
        # follows the locale's collation: overlapping patterns mean the
        # answer changes with LC_COLLATE. Probed with the names that
        # actually occur, plus each pattern made concrete.
        probes = set()
        for _, pattern in self.all_patterns():
            probes.add(pattern.replace("*", "x").replace("?", "x"))
            probes.add(pattern.replace("*", "").replace("?", "x"))
            probes.add(pattern.replace("*", "-1.2.3-setup-x64.exe"))
        for probe in sorted(probes):
            owners = sorted({name for name, pattern in self.all_patterns()
                             if fnmatch.fnmatchcase(probe, pattern)})
            self.assertLessEqual(len(owners), 1,
                                 "%r is matched by %s: which one the runner "
                                 "picks depends on the locale" % (probe, owners))

    def test_the_studio_and_the_player_never_catch_each_other(self):
        # These two are one word apart, their publisher blocks one and not
        # the other, and getting it backwards either refuses a program that
        # works or offers one that cannot.
        import importlib.machinery, importlib.util
        reader = WINDOWS / "raye-windows-titles"
        loader = importlib.machinery.SourceFileLoader("rwt", str(reader))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        mod = importlib.util.module_from_spec(spec)
        loader.exec_module(mod)
        for name, want in (("RobloxStudioLauncherBeta.exe", "roblox-studio"),
                           ("RobloxStudioLauncher.exe", "roblox-studio"),
                           ("RobloxStudioInstaller.exe", "roblox-studio"),
                           ("RobloxPlayerLauncher.exe", "roblox"),
                           ("RobloxPlayerInstaller.exe", "roblox")):
            self.assertEqual(mod.by_file(name, str(PROFILES)), want, name)


class LinkedToAnAnswer(unittest.TestCase):
    def test_every_profile_names_a_title_that_has_an_answer(self):
        known = set()
        for path in sorted(TITLES.glob("*.titles")):
            known |= set(re.findall(r"(?m)^\[([^\]]+)\]$",
                                    path.read_text(encoding="utf-8")))
        for path in profiles():
            tid = value(path, "title")
            self.assertTrue(tid, "%s: no title=, so nothing can be said about "
                                 "it before the download" % path.name)
            self.assertIn(tid, known, "%s: title=%r has no entry in titles/" %
                          (path.name, tid))


class SharedWithRayeBase(unittest.TestCase):
    """A file name that exists in both trees is one profile, not two."""

    def declared(self):
        if not REPLACES.is_file():
            return {}
        out = {}
        for line in REPLACES.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            path, _, why = line.partition("  ")
            out[path.strip()] = why.strip()
        return out

    def test_an_overlap_is_declared_in_replaces_with_a_reason(self):
        if not RAYE_BASE.is_dir():
            self.skipTest("raye-base is not beside this tree: %s" % RAYE_BASE)
        theirs = {p.name for p in RAYE_BASE.glob("*.profile")}
        declared = self.declared()
        for path in profiles():
            key = "profiles/" + path.name
            if path.name in theirs:
                self.assertIn(key, declared,
                              "%s also exists in raye-base and is not in "
                              "REPLACES: both go to the same folder, and which "
                              "one wins depends on unpack order" % path.name)
                self.assertGreater(len(declared[key]), 20,
                                   "%s: REPLACES says nothing useful" % key)
            else:
                self.assertNotIn(key, declared,
                                 "%s is in REPLACES but raye-base has no such "
                                 "file: the note is stale" % key)

    def test_no_declaration_points_at_a_file_we_do_not_ship(self):
        ours = {"profiles/" + p.name for p in profiles()}
        for key in self.declared():
            self.assertIn(key, ours, "REPLACES names %s, which is not here" % key)


if __name__ == "__main__":
    unittest.main()


class Opens(unittest.TestCase):
    """opens= and files= hand files and links to a program: every file type
    it names has to exist (raye-base's raye-windows.xml), and every
    extension in files= has to be one of those types, or Files never
    offers it."""

    MIME = RAYE_BASE.parent / "raye-windows.xml"

    def globs(self):
        import xml.etree.ElementTree as ET
        ns = "{http://www.freedesktop.org/standards/shared-mime-info}"
        out = {}
        for t in ET.parse(self.MIME).getroot().iter(ns + "mime-type"):
            out[t.get("type")] = {g.get("pattern") for g in t.iter(ns + "glob")}
        return out

    def test_types_and_extensions_agree(self):
        if not self.MIME.is_file():
            self.skipTest("raye-base is not checked out next to this tree")
        globs = self.globs()
        for path in profiles():
            opens = [t for t in value(path, "opens").split(";") if t]
            files = value(path, "files").split()
            types = [t for t in opens if not t.startswith("x-scheme-handler/")]
            for t in types:
                self.assertIn(t, globs, "%s: %s is not in raye-windows.xml" % (path.name, t))
            known = set().union(*(globs[t] for t in types)) if types else set()
            for ext in files:
                self.assertIn("*." + ext, known, "%s: .%s is in no type of opens=" % (path.name, ext))
            if files:
                self.assertTrue(types, "%s: files= without a file type in opens=" % path.name)

    def test_studio_takes_its_browser_sign_in_back(self):
        opens = value(PROFILES / "roblox-studio.profile", "opens").split(";")
        self.assertIn("x-scheme-handler/roblox-studio-auth", opens)
        self.assertNotIn("roblox", value(PROFILES / "roblox.profile", "opens"))

