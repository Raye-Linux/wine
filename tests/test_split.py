# SPDX-License-Identifier: LGPL-2.1-or-later
"""raye-wine comes in parts, each small enough for the archive mirrors.

GitHub Pages carries a copy of the apt archive and refuses any file of
100 MB or more; raye-wine in one piece was 238 MB. The parts have to stay
under the limit, hold between them exactly what the single package held,
and move together on every update.
"""
import importlib.util
import io
import os
import pathlib
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PARTS = ["raye-wine-i386", "raye-wine-mono", "raye-wine-gecko",
         "raye-wine-gecko-i386", "raye-wine-extras"]
LAST_SINGLE = "11.21-1~raye3"

spec = importlib.util.spec_from_file_location("check_debs", ROOT / "tools" / "check-debs.py")
check_debs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_debs)


def stanzas():
    text = (ROOT / "debian" / "control").read_text(encoding="utf-8")
    text = "\n".join(l for l in text.splitlines() if not l.startswith("#"))
    out = {}
    for block in re.split(r"\n\s*\n", text):
        fields, key = {}, None
        for line in block.splitlines():
            if line[:1] in " \t" and key:
                fields[key] += " " + line.strip()
            elif ":" in line:
                key, value = line.split(":", 1)
                fields[key] = value.strip()
        if "Package" in fields:
            out[fields["Package"]] = fields
    return out


def version():
    head = (ROOT / "debian" / "changelog").read_text(encoding="utf-8").splitlines()[0]
    return re.match(r"\S+ \(([^)]+)\)", head).group(1)


def newer(a, b):
    return subprocess.run(["dpkg", "--compare-versions", a, "gt", b]).returncode == 0


def ar(path, members):
    with open(path, "wb") as f:
        f.write(b"!<arch>\n")
        for name, data in members:
            f.write(("%-16s%-12d%-6d%-6d%-8s%-10d`\n" %
                     (name, 0, 0, 0, "100644", len(data))).encode())
            f.write(data)
            if len(data) % 2:
                f.write(b"\n")


def tgz(files):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        dirs = set()
        for path in files:
            parts = path.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                dirs.add("/".join(parts[:i]))
        for d in sorted(dirs):
            info = tarfile.TarInfo("./" + d)
            info.type = tarfile.DIRTYPE
            t.addfile(info)
        for path, data in files.items():
            info = tarfile.TarInfo("./" + path)
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def fake_deb(folder, package, files):
    control = ("Package: %s\nVersion: 1\nArchitecture: amd64\n"
               "Maintainer: x <x@example.org>\nDescription: x\n" % package).encode()
    path = os.path.join(folder, package + ".deb")
    ar(path, [("debian-binary", b"2.0\n"),
              ("control.tar.gz", tgz({"control": control})),
              ("data.tar.gz", tgz(files))])
    return path


class TheControlFile(unittest.TestCase):
    def test_the_core_pulls_every_part_at_its_own_version(self):
        depends = stanzas()["raye-wine"]["Depends"]
        for part in PARTS:
            self.assertIn("%s (= ${binary:Version})" % part, depends)

    def test_parts_take_files_over_from_the_single_package(self):
        pkgs = stanzas()
        for part in PARTS:
            for f in ("Breaks", "Replaces"):
                m = re.search(r"raye-wine \(<< ([^)]+)\)", pkgs[part].get(f, ""))
                self.assertTrue(m, "%s has no %s on the single raye-wine" % (part, f))
                self.assertTrue(newer(m.group(1), LAST_SINGLE))
                self.assertFalse(newer(m.group(1), version()))

    def test_the_version_is_past_the_last_single_package(self):
        self.assertTrue(newer(version(), LAST_SINGLE))


class TheInstallFiles(unittest.TestCase):
    def lines(self, package):
        path = ROOT / "debian" / ("%s.install" % package)
        self.assertTrue(path.is_file(), "%s has no .install file" % package)
        return [l.strip() for l in path.read_text().splitlines() if l.strip()]

    def test_no_path_is_dealt_to_two_packages(self):
        seen = {}
        for package in ["raye-wine"] + PARTS:
            for line in self.lines(package):
                self.assertNotIn(line, seen)
                seen[line] = package
        paths = list(seen)
        for a in paths:
            for b in paths:
                if a != b:
                    self.assertFalse(b.startswith(a.rstrip("*") + "/"),
                                     "%s is inside %s" % (b, a))

    def test_the_rules_stop_on_a_file_no_package_takes(self):
        rules = (ROOT / "debian" / "rules").read_text(encoding="utf-8")
        self.assertIn("dh_missing --fail-missing", rules)


class TheChecker(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_package_over_the_limit_is_refused(self):
        deb = fake_deb(self.d, "big", {"opt/a": os.urandom(4000)})
        self.assertTrue(check_debs.check([deb], max_bytes=2000))
        self.assertEqual(check_debs.check([deb], max_bytes=10_000_000), [])

    def test_a_file_in_two_packages_is_refused(self):
        a = fake_deb(self.d, "a", {"opt/x": b"1"})
        b = fake_deb(self.d, "b", {"opt/x": b"1"})
        self.assertTrue(any("both" in p for p in check_debs.check([a, b])))

    def test_parts_must_hold_what_the_old_package_held(self):
        old = fake_deb(self.d, "old", {"opt/x": b"1", "opt/y": b"2",
                                       "usr/share/doc/old/changelog": b""})
        a = fake_deb(self.d, "a", {"opt/x": b"1", "usr/share/doc/old/changelog": b"",
                                   "usr/share/doc/a/changelog": b""})
        b = fake_deb(self.d, "b", {"opt/y": b"2"})
        self.assertEqual(check_debs.check([a, b], against=old), [])
        self.assertIn("missing: opt/y", check_debs.check([a], against=old))
        c = fake_deb(self.d, "c", {"opt/z": b"3"})
        self.assertTrue(any("opt/z" in p for p in check_debs.check([a, b, c], against=old)))


class TheBuiltPackages(unittest.TestCase):
    """Runs on what build.sh left in out/, once there is something."""

    def built(self):
        v = version()
        debs = sorted(str(p) for p in (ROOT / "out").glob("raye-wine*_%s_*.deb" % v))
        if not debs:
            self.skipTest("no %s packages in out/ yet" % v)
        return debs

    def test_every_package_is_under_the_limit(self):
        self.assertEqual(check_debs.check(self.built()), [])

    def test_they_hold_the_files_of_the_single_package(self):
        old = os.environ.get("RAYE_WINE_SINGLE_DEB")
        if not old or not os.path.isfile(old):
            self.skipTest("RAYE_WINE_SINGLE_DEB does not name the %s package" % LAST_SINGLE)
        self.assertEqual(check_debs.check(self.built(), against=old), [])


if __name__ == "__main__":
    unittest.main()
