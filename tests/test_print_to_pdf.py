# SPDX-License-Identifier: LGPL-2.1-or-later
"""What a Windows program prints to Raye's Print to PDF printer (base/0026):
the creator line of the PostScript becomes the PDF's "Creator", which every
PDF viewer shows.

    python3 -I -B -m unittest discover -s tests
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = ROOT / "patches" / "base"
PATCH = BASE / "0026-print-creator.patch"


class Creator(unittest.TestCase):
    def test_patch_is_applied(self):
        series = [l.strip() for l in (BASE / "series").read_text().splitlines()
                  if l.strip() and not l.startswith("#")]
        self.assertIn(PATCH.name, series)

    def test_creator_names_raye(self):
        lines = PATCH.read_text().splitlines()
        self.assertIn('-"%%%%Creator: Wine PostScript Driver\\n"', lines)
        self.assertIn('+"%%%%Creator: Raye\\n"', lines)


if __name__ == "__main__":
    unittest.main()
