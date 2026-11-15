# SPDX-License-Identifier: LGPL-2.1-or-later
"""The layer is built with the kernel's ntsync:

    python3 -I -B -m unittest discover -s tests

configure turns ntsync on only when it finds linux/ntsync.h, and says
nothing when it does not: the build goes through, the package installs,
and every program falls back to the server's slower waits. So the header
is a build dependency, and the build stops if config.h came out without
it. With a built package in out/, the server and ntdll are checked for
the device too.
"""
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True


class NtsyncBuild(unittest.TestCase):
    def test_the_header_is_a_build_dependency(self):
        control = (ROOT / "debian" / "control").read_text()
        deps = control.split("Build-Depends:", 1)[1].split("\n\n", 1)[0]
        m = re.search(r"linux-libc-dev \(>= ([0-9.]+)\)", deps)
        self.assertIsNotNone(m, "linux-libc-dev with ntsync.h not in Build-Depends")
        # linux/ntsync.h with the wait and event calls the layer uses: 6.14.
        self.assertGreaterEqual(tuple(int(x) for x in m.group(1).split(".")), (6, 14))

    def test_the_build_stops_without_ntsync(self):
        rules = (ROOT / "debian" / "rules").read_text()
        test = rules.split("override_dh_auto_test:", 1)[1].split("\n\n", 1)[0]
        self.assertIn("HAVE_LINUX_NTSYNC_H 1", test)
        self.assertIn("build/include/config.h", test)
        # Checked even with nocheck: it is about what ships, not a test run.
        before_ifeq = test.split("ifeq", 1)[0]
        self.assertIn("HAVE_LINUX_NTSYNC_H", before_ifeq)

    def test_a_built_package_opens_the_device(self):
        debs = sorted((ROOT / "out").glob("raye-wine_*_amd64.deb"))
        if not debs:
            self.skipTest("no built raye-wine in out/")
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(["dpkg-deb", "-x", str(debs[-1]), tmp], check=True)
            server = pathlib.Path(tmp, "opt/raye-wine/bin/wineserver").read_bytes()
            ntdll = pathlib.Path(tmp, "opt/raye-wine/lib/wine/x86_64-unix/ntdll.so").read_bytes()
        self.assertIn(b"/dev/ntsync", server)
        self.assertIn(b"inproc_wait", ntdll)


if __name__ == "__main__":
    unittest.main()
