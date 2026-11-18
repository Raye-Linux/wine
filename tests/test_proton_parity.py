# SPDX-License-Identifier: LGPL-2.1-or-later
"""What the layer Steam runs games with has, and this one now has too.

    python3 -I -B -m unittest discover -s tests

Read from the patches; whether a 32-bit program really gets 4 GB is seen
with tests/samples/laaprobe.c in the test VM.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True


def added(name):
    text = (ROOT / "patches" / "base" / name).read_text(encoding="utf-8")
    return "\n".join(l[1:] for l in text.splitlines() if l.startswith("+") and not l.startswith("+++"))


def removed(name):
    text = (ROOT / "patches" / "base" / name).read_text(encoding="utf-8")
    return "\n".join(l[1:] for l in text.splitlines() if l.startswith("-") and not l.startswith("---"))


class LargeAddressAware(unittest.TestCase):
    PATCH = "0019-large-address-aware.patch"

    def test_in_the_series(self):
        self.assertIn(self.PATCH, (ROOT / "patches" / "base" / "series").read_text().split())

    def test_reads_the_variable_and_still_honours_the_header(self):
        a = added(self.PATCH)
        self.assertIn('getenv( "WINE_LARGE_ADDRESS_AWARE" )', a)
        self.assertIn("IMAGE_FILE_LARGE_ADDRESS_AWARE) return TRUE", a)

    def test_only_exactly_one_turns_it_on(self):
        # "0", empty or anything else must leave the 2 GB limit alone
        self.assertRegex(added(self.PATCH), r"env\[0\] == '1' && !env\[1\]")

    def test_both_the_wow64_and_the_32_bit_paths_use_it(self):
        a, r = added(self.PATCH), removed(self.PATCH)
        self.assertEqual(r.count("IMAGE_FILE_LARGE_ADDRESS_AWARE"), 2)
        self.assertEqual(len(re.findall(r"large_address_aware\(\)", a)), 2)


if __name__ == "__main__":
    unittest.main()
