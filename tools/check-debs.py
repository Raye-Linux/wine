#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks the packages a build made before anything ships them.

    check-debs.py out/*.deb
    check-debs.py --against old/raye-wine_11.21-1~raye3_amd64.deb out/*.deb

Every package must stay under MAX_BYTES: the apt archive is mirrored on
GitHub Pages, which refuses any file of 100 MB or more. No path may be in
two packages (dpkg would refuse to unpack the second). With --against,
the packages together must hold exactly the files of the given older
package, apart from their own /usr/share/doc folders.
"""
import argparse
import os
import subprocess
import sys
import tarfile

MAX_BYTES = 90_000_000


def field(deb, name):
    return subprocess.run(["dpkg-deb", "-f", deb, name], check=True,
                          capture_output=True, text=True).stdout.strip()


def contents(deb):
    """path -> (type, link target) for every entry of the package."""
    proc = subprocess.Popen(["dpkg-deb", "--fsys-tarfile", deb],
                            stdout=subprocess.PIPE)
    entries = {}
    with tarfile.open(fileobj=proc.stdout, mode="r|") as tar:
        for m in tar:
            path = m.name[2:] if m.name.startswith("./") else m.name
            path = path.rstrip("/")
            if not path:
                continue
            kind = "dir" if m.isdir() else "link" if m.issym() else \
                "hardlink" if m.islnk() else "file"
            entries[path] = (kind, m.linkname if m.issym() else "")
    proc.stdout.close()
    if proc.wait() != 0:
        raise SystemExit("dpkg-deb could not read %s" % deb)
    return entries


def check(debs, against=None, max_bytes=MAX_BYTES):
    """The list of problems; empty when the packages are fine."""
    problems = []
    owner = {}
    union = {}
    names = set()
    for deb in debs:
        size = os.path.getsize(deb)
        if size >= max_bytes:
            problems.append("%s: %.1f MB, the limit is %.0f MB" %
                            (os.path.basename(deb), size / 1e6, max_bytes / 1e6))
        names.add(field(deb, "Package"))
        for path, entry in contents(deb).items():
            if entry[0] != "dir":
                if path in owner:
                    problems.append("%s is in both %s and %s" %
                                    (path, owner[path], os.path.basename(deb)))
                owner[path] = os.path.basename(deb)
            union[path] = entry
    if against:
        old = contents(against)
        doc = tuple("usr/share/doc/%s" % n for n in names)
        missing = sorted(p for p in old if p not in union)
        extra = sorted(p for p in union if p not in old
                       and not (p in doc or p.startswith(tuple(d + "/" for d in doc))))
        changed = sorted(p for p in old if p in union and old[p] != union[p])
        for p in missing:
            problems.append("missing: %s" % p)
        for p in extra:
            problems.append("not in the old package: %s" % p)
        for p in changed:
            problems.append("changed: %s %s -> %s" % (p, old[p], union[p]))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--against")
    ap.add_argument("--max-bytes", type=int, default=MAX_BYTES)
    ap.add_argument("debs", nargs="+")
    args = ap.parse_args()
    problems = check(args.debs, args.against, args.max_bytes)
    for p in problems:
        print("check-debs: " + p, file=sys.stderr)
    if problems:
        return 1
    print("check-debs: %d packages, all under %.0f MB%s" %
          (len(args.debs), args.max_bytes / 1e6,
           ", same files as %s" % args.against if args.against else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
