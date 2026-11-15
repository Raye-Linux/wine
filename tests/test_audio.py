# SPDX-License-Identifier: LGPL-2.1-or-later
"""Raye ASIO and the PipeWire setting it relies on.

A host such as FL Studio opens its project at 44.1 kHz. The driver can
only move PipeWire's graph to a rate the graph allows, so the rates the
driver offers and the rates the shipped setting allows are one list,
written twice: here it is checked that they stay the same.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
CONF = ROOT / "release" / "pipewire" / "50-raye-rates.conf"
PATCH = ROOT / "patches" / "audio" / "0009-asio-sample-rate.patch"


def conf_rates():
    text = "\n".join(l.split("#", 1)[0] for l in CONF.read_text().splitlines())
    m = re.search(r"default\.clock\.allowed-rates\s*=\s*\[([^\]]*)\]", text)
    return sorted(int(x) for x in m.group(1).split()) if m else []


def driver_rates():
    added = "\n".join(l[1:] for l in PATCH.read_text().splitlines() if l.startswith("+"))
    m = re.search(r"graph_rates\[\]\s*=\s*\{([^}]*)\}", added)
    return sorted(int(x) for x in m.group(1).replace(",", " ").split()) if m else []


class AsioRates(unittest.TestCase):
    def test_44100_is_offered(self):
        self.assertIn(44100, driver_rates())
        self.assertIn(44100, conf_rates())

    def test_driver_and_setting_agree(self):
        self.assertEqual(driver_rates(), conf_rates())

    def test_48000_stays_allowed(self):
        self.assertIn(48000, conf_rates())

    def test_patch_is_in_the_series(self):
        series = (ROOT / "patches" / "audio" / "series").read_text().split()
        self.assertIn(PATCH.name, series)

    def test_setting_is_installed(self):
        rules = (ROOT / "debian" / "rules").read_text()
        self.assertIn("release/pipewire/50-raye-rates.conf", rules)
        self.assertIn("/usr/share/pipewire/pipewire.conf.d/", rules)

    def test_jack_library_is_a_dependency(self):
        # the driver dlopens PipeWire's libjack; the image skips Recommends
        control = (ROOT / "debian" / "control").read_text()
        depends = re.search(r"^Package: raye-wine\n.*?^Depends:(.*?)^\S", control, re.M | re.S).group(1)
        self.assertIn("pipewire-jack", depends)


class MediaCodecs(unittest.TestCase):
    def test_gstreamer_plugins_are_dependencies(self):
        control = (ROOT / "debian" / "control").read_text()
        depends = re.search(r"^Package: raye-wine\n.*?^Depends:(.*?)^\S", control, re.M | re.S).group(1)
        for name in ("gstreamer1.0-plugins-base", "gstreamer1.0-plugins-good",
                     "gstreamer1.0-plugins-ugly", "gstreamer1.0-libav"):
            self.assertIn(name, depends)


if __name__ == "__main__":
    unittest.main()
