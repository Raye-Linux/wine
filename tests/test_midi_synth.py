# SPDX-License-Identifier: LGPL-2.1-or-later
"""MIDI with nothing plugged in (audio/0010): Raye's synth is the first MIDI
output, like the synth Windows ships, and starts when a program opens it.

    python3 -I -B -m unittest discover -s tests
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
AUDIO = ROOT / "patches" / "audio"
PATCH = AUDIO / "0010-built-in-synth.patch"


def added():
    return "\n".join(l[1:] for l in PATCH.read_text().splitlines()
                     if l.startswith("+") and not l.startswith("+++"))


class BuiltInSynth(unittest.TestCase):
    def test_patch_is_applied_after_the_port_names(self):
        series = [l.strip() for l in (AUDIO / "series").read_text().splitlines()
                  if l.strip() and not l.startswith("#")]
        self.assertIn(PATCH.name, series)
        self.assertLess(series.index("0008-midi-port-names.patch"), series.index(PATCH.name))

    def test_name_and_starter(self):
        text = added()
        self.assertIn('synth_name[] = "Raye Synth"', text)
        # the program raye-base ships for it
        self.assertIn('synth_starter[] = "/usr/libexec/raye-synth"', text)
        self.assertNotRegex(text, r'"[^"]*\b(Wine|WINE|FluidSynth|FLUID)\b[^"]*"')

    def test_listed_first_and_started_on_open(self):
        text = added()
        self.assertIn("if (builtin) synth_add();", text)
        self.assertLess(text.index("if (builtin) synth_add();"),
                        text.index("is_hidden_client(cinfo) || (builtin && is_synth_client(cinfo))"))
        self.assertIn("synth_start();", text)
        self.assertIn("synth_find(midi_seq, &dest->addr)", text)

    def test_every_message_path_knows_the_synth(self):
        # open, close, short messages and SysEx (a GS reset is SysEx)
        self.assertEqual(len(re.findall(r"^\+    case MOD_SWSYNTH:$", PATCH.read_text(), re.M)), 4)

    def test_described_like_windows_synth(self):
        text = added()
        self.assertIn("dest->caps.wTechnology = MOD_SWSYNTH;", text)
        self.assertIn("dest->caps.wVoices = 32;", text)
        self.assertIn("MIDICAPS_VOLUME | MIDICAPS_LRVOLUME", text)


if __name__ == "__main__":
    unittest.main()
