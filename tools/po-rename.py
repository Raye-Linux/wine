#!/usr/bin/python3 -I
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Swaps strings in a Wine po file after their English source changed.

    po-rename.py names.tsv po/fr.po

Each line of names.tsv is: old English, new English, new translation,
separated by tabs, written the way they appear in the .po file (C
escapes, \\n, \\"). Lines starting with # are skipped. Every entry whose
msgid is the old English gets the new msgid and the new msgstr, and loses
its fuzzy flag. An old string that is not in the file is an error: the
upstream text moved and the table needs a look.

The other po files are left alone. Their entries for the old text no
longer match anything, so those languages show the new English, which
never names the old one.
"""
import re
import sys

WIDTH = 79


def read_table(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 3:
                sys.exit("%s:%d: want 3 columns, got %d" % (path, n, len(cols)))
            rows.append(cols)
    return rows


def joined(block):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block))


def po_string(keyword, text):
    """keyword "text" the way gettext wraps it: one line when short,
    otherwise an empty first line and pieces cut after \\n and spaces."""
    if len(keyword) + len(text) + 3 <= WIDTH and "\\n" not in text[:-2]:
        return '%s "%s"\n' % (keyword, text)
    pieces, cur = [], ""
    for token in re.findall(r'\\n|\\.|[^\\ ]+ ?| ', text):
        if cur and len(cur) + len(token) > WIDTH - 2:
            pieces.append(cur)
            cur = ""
        cur += token
        if token == "\\n":
            pieces.append(cur)
            cur = ""
    if cur:
        pieces.append(cur)
    return '%s ""\n' % keyword + "".join('"%s"\n' % p for p in pieces)


ENTRY = re.compile(r'(?m)^(?P<head>(?:#.*\n)*)(?P<ctx>msgctxt (?:".*"\n)+)?'
                   r'msgid (?P<id>(?:".*"\n)+)msgstr (?P<str>(?:".*"\n)+)')


def rename(text, rows):
    table = {old: (new, tr) for old, new, tr in rows}
    seen = set()

    def swap(m):
        old = joined(m.group("id"))
        if old not in table:
            return m.group(0)
        seen.add(old)
        new, tr = table[old]
        head = re.sub(r"(?m)^#, fuzzy\n", "", m.group("head"))
        head = re.sub(r"(?m)^(#,.*), fuzzy", r"\1", head)
        return head + (m.group("ctx") or "") + po_string("msgid", new) + po_string("msgstr", tr)

    out = merge(ENTRY.sub(swap, text))
    missing = [old for old in table if old not in seen]
    return out, missing


def merge(text):
    """A new name may already be in the file (\"Help\", \"Control Panel\"):
    msgfmt refuses a msgid twice, so the later entries give their source
    references to the first one and go."""
    entries = list(ENTRY.finditer(text))
    first, refs, drop = {}, {}, set()
    for i, m in enumerate(entries):
        key = (m.group("ctx") or "", joined(m.group("id")))
        if not key[1]:
            continue
        if key in first:
            refs[first[key]] += re.findall(r"(?m)^#: .*\n", m.group("head"))
            drop.add(i)
        else:
            first[key] = i
            refs[i] = []
    out, pos = [], 0
    for i, m in enumerate(entries):
        out.append(text[pos:m.start()])
        pos = m.end()
        if i in drop:
            if text.startswith("\n", pos):
                pos += 1
            continue
        head = m.group("head")
        if refs.get(i):
            own = re.findall(r"(?m)^#: .*\n", head)
            at = head.index(own[-1]) + len(own[-1]) if own else 0
            head = head[:at] + "".join(refs[i]) + head[at:]
        out.append(head + m.group(0)[len(m.group("head")):])
    out.append(text[pos:])
    return "".join(out)


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: po-rename.py names.tsv file.po")
    rows = read_table(argv[0])
    with open(argv[1], encoding="utf-8") as f:
        text = f.read()
    out, missing = rename(text, rows)
    for old in missing:
        print("not found: %s" % old[:70], file=sys.stderr)
    if missing:
        return 1
    with open(argv[1], "w", encoding="utf-8") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
