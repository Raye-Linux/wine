# SPDX-License-Identifier: LGPL-2.1-or-later
"""Every string this layer shows a user must exist in French:

    python3 -I -B -m unittest discover -s tests

HOW THIS DIFFERS FROM THE OTHER REPOSITORIES' TRANSLATION SWEEP

There is no gettext catalogue here, and there is not going to be one.
This repository ships no program of its own: it ships a release of an
upstream project plus patches, and the French of that project lives in
its own po/fr.po, inside the tarball, which is not in this tree. So the
usual check -- "every msgid of the template has a msgstr" -- has nothing
to read.

What this repository does ship, and therefore what is checked here:

    tools/names-fr.tsv            old English, new English, new French.
                                  tools/po-rename.py applies it to the
                                  release's po/fr.po during the build,
                                  which is how a renamed string keeps a
                                  translation instead of losing one.
    release/windows/titles/       255 answers shown before a download,
                                  each with why_en and why_fr.
    patches/*/*.patch             the lines a patch ADDS, including
                                  strings the release never had.

🚨 THE DEFECT THIS FILE EXISTS FOR: a patch adds a NEW user-visible
string. names-fr.tsv cannot help -- po-rename.py REFUSES a row whose old
English is not already in po/fr.po, which is the right behaviour and
means a brand-new string cannot be put in that table at all. So a new
string has exactly three honest ways to be French, and this file checks
that every one of them took one:

    1. it is the new English of a names-fr.tsv row, so the rename
       carries the French with it;
    2. 0009-french-names.patch adds it to po/fr.po as a msgid itself;
    3. the patch carries its own LANGUAGE LANG_FRENCH block in the same
       .rc, which is what patches/audio/0003-asio-driver.patch does for
       the ASIO panel.

Miss all three and the string reaches a French session in English, no
catalogue is short of anything, no tool complains, and nobody finds out
until somebody opens that dialog.

HOW THIS FITS WITH THE TESTS ALREADY HERE, rather than stacking on them

tests/test_names.py walks EXACTLY the same corpus -- the strings the
patches add -- and asks a different question of it: does this string
name the upstream project? Two orthogonal questions about one corpus, so
there is one walk, not two: this file IMPORTS its added_strings() rather
than writing a second copy that would drift from it. If that walker
changes, both questions move with it.

    test_names.py       does a user-visible string name the project?
    this file           does a user-visible string have French?

tests/test_tree.py (class FrenchTable) already checks that every row of
names-fr.tsv has three columns, never brings the old name back, and
keeps the same %s count and the same trailing newline on both sides. It
does NOT check that the French column says anything, nor that the French
of a new string exists at all. Those are here.

⚠️ Delegation nobody asserts is a hole, so class Delegated below fails
if either of those two files stops doing its half.

WHAT THIS FILE CANNOT SEE, said plainly rather than left to be assumed

  * Whether a translation is RIGHT. "Mappeur de sons" could say
    anything; only a person reading the dialog can tell.
  * The release's own 60-odd other languages. po-rename.py leaves them
    alone on purpose -- their entries for the old text stop matching, so
    those languages fall back to the new English, which at least never
    names the upstream project. That is a documented choice, not a bug,
    and it is the reason French is the only language checked here.
  * Whether the patches still apply. Only `build.sh --check` knows, and
    it needs the 35 MB tarball. A patch that stopped applying looks
    perfectly healthy here, so a string whose French this file approved
    may never reach the build at all.
  * Strings the release shows that no patch touches: they keep upstream's
    own French, which is upstream's business and is not in this tree.
  * Anything built at run time -- a string assembled from pieces is
    invisible to any reader of a patch.

WHAT IS DELIBERATELY NOT ASKED FOR, AND WHY -- the same kind of list
tools/check-names.sh and test_names.py keep, because a guard that asks
for the wrong thing gets switched off:

  * po/fr.po additions. That file IS the French; asking it for a
    translation of itself is circular.
  * .c and .h additions. The release keeps its user text in resources,
    and the C strings these patches add are registry keys
    (L"Buffer size" in dlls/rayeasio/asio.c), PulseAudio property names,
    GUIDs and trace lines. The two that ARE user text already carry both
    languages in the C itself: dlls/winepulse.drv/pulse.c asks
    locale_is_french() and returns "Sortie par défaut" or "Default
    output". class BothLanguagesInTheCode asserts that mechanism is
    still there, so it cannot quietly become English-only.
  * VER_FILEDESCRIPTION_STR in a Makefile.in. Programs read it to tell
    builtin DLLs apart; it is not shown.
  * The .profile names -- "OBS Studio", "Roblox", "Roblox Studio". They
    are the products' own names and have no French. Translating a
    product name is how a store ends up offering software nobody can
    find. test_names.py already guards them against the other defect.

🚨 WHY THE FLOORS EXIST. A test that walks a list and asserts something
about every item PASSES -- green, silent -- when the list is empty: zero
items, zero assertions, nothing said. A renamed folder, a regex that
stops matching, a titles file cut to its comments: each one empties a
list, and this whole file would then report success while protecting
nothing. So every collection reasoned about here is asserted non-empty,
and the large ones against a floor well below today's count. The guards
are tests of their own, in class Guards.
"""
import configparser
import importlib.machinery
import importlib.util
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True

TABLE = ROOT / "tools" / "names-fr.tsv"
TITLES = ROOT / "release" / "windows" / "titles"
PATCHES = ROOT / "patches"
PO_PATCH = PATCHES / "base" / "0009-french-names.patch"
NAMES_TEST = ROOT / "tests" / "test_names.py"
TREE_TEST = ROOT / "tests" / "test_tree.py"

# Today: 51 table rows, 255 titles, 2621 added strings, 26 patched .rc
# files. A floor catches a collapse to nothing; it is not meant to be
# raised each time a string is added.
FLOORS = {"rows": 40, "titles": 200, "added_strings": 200, "rc_files": 15,
          "french_msgids": 20}

# The keys of a title that a person reads before a download.
SHOWN = ("name", "why_en", "why_fr")

# A pair whose French is the same as its English, with the reason. Both
# are names, and a name has no translation. Two entries; a long list
# here would mean the check is wrong, not that French looks like
# English.
SAME_IN_BOTH = {
    "Internet Explorer": "the product's name, which French keeps",
    "Mines": "the game's name; the French for the game happens to be the "
             "same word, which is why the row exists at all -- it is "
             "renaming WineMine",
}


# --------------------------------------------------- the sibling tests
def load(path, name):
    """Import a sibling test module, so its walk is used and not copied."""
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


# ------------------------------------------------------------ the data
def rows():
    lines = TABLE.read_text(encoding="utf-8").splitlines()
    return [l.split("\t") for l in lines if l and not l.startswith("#")]


def titles():
    """{(file, id): record} for every title that is shipped."""
    out = {}
    for path in sorted(TITLES.glob("*.titles")):
        parser = configparser.ConfigParser(interpolation=None)
        with open(path, encoding="utf-8") as f:
            parser.read_file(f, str(path))
        for name in parser.sections():
            out[(path.name, name)] = parser[name]
    return out


def french_msgids():
    """Every msgid that 0009-french-names.patch ADDS to po/fr.po.

    A string added there is translated by that same patch, which adds
    the msgstr beside it, so it needs nothing from the table."""
    text = PO_PATCH.read_text(encoding="utf-8", errors="replace")
    out, buf, collecting = set(), [], False
    for line in text.splitlines():
        if not line.startswith("+"):
            if collecting:
                out.add("".join(buf))
            buf, collecting = [], False
            continue
        body = line[1:]
        if body.startswith("msgid "):
            if collecting:
                out.add("".join(buf))
            buf = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
            collecting = True
        elif collecting and re.match(r'^\s*"', body):
            buf += re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        else:
            if collecting:
                out.add("".join(buf))
            buf, collecting = [], False
    if collecting:
        out.add("".join(buf))
    out.discard("")
    return out


# ------------------------------------- reading what a patch adds to .rc
WORD = re.compile(r"[A-Za-z]{2,}")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Upstream's own marker for a string that must stay as it is: registry
# paths, class names. check-names.sh honours it too.
DO_NOT_TRANSLATE = "#msgctxt#do not translate#"
# A literal that is plainly not text for a person.
NOT_TEXT = [
    re.compile(r"^\s*$"),
    re.compile(r"^[^A-Za-z]*$"),            # digits, punctuation, %u, %s
    re.compile(r"^MS Shell Dlg"),           # the dialog font of every .rc
    re.compile(r"^[A-Za-z_][\w.]*\\\\"),    # Software\\Raye\\ASIO
    re.compile(r"^[a-z][a-z0-9.]*\.[a-z][a-z0-9.]*$"),   # stream.capture.sink
]


def is_text(value):
    """Several real words, and not one of the shapes above."""
    if any(p.search(value) for p in NOT_TEXT):
        return False
    return len(WORD.findall(value)) >= 2


def rc_additions():
    """Every string a patch adds to a .rc, grouped by the file it lands
    in and split by the language block it sits in.

    Two things have to be read out of the diff, and the second one is
    the one that caught me out while writing this.

    An .rc splits a long string across adjacent literals, which the
    resource compiler concatenates:

        "Some programs show web pages inside their windows and need "
        "an extra component for it."

    Each piece on its own is in no table; the whole string is. So every
    run of consecutive added lines is also offered joined, and a piece
    counts as translated when any join that contains it is.

    And a Windows resource file holds its translations INSIDE itself,
    one `LANGUAGE LANG_x` block per language, each a copy of the same
    dialogs and string tables with the same control ids. So the French
    of an English string in such a file is not beside it -- it is
    further down, in the French block. An English string is therefore
    translated BECAUSE ITS FILE HAS a French block, which is the
    opposite of what a first reading of the diff suggests: my first
    version asked whether the string itself sat in the French block,
    which approved the six French strings of the ASIO panel and flagged
    the six English ones it exists to translate.
    """
    files = {}
    for patch in sorted(PATCHES.glob("*/*.patch")):
        text = patch.read_text(encoding="utf-8", errors="replace")
        target, french, run = None, False, []

        def finish(run, french):
            if not run or target is None:
                return
            pieces = [v for _, v in run]
            entry = files.setdefault(
                (f"{patch.parent.name}/{patch.name}", target),
                {"english": [], "french": [], "has_french_block": False})
            side = entry["french"] if french else entry["english"]
            for k, (line_no, value) in enumerate(run):
                # Only the joins that CONTAIN this piece. Offering the
                # whole run's joins to every piece let one translated
                # string vouch for its untranslated neighbours: an LTEXT
                # added under `CAPTION "Web content support"` -- a string
                # that IS in the table -- was approved by it, because the
                # pair sat in one run of added lines. Measured while
                # proving this very check could go red, 2026-11-06. The
                # permissive direction is the silent one.
                joins = {"".join(pieces[i:j])
                         for i in range(k + 1)
                         for j in range(k + 1, len(pieces) + 1)}
                side.append({"line": line_no, "value": value, "joins": joins})

        for line_no, line in enumerate(text.splitlines(), 1):
            head = re.match(r"^\+\+\+ b/(.*)", line)
            if head:
                finish(run, french)
                run, target, french = [], head.group(1), False
                continue
            if target is None or not target.endswith(".rc"):
                continue
            if re.search(r"LANGUAGE\s+LANG_FRENCH", line):
                finish(run, french)
                run, french = [], True
                files.setdefault(
                    (f"{patch.parent.name}/{patch.name}", target),
                    {"english": [], "french": [], "has_french_block": False}
                )["has_french_block"] = True
                continue
            if re.search(r"LANGUAGE\s+LANG_(ENGLISH|NEUTRAL)", line):
                finish(run, french)
                run, french = [], False
                continue
            if not line.startswith("+") or line.startswith("+++"):
                finish(run, french)
                run = []
                continue
            if re.match(r"^\+\s*(//|/?\*)", line):
                continue
            if DO_NOT_TRANSLATE in line:
                continue
            values = LITERAL.findall(line[1:])
            if values:
                run += [(line_no, v) for v in values]
            else:
                finish(run, french)
                run = []
        finish(run, french)
    return files


ROWS = rows()
NEW_ENGLISH = {r[1] for r in ROWS if len(r) == 3}
FRENCH_MSGIDS = french_msgids()
RC = rc_additions()
TITLE_RECORDS = titles()
# Every English string added to a resource, with the file it is in.
RC_ENGLISH = [dict(item, patch=patch, file=target)
              for (patch, target), entry in sorted(RC.items())
              for item in entry["english"]]
RC_FRENCH = [dict(item, patch=patch, file=target)
             for (patch, target), entry in sorted(RC.items())
             for item in entry["french"]]


def has_a_path_to_french(item, entry):
    """One of the three honest ways, from the docstring."""
    if entry["has_french_block"]:
        return True                     # the file carries its own French
    candidates = {item["value"]} | item["joins"]
    return bool(candidates & NEW_ENGLISH) or bool(candidates & FRENCH_MSGIDS)


class Guards(unittest.TestCase):
    """What stops this file from passing by having looked at nothing."""

    def test_the_table_was_read(self):
        self.assertGreaterEqual(
            len(ROWS), FLOORS["rows"],
            f"only {len(ROWS)} rows read from tools/names-fr.tsv: the table or "
            f"the reader collapsed, and every check below would pass on nothing")
        self.assertTrue(NEW_ENGLISH, "no new English read from the table")

    def test_the_titles_were_read(self):
        self.assertGreaterEqual(
            len(TITLE_RECORDS), FLOORS["titles"],
            f"only {len(TITLE_RECORDS)} titles read from {TITLES}: a .titles "
            f"file was emptied or renamed, and the French check would pass on "
            f"an empty list")
        # A floor on the TOTAL hides a file going empty: 00-upstream has
        # 249 of the 255, so emptying 50-raye -- the six titles a person
        # verified by hand, the ones that override the generated answer
        # -- left the total at 249 and this guard green. Measured while
        # proving it could go red, 2026-11-06. So every file is counted
        # on its own.
        per_file = {}
        for where, _ in TITLE_RECORDS:
            per_file[where] = per_file.get(where, 0) + 1
        shipped = sorted(p.name for p in TITLES.glob("*.titles"))
        self.assertGreaterEqual(
            len(shipped), 2,
            f"only {len(shipped)} .titles file(s): {shipped}. The mechanism is "
            f"two of them -- the generated list and the hand-verified "
            f"override -- and losing one loses half the answers silently")
        empty = [name for name in shipped if not per_file.get(name)]
        self.assertEqual(
            [], empty,
            f"{len(empty)} .titles file(s) with no title in them at all: "
            f"{empty}. Their answers are gone and the total hid it")

    def test_the_patches_were_walked(self):
        files = {target for _, target in RC}
        self.assertGreaterEqual(
            len(files), FLOORS["rc_files"],
            f"strings found added to only {len(files)} .rc files: the walk "
            f"broke, and the defect this file exists for would go unreported: "
            f"{sorted(files)}")
        self.assertTrue(
            RC_ENGLISH,
            "no English string found added to any .rc: every check below is "
            "asserting about an empty set")

    def test_the_french_msgids_were_read(self):
        self.assertGreaterEqual(
            len(FRENCH_MSGIDS), FLOORS["french_msgids"],
            f"only {len(FRENCH_MSGIDS)} msgids read from "
            f"{PO_PATCH.name}: the reader broke, and strings translated THERE "
            f"would be reported as untranslated")

    def test_the_text_filter_can_tell_text_from_the_rest(self):
        # If is_text() said no to everything, every check below would go
        # quiet -- the dangerous direction.
        self.assertTrue(is_text("Setting up Windows programs. This can take a minute."))
        self.assertTrue(is_text("Buffer size"))
        # "%u samples (%s ms)" IS text and does have French in the ASIO
        # panel ("%u échantillons (%s ms)"): a string is not plumbing
        # just because it carries placeholders. Asserting the opposite
        # here is what caught that out while this file was written.
        self.assertTrue(is_text("%u samples (%s ms)"))
        self.assertFalse(is_text("%u/%s"))
        self.assertFalse(is_text("MS Shell Dlg"))
        self.assertFalse(is_text("stream.capture.sink"))
        self.assertTrue(
            [i for i in RC_ENGLISH if is_text(i["value"])],
            "no added .rc string looks like text at all: is_text() rejects "
            "everything and this file is asserting about an empty set")

    def test_the_french_language_block_is_seen(self):
        # patches/audio/0003-asio-driver.patch carries one. If the reader
        # stopped finding it, the ASIO panel's French would be reported
        # missing; if it thought every file had one, nothing ever would.
        with_block = sorted(t for (_, t), e in RC.items() if e["has_french_block"])
        self.assertTrue(
            with_block,
            "no added .rc seen to carry a LANGUAGE LANG_FRENCH block: the "
            "reader broke, and a patch that ships its own French in the "
            "resource would be reported as untranslated")
        without = [t for (_, t), e in RC.items() if not e["has_french_block"]]
        self.assertTrue(
            without, "every patched .rc looks as if it had a French block: the "
                     "reader latched on and this file now approves everything")
        self.assertTrue(
            RC_FRENCH, "no string read out of a French block: the block is "
                       "found but its contents are not, so the count check "
                       "below is comparing against nothing")


class Delegated(unittest.TestCase):
    """The two halves this file does not do itself.

    A delegation nobody asserts is a hole: the day one of those files
    stops doing its half, nothing fails, and this file's docstring still
    claims it was covered."""

    def test_test_names_still_walks_what_the_patches_add(self):
        self.assertTrue(
            NAMES_TEST.is_file(),
            "tests/test_names.py is gone: the corpus this file imports its "
            "walk from no longer exists, and the other question asked of that "
            "corpus -- does the string name the upstream project? -- is now "
            "asked by nothing")
        names = load(NAMES_TEST, "names_for_i18n")
        self.assertTrue(
            hasattr(names, "WhatThePatchesAdd"),
            "test_names.py no longer has WhatThePatchesAdd: this file's import "
            "of its walk is broken")
        walked = list(names.WhatThePatchesAdd().added_strings())
        self.assertGreaterEqual(
            len(walked), FLOORS["added_strings"],
            f"test_names.py's walk returned {len(walked)} strings: it is no "
            f"longer reading the patches, so neither question is being asked")

    def test_test_tree_still_checks_the_shape_of_the_table(self):
        self.assertTrue(
            TREE_TEST.is_file(),
            "tests/test_tree.py is gone: nothing checks that names-fr.tsv has "
            "three columns per row, keeps its %s count or never brings the old "
            "name back")
        text = TREE_TEST.read_text(encoding="utf-8")
        # The name has to END there: `assertIn("class FrenchTable")` passed
        # happily on `class FrenchTableGone`, because one is a prefix of
        # the other. Measured while proving this could go red, 2026-11-06
        # -- the same shape as the Messages.sh check in the files
        # repository, where the word survived in an error message.
        self.assertRegex(
            text, r"(?m)^class FrenchTable\b",
            "test_tree.py lost class FrenchTable: the shape of names-fr.tsv -- "
            "three columns, no old name, the same escapes on both sides -- is "
            "now checked by nothing, and this file only checks that the French "
            "says something")

    def test_po_rename_is_what_applies_the_table(self):
        self.assertTrue(
            (ROOT / "tools" / "po-rename.py").is_file(),
            "tools/po-rename.py is gone: names-fr.tsv is then a table nothing "
            "applies, and every French string in it is dead data")
        self.assertIn(
            "po-rename.py", (ROOT / "README.md").read_text(encoding="utf-8"),
            "README.md no longer says the French hunks come from po-rename.py: "
            "the next person to move the release will redo them by hand")


class TheTable(unittest.TestCase):
    """tools/names-fr.tsv: the French of a renamed string."""

    def test_every_row_has_french(self):
        missing = [r[0][:60] for r in ROWS if len(r) == 3 and not r[2].strip()]
        self.assertEqual(
            [], sorted(missing),
            f"{len(missing)} row(s) of names-fr.tsv with an empty French "
            f"column. po-rename.py would write an empty msgstr, which gettext "
            f"reads as untranslated, and the string would show in English:\n"
            + "\n".join("    " + m for m in sorted(missing)))

    def test_french_is_not_just_the_english_copied_over(self):
        same = [r[1] for r in ROWS
                if len(r) == 3 and r[1] == r[2] and r[1] not in SAME_IN_BOTH]
        self.assertEqual(
            [], sorted(same),
            f"{len(same)} row(s) whose French column is the English copied "
            f"over. If the string really has no French -- a product name -- "
            f"add it to SAME_IN_BOTH with the reason; otherwise translate it:\n"
            + "\n".join("    " + s for s in sorted(same)))

    def test_the_allowed_pairs_are_still_in_the_table(self):
        # An exemption for a row that no longer exists is an exemption
        # that will one day excuse a different string with the same text.
        gone = sorted(set(SAME_IN_BOTH) - NEW_ENGLISH)
        self.assertEqual(
            [], gone,
            f"SAME_IN_BOTH names {len(gone)} string(s) that are no longer in "
            f"the table; drop them from it:\n" + "\n".join("    " + g for g in gone))


class TheTitles(unittest.TestCase):
    """release/windows/titles: the answer shown before a download.

    These are the only strings of this layer a person reads BEFORE
    deciding to wait for forty gigabytes, so an English-only one is the
    most expensive kind here."""

    def test_every_title_has_both_languages(self):
        missing = []
        for (where, name), record in sorted(TITLE_RECORDS.items()):
            for key in SHOWN:
                if not record.get(key, "").strip():
                    missing.append(f"{where} [{name}]: {key} is empty or absent")
        self.assertEqual(
            [], missing,
            f"{len(missing)} title field(s) missing. why_fr is what a French "
            f"session shows in front of the download:\n"
            + "\n".join("    " + m for m in missing))

    def test_no_title_has_the_english_copied_into_the_french(self):
        same = [f"{where} [{name}]: {record['why_fr'][:60]!r}"
                for (where, name), record in sorted(TITLE_RECORDS.items())
                if record.get("why_en", "").strip()
                and record.get("why_en", "").strip() == record.get("why_fr", "").strip()]
        self.assertEqual(
            [], same,
            f"{len(same)} title(s) whose why_fr is why_en copied over, which is "
            f"what happens when a title is added in a hurry. Measured on the "
            f"255 titles shipped today: none:\n"
            + "\n".join("    " + s for s in same))


class WhatThePatchesAdd(unittest.TestCase):
    """🚨 The defect this file exists for: a NEW user-visible string
    that no catalogue is short of, because no catalogue has heard of it."""

    def test_every_added_resource_string_has_a_path_to_french(self):
        bad = []
        for (patch, target), entry in sorted(RC.items()):
            for item in entry["english"]:
                if not is_text(item["value"]) or has_a_path_to_french(item, entry):
                    continue
                bad.append(f"{patch}:{item['line']} {target}: "
                           f"{item['value'][:70]!r}")
        self.assertEqual(
            [], sorted(set(bad)),
            f"{len(set(bad))} string(s) a patch adds to a resource with no way "
            f"of being French. One of three: put the row in "
            f"tools/names-fr.tsv (only if the release already has the old "
            f"string -- po-rename.py refuses it otherwise), add the msgid and "
            f"msgstr to {PO_PATCH.name}, or give the .rc its own LANGUAGE "
            f"LANG_FRENCH block:\n" + "\n".join("    " + b for b in sorted(set(bad))))

    def test_a_french_block_answers_every_english_entry_of_its_file(self):
        """A file with its own French block is trusted by the check
        above, so the block has to keep up with the English one.

        The way that goes wrong is ordinary: somebody adds an LTEXT to
        the dialog, in the English block, and does not copy it down. The
        file still has a French block, the check above still says yes,
        and the new label is English in a French session. So the two
        blocks are counted against each other -- the control ids are the
        same on both sides by construction, so the number of strings
        that carry text has to match."""
        wrong = []
        for (patch, target), entry in sorted(RC.items()):
            if not entry["has_french_block"]:
                continue
            english = [i for i in entry["english"] if is_text(i["value"])]
            french = [i for i in entry["french"] if is_text(i["value"])]
            if len(english) != len(french):
                wrong.append(
                    f"{patch} {target}: {len(english)} English string(s) and "
                    f"{len(french)} French one(s)"
                    + ("" if len(english) <= len(french) else
                       " -- something was added to the English block and not "
                       "copied into the French one"))
        self.assertEqual(
            [], sorted(wrong),
            f"{len(wrong)} resource file(s) whose two language blocks do not "
            f"match:\n" + "\n".join("    " + w for w in sorted(wrong)))

    def test_every_added_desktop_key_has_french(self):
        # loader/wine.desktop is what the desktop shows for a bare .exe.
        # check-names.sh guards its Name= against the other defect; its
        # Name[fr]= is guarded here.
        keys = ("Name", "GenericName", "Comment")
        added, missing = {}, []
        for patch in sorted(PATCHES.glob("*/*.patch")):
            text = patch.read_text(encoding="utf-8", errors="replace")
            target = None
            for line in text.splitlines():
                head = re.match(r"^\+\+\+ b/(.*)", line)
                if head:
                    target = head.group(1)
                    continue
                if target is None or not target.endswith(".desktop"):
                    continue
                entry = re.match(r"^\+(%s)(\[([a-z_]+)\])?\s*=\s*(\S.*)$"
                                 % "|".join(keys), line)
                if entry:
                    added.setdefault((target, entry.group(1)), set()).add(
                        entry.group(3) or "")
        self.assertTrue(
            added, "no .desktop key found added by any patch: the walk broke, "
                   "and a launcher entry with no French would pass")
        for (target, key), languages in sorted(added.items()):
            if "" in languages and "fr" not in languages:
                missing.append(f"{target}: {key}= is added with no {key}[fr]=")
        self.assertEqual(
            [], sorted(missing),
            f"{len(missing)} desktop key(s) added in English only:\n"
            + "\n".join("    " + m for m in sorted(missing)))


class BothLanguagesInTheCode(unittest.TestCase):
    """The two user-visible strings a patch adds in C rather than in a
    resource. They cannot go through po/fr.po -- the unix side of
    winepulse has no access to the Windows UI language -- so the patch
    asks the locale itself and carries both languages inline.

    That is a translation, and it is the only one in this tree that
    nothing else would notice losing: delete the French branch and the
    code still compiles, still runs, and quietly says "Default output"
    to everyone."""

    def patch_text(self):
        path = PATCHES / "audio" / "0001-default-device-names.patch"
        self.assertTrue(
            path.is_file(),
            f"{path.name} is gone: the default sound devices are named after "
            f"the sound server again, in English")
        return path.read_text(encoding="utf-8", errors="replace")

    def test_the_default_device_names_still_have_their_french(self):
        text = self.patch_text()
        self.assertIn(
            "locale_is_french", text,
            "the patch no longer asks the locale: the device names are now one "
            "language for everybody")
        for english, french in (("Default output", "Sortie par défaut"),
                                ("Default input", "Entrée par défaut")):
            self.assertIn(f'"{english}"', text, f"{english!r} is gone")
            self.assertIn(f'"{french}"', text,
                          f"{english!r} lost its French ({french!r}): a French "
                          f"session now sees the English name")

    def test_stereo_mix_still_has_its_french(self):
        path = PATCHES / "audio" / "0002-stereo-mix.patch"
        self.assertTrue(path.is_file(), f"{path.name} is gone")
        text = path.read_text(encoding="utf-8", errors="replace")
        self.assertIn('"Stereo Mix"', text, "the loopback device is gone")
        self.assertIn(
            '"Mixage stéréo"', text,
            "the loopback device lost its French: every program's recording "
            "list shows it, and it would be the one English name among the "
            "French ones")


if __name__ == "__main__":
    unittest.main()
