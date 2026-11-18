# What these tests cover, and what they do not

```
python3 -I -B -m unittest discover -s tests        # 139 checks, ~12 s, no Wine tree
```

Run by `build.sh` before it builds anything, and by
`/srv/raye/iso/tools/raye-tests --repo wine`.

| file | checks | about |
|---|---|---|
| `test_tree.py` | 12 | series, patch headers, the French table, po-rename, the pinned third-party list |
| `test_titles.py` | 14 | the per-title answers, and that a positive one carries a witness |
| `test_profiles.py` | 9 | profiles, read the way the runner reads them |
| `test_names.py` | 12 | the upstream name never reaching a user |
| `test_i18n.py` | 19 | the French of everything this repository ships: the rename table, the 255 per-title answers, and the strings a patch ADDS |
| `test_packaging.py` | 6 | the build tree gets what the packaging reaches for, and no dpkg conflict |
| `test_fonts.py` | 12 | font names programs ask for map to fonts Raye ships; MS Sans Serif to Tahoma |
| `test_package.py` | 25 | Store packages (.msix, .appx, bundles): what runs, what is refused, safe unpacking |
| `test_proton_parity.py` | 4 | WINE_LARGE_ADDRESS_AWARE gives 32-bit programs 4 GB |

## The gaps, named

Nothing here builds, downloads, or boots, so these are real and they are
listed so nobody mistakes a green run for a tested layer. Each says what
a failure would look like if it happened.

**Not covered at all**

- **`tools/raye-wine-gpu.c`.** It decides whether Direct3D goes to Vulkan
  or to the software renderer, and it is the one piece whose wrong answer
  is invisible: games run, slowly, and nobody knows a GPU was skipped.
  It is C and needs compiling; nothing tests it.
- **`capture/`.** `capture/tests/test-logic.c` exists and runs, but only
  inside the package build (`override_dh_auto_test`). A broken share
  picker means programs that read the screen get a black rectangle.
- **What a space really draws.** `test_fonts.py` reads the patch;
  `tests/samples/fontprobe.exe` run in a space says which font each name
  gets (Courier New must come back monospace, MS Sans Serif not as a
  bitmap), and `laa-check32.exe` with WINE_LARGE_ADDRESS_AWARE=1 must say
  4095 MB.
- **`audio/measure.sh`** and the Direct3D / sound sample programs under
  `tests/samples/`: by hand, in the test VM, per the app list in
  the design notes of the iso repository.
- **Whether the patches apply.** Only `build.sh --check` knows, and it
  needs the 35 MB upstream tarball. A patch that stopped applying looks
  perfectly healthy here.
- **`tools/check-names.sh` itself.** It exits 2 outside a Wine tree, so
  it never runs here and its regexes can rot unnoticed.
  `test_names.py` keeps one canary - it asserts the script still names
  `loader/wine.desktop` - but the script's logic is untested.
- **The French `po` output.** `test_tree.PoRename` tests the tool on a
  synthetic file; what it produces from the real `po/fr.po` is only seen
  during a build. `test_i18n.py` checks that every string a patch adds has
  SOME way of being French -- a row in the table, a msgid in
  `0009-french-names.patch`, or its own `LANGUAGE LANG_FRENCH` block -- but
  not that the result compiles or reads well.
- **Whether a translation is right.** Nothing here reads French. A `why_fr`
  that is present, different from its English and complete nonsense passes
  every check in this folder.
- **Mono and Gecko versions against what the release wants.** `build.sh`
  compares them with `third-party/sources.txt` and refuses a mismatch;
  here, only the shape of the list is checked.

**Covered, but by a copy of the thing rather than the thing**

- `test_profiles.py` reimplements `raye-run-windows`' `sed` expression
  instead of running the runner, which lives in another repository. If
  the runner changes how it parses a profile, every test here still
  passes and every profile silently stops being read. The reimplementation
  is deliberate and marked as such in that file, but it is a copy.
- `test_names.py` only knows the word with a capital W. The lowercase
  shape - `wine: could not load...` from the loader - can only come from
  upstream code, which `tools/check-names.sh` guards in the patched tree.
  The split is intentional and asserted, not forgotten.

**Not a gap in the tests, a gap in the layer**

- `raye-run-windows` ignores `title=`, so double-clicking the installer
  of a game its anti-cheat blocks still opens a space and starts it. The
  per-title answer reaches the Store, not the double-click, until that
  one line is added in raye-base. See `../release/windows/README.md`.
