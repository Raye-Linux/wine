# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks of the tree itself, no Wine source needed:

    python3 -I -B -m unittest discover -s tests

-B is not optional: -I makes the interpreter ignore
PYTHONDONTWRITEBYTECODE, and unittest compiles these very files before
any of them can set sys.dont_write_bytecode, so without it the run
leaves a __pycache__ under the repository. raye-tests already passes -B;
this is the same command.

- every series names patches that exist, and every patch is in a series;
- every patch says what it does before its diff;
- the French table never brings the old name back;
- po-rename.py swaps msgid and msgstr and refuses a table that no longer
  matches;
- the third-party list is complete and its hashes look like hashes.
"""
import importlib.machinery
import importlib.util
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PATCHES = ROOT / "patches"
sys.dont_write_bytecode = True


def load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def series(folder):
    lines = (folder / "series").read_text().splitlines()
    return [l.strip() for l in lines if l.strip() and not l.startswith("#")]


class Series(unittest.TestCase):
    def test_three_folders_each_with_a_series(self):
        for name in ("base", "audio", "capture"):
            self.assertTrue((PATCHES / name / "series").is_file(), name)

    def test_series_and_files_agree(self):
        for folder in (PATCHES / n for n in ("base", "audio", "capture")):
            listed = series(folder)
            self.assertEqual(len(listed), len(set(listed)), folder)
            on_disk = sorted(p.name for p in folder.glob("*.patch"))
            self.assertEqual(sorted(listed), on_disk, folder)

    def test_a_series_is_in_the_order_its_own_numbers_say(self):
        # test_series_and_files_agree sorts both sides, so it compares the
        # SET of patches and says nothing about the order -- and the order
        # is the whole purpose of the file: 0009-french-names renames the
        # strings 0006-programs introduced, and 0011-last-name-leaks is
        # called last because it is. A reordered series still builds right
        # up to the git apply that cannot land, three quarters of an hour
        # in, with a message about a hunk rather than about the order.
        for folder in (PATCHES / n for n in ("base", "audio", "capture")):
            listed = series(folder)
            numbered = [l for l in listed if re.match(r"^\d{4}-", l)]
            self.assertEqual(numbered, sorted(numbered),
                             "%s/series is not in the order its numbers say; "
                             "build.sh applies it top to bottom" % folder.name)
            self.assertEqual(len(numbered), len(listed),
                             "%s/series has a patch with no number, so nothing "
                             "can say where it belongs" % folder.name)

    def test_every_patch_says_what_it_does(self):
        for patch in PATCHES.glob("*/*.patch"):
            text = patch.read_text(encoding="utf-8")
            head = text.split("\ndiff --git ", 1)[0]
            self.assertTrue(head.startswith("Subject: "), patch)
            body = head.split("\n", 1)[1].strip()
            self.assertGreater(len(body), 20, patch)
            self.assertIn("\ndiff --git ", text, patch)


class FrenchTable(unittest.TestCase):
    def rows(self):
        lines = (ROOT / "tools" / "names-fr.tsv").read_text(encoding="utf-8").splitlines()
        return [l.split("\t") for l in lines if l and not l.startswith("#")]

    def test_three_columns_and_no_old_name(self):
        rows = self.rows()
        self.assertGreater(len(rows), 40)
        for row in rows:
            self.assertEqual(len(row), 3, row[0][:40])
            for text in row[1:]:
                self.assertNotRegex(text, r"\bWine", row[0][:40])

    def test_same_escapes_on_both_sides(self):
        for old, new, fr in self.rows():
            self.assertEqual(new.count("%s"), old.count("%s") if "%s" in new else 0, old[:40])
            self.assertEqual(fr.count("%s"), new.count("%s"), old[:40])
            # translators break long help texts their own way; the ending counts
            self.assertEqual(fr.endswith("\\n"), new.endswith("\\n"), old[:40])


PO = '''msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\\n"

#: programs/x/x.rc:1
msgid "Old title"
msgstr "Ancien titre"

#: programs/x/x.rc:2
#, fuzzy
msgid ""
"A long old sentence that goes on and on so that gettext has to cut it in "
"two lines."
msgstr "Une phrase"

#: programs/x/x.rc:3
msgid "Untouched"
msgstr "Intact"
'''


class PoRename(unittest.TestCase):
    def setUp(self):
        self.mod = load(ROOT / "tools" / "po-rename.py", "po_rename")

    def test_swaps_and_drops_fuzzy(self):
        rows = [["Old title", "New title", "Nouveau titre"],
                ["A long old sentence that goes on and on so that gettext has to cut it in two lines.",
                 "Short", "Court"]]
        out, missing = self.mod.rename(PO, rows)
        self.assertEqual(missing, [])
        self.assertIn('msgid "New title"\nmsgstr "Nouveau titre"\n', out)
        self.assertIn('#: programs/x/x.rc:2\nmsgid "Short"\nmsgstr "Court"\n', out)
        self.assertNotIn("fuzzy", out)
        self.assertIn('msgid "Untouched"\nmsgstr "Intact"\n', out)

    def test_a_new_name_that_exists_is_merged(self):
        po = PO + '\n#: programs/y/y.rc:9\nmsgid "Help"\nmsgstr "Aide"\n'
        out, missing = self.mod.rename(po, [["Old title", "Help", "Aide"],
                                            ["Untouched", "Help", "Aide"]])
        self.assertEqual(missing, [])
        self.assertEqual(out.count('msgid "Help"'), 1)
        entry = out[out.index("#: programs/x/x.rc:1"):]
        self.assertTrue(entry.startswith("#: programs/x/x.rc:1\n#: programs/x/x.rc:3\n"
                                         "#: programs/y/y.rc:9\nmsgid \"Help\""), entry[:120])

    def test_long_strings_are_wrapped(self):
        text = "word " * 40 + "end\\n"
        out = self.mod.po_string("msgid", text.strip())
        lines = out.splitlines()
        self.assertEqual(lines[0], 'msgid ""')
        self.assertTrue(all(len(l) <= 81 for l in lines))
        self.assertEqual("".join(re.findall(r'"(.*)"', out)), text.strip())

    def test_a_string_that_moved_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            table = pathlib.Path(tmp) / "t.tsv"
            table.write_text("Gone\tNew\tNouveau\n", encoding="utf-8")
            po = pathlib.Path(tmp) / "fr.po"
            po.write_text(PO, encoding="utf-8")
            r = subprocess.run([sys.executable, "-I", "-B", str(ROOT / "tools" / "po-rename.py"),
                                str(table), str(po)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn("not found: Gone", r.stderr)
            self.assertEqual(po.read_text(encoding="utf-8"), PO)


class ThirdParty(unittest.TestCase):
    def test_list(self):
        rows = [l.split() for l in (ROOT / "third-party" / "sources.txt").read_text().splitlines()
                if l.strip() and not l.startswith("#")]
        self.assertEqual(sorted(r[0] for r in rows),
                         ["dxvk-src", "gecko32", "gecko64", "mono", "nvapi", "vkd3d"])
        for kind, version, digest, url in rows:
            self.assertTrue(url.startswith("https://"), kind)
            if kind == "dxvk-src":
                # a git checkout, pinned by the commit of its tag
                self.assertRegex(digest, r"^[0-9a-f]{40}$", kind)
                self.assertTrue(url.endswith(".git"), kind)
            else:
                self.assertRegex(digest, r"^[0-9a-f]{64}$", kind)
                self.assertIn(version, url, kind)

    def test_upstream_is_pinned(self):
        text = (ROOT / "UPSTREAM").read_text()
        self.assertRegex(text, r"(?m)^sha256=[0-9a-f]{64}$")
        version = re.search(r"(?m)^version=(\S+)$", text).group(1)
        self.assertRegex(text, r"(?m)^url=https://\S+/wine-%s\.tar\.xz$" % re.escape(version))


if __name__ == "__main__":
    unittest.main()
