# SPDX-License-Identifier: LGPL-2.1-or-later
"""The doc's anti-cheat tables come from the titles files, not by hand."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "titles-doc.py"


class TitlesDoc(unittest.TestCase):
    def run_tool(self, games):
        with tempfile.NamedTemporaryFile("w", suffix=".json") as f:
            json.dump(games, f)
            f.flush()
            return subprocess.run([sys.executable, "-I", "-B", str(TOOL), f.name],
                                  capture_output=True, text=True, check=True).stdout

    def test_blocked_titles_keep_their_reason_and_source(self):
        out = self.run_tool([])
        row = next(l for l in out.splitlines() if l.startswith("| Fortnite |"))
        self.assertIn("Easy Anti-Cheat", row)
        self.assertIn("Epic", row)
        self.assertIn("6c26305", row)

    def test_a_title_with_no_anti_cheat_is_not_listed_as_blocked_by_one(self):
        out = self.run_tool([])
        by_cheat = out.split("Bloqués pour une autre raison")[0]
        self.assertNotIn("| Minecraft for Windows", by_cheat)
        self.assertIn("| Minecraft for Windows", out)

    def test_a_steam_number_moves_a_title_to_the_steam_table(self):
        before = self.run_tool([])
        after = self.run_tool([{"slug": "100-orange-juice", "storeIds": {"steam": "282800"}}])
        steam_part = lambda out: out.split("vendus sur Steam")[1].split("sans numéro Steam")[0]
        self.assertNotIn("100% Orange Juice", steam_part(before))
        self.assertIn("store.steampowered.com/app/282800", steam_part(after))
