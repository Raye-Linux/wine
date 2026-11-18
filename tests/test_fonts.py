# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fonts in a new space: the font names Windows programs ask for mapped to
fonts Raye ships, so text is the right kind and can be smoothed.

    python3 -I -B -m unittest discover -s tests

Without these, text a program sets in MS Sans Serif comes out in a 1-bit
bitmap font that nothing smooths (WinRAR's installer licence, old Visual
Basic programs), and Courier New, Consolas or Times New Roman all come
out as Liberation Sans, so a code editor or a terminal gets a
proportional font.

Read from the patch the way wine.inf reads it; the result inside a real
space is checked with tests/samples/fontprobe.c in the test VM.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PATCH = ROOT / "patches" / "base" / "0018-fonts-in-every-space.patch"
sys.dont_write_bytecode = True

REPLACEMENTS = r"HKCU,Software\Wine\Fonts\Replacements"
DESKTOP = r"HKCU,Control Panel\Desktop"
MULTI_SZ_NOCLOBBER = 0x10002
DWORD_NOCLOBBER = 0x10003

# Families the image has, and the package that brings each. A target that
# is not in here would map a name to nothing at all.
SHIPPED = {
    "Liberation Sans": "fonts-liberation",
    "Liberation Serif": "fonts-liberation",
    "Liberation Mono": "fonts-liberation",
    "Carlito": "fonts-crosextra-carlito",
    "Caladea": "fonts-crosextra-caladea",
    "Cascadia Mono": "fonts-cascadia-code",
    "Inter": "fonts-inter",
    "DejaVu Sans": "fonts-dejavu-core",
    "DejaVu Serif": "fonts-dejavu-core",
    "DejaVu Sans Mono": "fonts-dejavu-core",
    "Noto Sans": "fonts-noto-core",
    "Noto Sans Symbols2": "fonts-noto-core",
    "Noto Sans Symbols": "fonts-noto-core",
    "Noto Sans CJK SC": "fonts-noto-cjk",
    "Noto Sans CJK TC": "fonts-noto-cjk",
    "Noto Sans CJK JP": "fonts-noto-cjk",
    "Noto Sans CJK KR": "fonts-noto-cjk",
    "Noto Sans Mono CJK JP": "fonts-noto-cjk",
}
MONOSPACE = {"Liberation Mono", "Cascadia Mono", "DejaVu Sans Mono", "Noto Sans Mono CJK JP"}
SERIF = {"Liberation Serif", "Caladea", "DejaVu Serif"}


def added_lines():
    text = PATCH.read_text(encoding="utf-8")
    return [l[1:] for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")]


def inf_fields(line):
    # root,key,"name",flags,"v1","v2"... the way setupapi splits a line
    return [f.strip().strip('"') for f in re.findall(r'"[^"]*"|[^,]+|(?<=,)(?=,|$)', line)]


def replacements():
    out = {}
    for line in added_lines():
        if line.startswith(REPLACEMENTS + ","):
            f = inf_fields(line[len(REPLACEMENTS) + 1:])
            out[f[0]] = (int(f[1], 16), f[2:])
    return out


def desktop():
    out = {}
    for line in added_lines():
        if line.startswith(DESKTOP + ","):
            f = inf_fields(line[len(DESKTOP) + 1:])
            out[f[0]] = (int(f[1], 16) if f[1] else 0, f[2])
    return out


class FontsInEverySpace(unittest.TestCase):
    def test_the_patch_is_in_the_series(self):
        names = (ROOT / "patches" / "base" / "series").read_text().split()
        self.assertIn(PATCH.name, names)

    def test_the_sections_are_installed(self):
        added = "\n".join(added_lines())
        self.assertIn("    RayeFonts,\\", added)
        self.assertRegex(added, r"(?m)^\[RayeFonts\]$")

    def test_smoothing_is_left_to_fontconfig(self):
        # the font code takes each face's smoothing from fontconfig, which
        # follows Plasma's font settings; wine.inf must not fight that
        self.assertEqual(desktop(), {})
        self.assertNotIn("sysparams.c", PATCH.read_text(encoding="utf-8"))

    def test_a_choice_made_in_the_space_is_kept(self):
        for name, (flags, _) in list(replacements().items()) + list(desktop().items()):
            self.assertTrue(flags & 0x2, "%s would overwrite the person's choice on update" % name)

    def test_every_target_is_a_font_the_image_ships(self):
        for name, (flags, targets) in replacements().items():
            self.assertEqual(flags, MULTI_SZ_NOCLOBBER, name)
            self.assertTrue(targets, name)
            for t in targets:
                self.assertIn(t, SHIPPED, "%s -> %s, a font Raye does not ship" % (name, t))

    def test_the_names_programs_ask_for_most(self):
        r = replacements()
        for name in ("Arial", "Times New Roman", "Courier New", "Calibri", "Cambria",
                     "Consolas", "Microsoft Sans Serif", "Segoe UI", "Verdana",
                     "Microsoft YaHei", "MS Gothic", "Malgun Gothic"):
            self.assertIn(name, r)

    def test_monospace_stays_monospace(self):
        r = replacements()
        for name in ("Courier New", "Consolas", "Lucida Console", "Cascadia Mono"):
            if name in r:
                for t in r[name][1]:
                    self.assertIn(t, MONOSPACE, "%s -> %s" % (name, t))

    def test_serif_stays_serif(self):
        r = replacements()
        for name in ("Times New Roman", "Cambria", "Georgia"):
            self.assertIn(r[name][1][0], SERIF, name)

    def test_metric_twins_first(self):
        # same widths as the font asked for, so text fits where it was laid out
        r = replacements()
        self.assertEqual(r["Arial"][1][0], "Liberation Sans")
        self.assertEqual(r["Times New Roman"][1][0], "Liberation Serif")
        self.assertEqual(r["Courier New"][1][0], "Liberation Mono")
        self.assertEqual(r["Calibri"][1][0], "Carlito")
        self.assertEqual(r["Cambria"][1][0], "Caladea")

    def test_dialog_fonts_are_left_alone(self):
        # MS Shell Dlg and Tahoma keep the layer's own Tahoma, whose metrics
        # old dialogs were laid out for
        r = replacements()
        for name in ("MS Shell Dlg", "MS Shell Dlg 2", "Tahoma"):
            self.assertNotIn(name, r)

    def test_ms_sans_serif_goes_to_tahoma_and_its_bitmaps_are_left_out(self):
        added = "\n".join(added_lines())
        self.assertIn(r'HKLM,%FontSubStr%,"MS Sans Serif",0x2,"Tahoma"', added)
        self.assertIn(r'HKLM,%FontSubStr%,"MS Sans Serif Regular",0x2,"Tahoma"', added)
        rules = (ROOT / "debian" / "rules").read_text()
        for pattern in ("sserif*.fon", "ssee*.fon", "ssef*.fon"):
            self.assertIn("share/wine/fonts/" + pattern, rules)
        # the other bitmap fonts stay: System, Courier and Small Fonts
        self.assertNotIn("vgasys", rules)

    def test_the_package_brings_the_metric_twins(self):
        control = (ROOT / "debian" / "control").read_text()
        block = control.split("Package: raye-wine\n", 1)[1].split("\n\n", 1)[0]
        deps = block.split("Description:", 1)[0]
        for pkg in ("fonts-liberation", "fonts-crosextra-carlito", "fonts-crosextra-caladea"):
            self.assertIn(pkg, deps)


if __name__ == "__main__":
    unittest.main()
