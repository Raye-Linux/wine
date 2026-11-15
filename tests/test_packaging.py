# SPDX-License-Identifier: LGPL-2.1-or-later
"""The build tree gets everything the packaging reaches for:

    python3 -I -B -m unittest discover -s tests

build.sh does not build in this repository. It unpacks the upstream
tarball into work/, applies the patches, and copies a NAMED LIST of this
repository's folders in:

    cp -a "$here/debian" "$here/tools" "$here/capture" "$tree/"

Then debian/rules runs in there. So a folder debian/rules installs from
but build.sh never copies is a build that gets forty minutes in and dies
on a missing path -- or worse, installs nothing and ships a package with
the files quietly absent. Adding a folder to the tree and forgetting that
line is a one-word mistake with a three-quarter-hour feedback loop, and
nothing in the repository noticed before this test.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True

# Folders the build does not need in the tree, and why.
NOT_NEEDED = {
    "patches": "applied to the tree, never copied into it",
    "tests": "run by build.sh before it builds anything",
    "audio": "a test program, built by hand",
    "out": "where the .deb lands",
    "work": "where the tree is made",
    "build": "debian/rules makes it inside the tree; it is not a source",
    ".git": "not a build input",
    "docs": "read by people, not by the build",
}


def source_dirs():
    """The repository's own top-level folders, ignoring what it builds."""
    ignored = {l.strip().rstrip("/") for l in
               (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
               if l.strip() and not l.startswith("#")}
    return {p.name for p in ROOT.iterdir()
            if p.is_dir() and not p.name.startswith("__")
            and p.name not in ignored}


def copied():
    """The folders build.sh puts in the tree, from the cp line itself."""
    text = (ROOT / "build.sh").read_text(encoding="utf-8")
    line = [l for l in text.splitlines() if re.match(r'^cp -a "\$here/', l)]
    assert len(line) == 1, "build.sh no longer has exactly one cp -a line: %r" % line
    found = set(re.findall(r'"\$here/([A-Za-z0-9_.-]+)"', line[0]))
    # third-party is not copied whole: sources.txt and the cache go in by
    # name, which is what the rest of the script does just below.
    if '"$here/third-party/sources.txt"' in text:
        found.add("third-party")
    return found


def referenced_by_rules():
    """Top-level folders debian/rules reads from the tree.

    Only real path references count. A bare word is not one: "--buildtype
    release" is not the release folder, and counting it would make this
    test pass for a reason that has nothing to do with what it checks.
    """
    text = (ROOT / "debian" / "rules").read_text(encoding="utf-8")
    dirs = source_dirs()
    found = set()
    for head in re.findall(r"(?<![A-Za-z0-9_./-])([A-Za-z0-9_-]+)/[A-Za-z0-9_.$(){}*/-]", text):
        if head in dirs and head != "debian":
            found.add(head)
    for head in re.findall(r"-C\s+([A-Za-z0-9_-]+)", text):
        if head in dirs and head != "debian":
            found.add(head)
    return found


class TheTreeHasWhatTheRulesNeed(unittest.TestCase):
    def test_every_folder_debian_rules_uses_is_copied_into_the_tree(self):
        missing = sorted(referenced_by_rules() - copied())
        self.assertEqual(missing, [], "debian/rules installs from %s, but "
                                      "build.sh's cp -a line does not copy "
                                      "it into the build tree" % missing)

    def test_nothing_is_copied_that_does_not_exist(self):
        for name in sorted(copied()):
            self.assertIn(name, source_dirs(), "build.sh copies %r, which is "
                                               "not in the repository" % name)

    def test_every_folder_is_either_copied_or_knowingly_left_out(self):
        unexplained = sorted(source_dirs() - copied() - set(NOT_NEEDED))
        self.assertEqual(unexplained, [], "these folders are neither copied "
                                          "into the build tree nor listed as "
                                          "not needed: %s" % unexplained)

    def test_the_release_tree_is_shipped_by_the_rules(self):
        # release/windows carries the titles, the profiles and the reader.
        # A build that copies them in and then never installs them is the
        # same silence from the other end.
        rules = (ROOT / "debian" / "rules").read_text(encoding="utf-8")
        absent = [path for path in ("release/windows/titles",
                                    "release/windows/profiles",
                                    "release/windows/raye-windows-titles")
                  if path not in rules]
        self.assertEqual(absent, [], "debian/rules never installs %s, so the "
                                     "package would ship without it" % absent)


class TheColliderIsNotInstalled(unittest.TestCase):
    """A profile raye-base owns the name of must not be installed here.

    dpkg refuses to unpack two packages over one path, so installing
    release/windows/profiles/obs.profile to
    /usr/share/raye/windows-profiles/obs.profile -- which raye-windows-apps
    owns -- would not be a cosmetic overlap. It would make raye-wine and
    raye-windows-apps unable to be installed together, and the error would
    arrive at apt time, to a user.
    """

    def make(self, variable):
        import subprocess
        extra = ROOT / "tests" / ".rules-probe.mk"
        extra.write_text("probe:\n\t@echo '$(%s)'\n" % variable, encoding="utf-8")
        try:
            r = subprocess.run(["make", "-s", "-f", "debian/rules", "-f", str(extra),
                                "probe"], cwd=ROOT, capture_output=True, text=True)
        finally:
            extra.unlink(missing_ok=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.split()

    def test_replaces_and_the_install_list_are_two_views_of_one_decision(self):
        replaced = set(self.make("REPLACED_PROFILES"))
        ours = {pathlib.Path(p).name for p in self.make("OUR_PROFILES")}
        on_disk = {p.name for p in (ROOT / "release" / "windows" / "profiles").glob("*.profile")}
        self.assertEqual(replaced | ours, on_disk,
                         "every profile is either installed by this package or "
                         "declared in REPLACES; %s is neither" %
                         sorted(on_disk - replaced - ours))
        self.assertEqual(replaced & ours, set(),
                         "%s is both declared a replacement and installed here: "
                         "that is the dpkg conflict" % sorted(replaced & ours))

    def test_nothing_this_package_installs_is_owned_by_raye_base(self):
        base = pathlib.Path("/srv/raye/iso/packages/raye-base/release/windows/profiles")
        if not base.is_dir():
            self.skipTest("raye-base is not beside this tree")
        theirs = {p.name for p in base.glob("*.profile")}
        ours = {pathlib.Path(p).name for p in self.make("OUR_PROFILES")}
        clash = sorted(ours & theirs)
        self.assertEqual(clash, [], "this package would install %s, which "
                                    "raye-windows-apps already owns: the two "
                                    "packages could not be installed together"
                                    % clash)


if __name__ == "__main__":
    unittest.main()
