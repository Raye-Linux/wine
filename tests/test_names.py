# SPDX-License-Identifier: LGPL-2.1-or-later
"""The upstream project's name must never reach a user:

    python3 -I -B -m unittest discover -s tests

tools/check-names.sh already guards the patched Wine tree, but it only
runs inside the package build: it needs the upstream tarball and mingw,
takes three quarters of an hour, and `[ -f dlls/appwiz.cpl/addons.c ]`
makes it exit 2 anywhere else. So nothing has been watching the files
THIS repository ships of its own: the package description apt and the
Store show, the profile names a dialog prints, the one-line reasons the
Store puts in front of a download, and the lines a patch ADDS.

That last one matters most. A patch that adds a user-visible string with
the name in it passes every test here today and is only caught by a full
build, long after it was written.

What is deliberately NOT a leak, and why -- the same kind of list
check-names.sh keeps:

- comments. Nobody reads them but us.
- debian/changelog. A maintainer's record has to be able to say what it
  was built from; that is its job.
- README and *.md. Developer documentation.
- Vcs-Git and other URLs. A repository address is not a sentence.
- the lowercase token `raye-wine`: the package's own name and the folder
  it lives in. Changing those is not a string fix, and a user who reads
  a path that deep has already left the interface behind.
- WINEPREFIX and friends: environment variables, never shown.
- the first column of tools/names-fr.tsv, which is BY DEFINITION the old
  string being replaced. (Columns two and three are guarded by
  test_tree.FrenchTable.)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True

NAME = re.compile(r"\bWine\b")

# Things that contain the letters but are not the name shown to a user.
ALLOWED = (
    re.compile(r"raye-wine"),            # the package, and /opt/raye-wine
    re.compile(r"raye_wine"),            # the same, as a C or shell name
    re.compile(r"\bWINE[A-Z_]*\b"),      # WINEPREFIX, WINEDLLOVERRIDES...
    re.compile(r"WINE_(TRACE|ERR|WARN|FIXME|DEFAULT)"),
    re.compile(r"Software.{1,4}Wine"),   # registry paths, not sentences
    re.compile(r"VER_FILEDESCRIPTION_STR"),   # programs read it to spot builtins
    re.compile(r"#msgctxt#do not translate#"),
)


def leaks(text):
    """The name, as a word, in something a user reads. [] when clean.

    Only the words left after the allowed tokens are removed count, so
    `/opt/raye-wine` is silent and `Wine Mono` is not.
    """
    stripped = text
    for allowed in ALLOWED:
        stripped = allowed.sub(" ", stripped)
    return NAME.findall(stripped)


def quoted(line):
    """The contents of the quoted strings in a line of C, .rc or shell."""
    return re.findall(r'"([^"]*)"', line)


def is_comment(line):
    return re.match(r'^\+?\s*(#|//|/?\*|;)', line) is not None


class TheDetector(unittest.TestCase):
    """Proof the check can go red. Without these it only proves silence."""

    def test_it_catches_the_name_in_a_sentence(self):
        self.assertEqual(leaks(".NET (Wine Mono) and web views"), ["Wine"])
        self.assertEqual(leaks("Could not start Wine"), ["Wine"])
        self.assertEqual(len(leaks("Wine Mono and Wine Gecko")), 2)

    def test_it_stays_quiet_for_the_package_and_its_folder(self):
        self.assertEqual(leaks("kept in /opt/raye-wine so it never clashes"), [])
        self.assertEqual(leaks("raye-wine (11.21-1~raye1) resolute"), [])
        self.assertEqual(leaks('app.pApplicationName = "raye-wine-gpu";'), [])

    def test_it_stays_quiet_for_things_no_user_sees(self):
        self.assertEqual(leaks('WINEDLLOVERRIDES="winex11.drv=d"'), [])
        self.assertEqual(leaks('WINE_TRACE("could not load\\n");'), [])
        self.assertEqual(leaks(r'"Software\\Wine\\Drivers"'), [])

    def test_it_is_not_fooled_by_a_longer_word(self):
        # "Winetricks" and "Winery" are not the name; "wine" lowercase on
        # its own is the command, which the layer never prints either, but
        # the word boundary is what this asserts.
        self.assertEqual(leaks("Winetricks installs it"), [])
        self.assertEqual(leaks("a glass of wine"), [])

    def test_the_lowercase_runtime_error_shape_is_caught_where_it_matters(self):
        # "wine: could not load" is lowercase, so NAME alone misses it.
        # It can only come from upstream's loader, which tools/check-names.sh
        # guards in the patched tree; this records that the split is known
        # and intended, so nobody later assumes this file covers it.
        self.assertEqual(leaks("wine: could not load kernel32.dll"), [])
        self.assertIn("loader/wine.desktop",
                      (ROOT / "tools" / "check-names.sh").read_text(encoding="utf-8"),
                      "check-names.sh no longer guards the loader, and nothing "
                      "else does: the lowercase shape is now uncovered")

    def test_device_and_codec_names_are_guarded(self):
        # games list the input devices by name, audio programs the codecs
        script = (ROOT / "tools" / "check-names.sh").read_text(encoding="utf-8")
        for path in ("dlls/dinput/mouse.c", "dlls/dinput/keyboard.c", "dlls/msacm32/pcmconverter.c",
                     "dlls/msadp32.acm/msadp32.c", "dlls/msg711.acm/msg711.c",
                     "dlls/l3codeca.acm/mpegl3.c", "dlls/msgsm32.acm/msgsm32.c",
                     "dlls/imaadp32.acm/imaadp32.c", "dlls/ddraw/ddraw.c", "dlls/msvcrt/exit.c",
                     "dlls/wbemprox/builtin.c", "dlls/winealsa.drv/alsamidi.c"):
            self.assertIn(path, script)
        series = (ROOT / "patches" / "base" / "series").read_text(encoding="utf-8")
        self.assertIn("0014-device-names.patch", series)
        self.assertIn("0015-codec-and-device-names.patch", series)
        audio = (ROOT / "patches" / "audio" / "series").read_text(encoding="utf-8")
        self.assertIn("0008-midi-port-names.patch", audio)

    def test_codec_copyright_is_gone(self):
        # five codecs said "Brought to you by the Wine team..."
        patch = (ROOT / "patches" / "base" / "0015-codec-and-device-names.patch").read_text(encoding="utf-8")
        removed = [l for l in patch.splitlines() if l.startswith("-") and "Brought to you" in l]
        added = [l for l in patch.splitlines() if l.startswith("+") and "Brought to you" in l]
        self.assertEqual((len(removed), added), (5, []))


class ThePackageDescription(unittest.TestCase):
    """apt, the Store and every package browser show this text."""

    def description(self):
        text = (ROOT / "debian" / "control").read_text(encoding="utf-8")
        # dpkg drops comment lines; nobody sees them
        text = "\n".join(l for l in text.splitlines() if not l.startswith("#"))
        block = text.split("\nDescription:", 1)
        self.assertEqual(len(block), 2, "debian/control has no Description")
        return "Description:" + block[1]

    def test_it_does_not_name_the_upstream_project(self):
        found = leaks(self.description())
        self.assertEqual(found, [], "debian/control's Description names the "
                                    "upstream project %d time(s); apt and the "
                                    "Store show this text" % len(found))

    def test_no_other_user_facing_control_field_names_it(self):
        text = (ROOT / "debian" / "control").read_text(encoding="utf-8")
        for line in text.splitlines():
            if line.startswith(("Vcs-", "#")) or "://" in line:
                continue
            self.assertEqual(leaks(line), [], "debian/control: %r" % line)


class WhatTheStoreSays(unittest.TestCase):
    """The names and reasons shown before a download."""

    def test_no_profile_name_names_it(self):
        for path in sorted((ROOT / "release" / "windows" / "profiles").glob("*.profile")):
            for line in path.read_text(encoding="utf-8").splitlines():
                if is_comment(line):
                    continue
                for text in quoted(line):
                    self.assertEqual(leaks(text), [], "%s: %r" % (path.name, text))

    def test_no_title_name_or_reason_names_it(self):
        titles = ROOT / "release" / "windows" / "titles"
        shown = ("name", "why_en", "why_fr")
        for path in sorted(titles.glob("*.titles")):
            for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if line.startswith("#"):
                    continue
                key, _, value = line.partition("=")
                if key.strip() in shown:
                    self.assertEqual(leaks(value), [], "%s:%d %r" %
                                     (path.name, n, line))

    def test_the_reader_shows_nothing_of_it(self):
        reader = ROOT / "release" / "windows" / "raye-windows-titles"
        for n, line in enumerate(reader.read_text(encoding="utf-8").splitlines(), 1):
            if is_comment(line):
                continue
            for text in quoted(line):
                self.assertEqual(leaks(text), [], "raye-windows-titles:%d %r" %
                                 (n, text))


class WhatThePatchesAdd(unittest.TestCase):
    """The lines a patch ADDS, which only a full build sees otherwise."""

    def added_strings(self):
        for path in sorted((ROOT / "patches").glob("*/*.patch")):
            for n, line in enumerate(path.read_text(encoding="utf-8",
                                                    errors="replace").splitlines(), 1):
                if not line.startswith("+") or line.startswith("+++"):
                    continue
                if is_comment(line):
                    continue
                for text in quoted(line):
                    yield path, n, line, text

    def test_no_added_user_visible_string_names_it(self):
        bad = []
        for path, n, line, text in self.added_strings():
            if leaks(text):
                bad.append("%s/%s:%d %s" % (path.parent.name, path.name, n,
                                            line.strip()[:100]))
        self.assertEqual(bad, [], "a patch adds the upstream name to a string a "
                                  "user reads:\n  " + "\n  ".join(bad))

    def test_this_check_is_actually_looking_at_something(self):
        # A green that scanned nothing is not a green. The patches add
        # thousands of strings; if this drops to a handful, the walk broke.
        self.assertGreater(sum(1 for _ in self.added_strings()), 200)


if __name__ == "__main__":
    unittest.main()
