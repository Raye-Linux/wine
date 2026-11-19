# SPDX-License-Identifier: LGPL-2.1-or-later
"""The hardware a Windows program sees, as on Windows:

    python3 -I -B -m unittest discover -s tests

- device and maker names in WMI and in the device installation files
  (base/0022): system information tools listed "Wine HID device" and
  "The Wine Project";
- serial ports (base/0023): every empty /dev/ttyS node was a COM port, so a
  USB adapter landed on COM33;
- the probe programs the bridge is checked with stay buildable.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = ROOT / "patches" / "base"
DEVICES = BASE / "0022-device-manager-names.patch"
SERIAL = BASE / "0023-serial-ports.patch"


def series():
    return [l.strip() for l in (BASE / "series").read_text().splitlines()
            if l.strip() and not l.startswith("#")]


def hunks(patch):
    """(removed lines, added lines) of a patch, without the headers."""
    removed, added = [], []
    for line in patch.read_text().splitlines():
        if line.startswith(("---", "+++")):
            continue
        if line.startswith("-"):
            removed.append(line[1:])
        elif line.startswith("+"):
            added.append(line[1:])
    return removed, added


class DeviceNames(unittest.TestCase):
    def test_patch_is_applied(self):
        self.assertIn(DEVICES.name, series())

    def test_every_device_name_and_maker_is_replaced(self):
        removed, added = hunks(DEVICES)
        gone = "\n".join(removed)
        for name in ("The Wine Project", "Wine HID device", "Wine HID bus driver",
                     "Wine USB bus driver", "Wine Bluetooth bus driver",
                     "Wine HID compatible device", "Wine XInput compatible device",
                     "Wine HID mouse device driver", 'L"Wine"', "Wine=mfg_section"):
            self.assertIn(name, gone)
        for line in added:
            self.assertIsNone(re.search(r"\bWine\b", line), line)

    def test_windows_names(self):
        _, added = hunks(DEVICES)
        text = "\n".join(added)
        for name in ("HID-compliant device", "XINPUT compatible HID device", "USB Input Device",
                     "(Standard system devices)", "Microsoft Corporation"):
            self.assertIn(name, text)

    def test_hardware_ids_are_left_alone(self):
        # drivers match devices by these, a rename would leave them without one
        _, added = hunks(DEVICES)
        text = "\n".join(added)
        for hwid in ("root\\winebus", "root\\wineusb", "root\\winebth", "WINEBUS\\WINE_COMP_HID",
                     "WINEBUS\\WINE_COMP_XINPUT", "HID_DEVICE"):
            self.assertIn(hwid, text)

    def test_the_full_build_keeps_watching(self):
        check = (ROOT / "tools" / "check-names.sh").read_text()
        self.assertIn("The Wine Project", check)
        self.assertIn(".inf", check)


class SerialPorts(unittest.TestCase):
    def test_patch_is_applied_after_the_device_names(self):
        names = series()
        self.assertIn(SERIAL.name, names)
        self.assertLess(names.index(DEVICES.name), names.index(SERIAL.name))

    def test_empty_uarts_are_skipped_and_gaps_crossed(self):
        removed, added = hunks(SERIAL)
        self.assertIn("            if (access( unix_path, F_OK ) != 0) break;", removed)
        text = "\n".join(added)
        self.assertIn("/sys/class/tty/%s/type", text)
        self.assertIn("if (access( unix_path, F_OK ) != 0) continue;", text)
        self.assertIn("is_empty_uart( unix_path )", text)
        self.assertRegex(text, r"for \(i = 0; i < \d+; i\+\+\)")


class Probes(unittest.TestCase):
    def test_probes_are_built(self):
        makefile = (ROOT / "tests" / "samples" / "Makefile").read_text()
        for exe in ("hw-check.exe", "hw-check32.exe", "deps-check.exe", "deps-check32.exe"):
            self.assertIn(exe, makefile)


if __name__ == "__main__":
    unittest.main()
