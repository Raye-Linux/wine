# Windows games and programs: what Raye says before the download

Three things live here. Two are data, one is the reader, and the whole
point of all three is to say "no" **before** somebody waits forty
gigabytes to be told it.

```
titles/00-upstream.titles   249 titles, generated: what publishers did
titles/50-raye.titles       what Raye verified itself
profiles/*.profile          which download is which title
raye-windows-titles         the reader the Store and the games space call
REPLACES                    the one profile raye-base also has a file for
```

## The two columns that must not be confused

| | says | written by |
|---|---|---|
| `state` | what **Raye** verified, on a real machine, on a day | a person, after playing it |
| `upstream` | what the **publisher** did about Linux | the generator, from a dated list |

`state` is one of four words: `works`, `offline-only`, `blocked`,
`untested`. `upstream` is one of `runtime-enabled`, `refused`, `none`,
`unknown`.

**`works` and `offline-only` are refused without `tested_on` and
`tested_at`.** `tests/test_titles.py` fails the build otherwise. A
launcher that opened is not a game that played, and the one failure mode
this whole mechanism exists to prevent is a confident green in front of a
download that cannot work.

**`upstream = runtime-enabled` is not `state = works`.** Easy
Anti-Cheat and BattlEye both ship a Linux module, and a publisher that
switched it on has done its half. Ours is still untested. Shipping the
publisher's half as our answer would be the same lie with a citation
attached.

## The 89 titles whose publisher said yes still cannot run here

Measured on 2026-11-06, in `third-party/sources.txt`: this layer ships
DXVK, VKD3D-Proton, NVAPI, Mono and Gecko, and **no anti-cheat runtime
of any kind**. Easy Anti-Cheat and BattlEye each have a Linux module,
and 67 + 22 titles have had it switched on by their publisher - but a
module nobody ships is a module nobody runs.

So `upstream = runtime-enabled` on 89 titles is a true statement about
those publishers and says nothing about Raye. Their `state` is
`untested`, and it would stay that way even on a machine with a GPU.

What it would take is a decision, not a patch, and it belongs to the
owner:

- the runtimes are the vendors' own binaries, redistributed inside
  Proton, with their own licence terms. Putting them in a `.deb` is a
  licensing question before it is an engineering one;
- and the module alone is not enough. Those titles expect the bridging
  Proton does, the Steam Linux Runtime container around it, and to be
  started by Steam or Heroic. A bare space is not that environment.

Until both are settled, the honest answer for all 89 is `untested`,
which is what the data says. Nothing here claims otherwise.

## Updating it without rebuilding the image

Files are read from two folders, in this order, and **the last file that
names a title wins**:

```
/usr/share/raye/windows-titles/*.titles    shipped in the package
/var/lib/raye/windows-titles/*.titles      dropped in later
```

inside each folder, in file-name order. So, lowest answer to highest:

```
00-upstream.titles   generated, 249 titles, nothing but publishers
50-raye.titles       verified here -- overrides the generated answer
/var/lib/.../70-*    anything dropped in later -- overrides both
```

The numbers are the mechanism, not decoration. An anti-cheat changes
side on a Tuesday; waiting for an image rebuild would mean telling
people something false for a month. Dropping one file in
`/var/lib/raye/windows-titles` changes the answer on the next read, and
a regenerated list can never bury a verdict somebody measured.

```
raye-windows-titles                      every title
raye-windows-titles roblox               one, in the session's language
raye-windows-titles --state roblox       the word alone
raye-windows-titles --json roblox        the record, with where it came from
raye-windows-titles --file Setup.exe     which title a download is
raye-windows-titles --where              the folders it read, in order
```

Exit 0 when the title is known, **3 when it is not** - not knowing is
not good news, and a caller has to be able to tell the two apart. An
unknown title answers `untested`, never `works`.

Regenerating the publisher half, with the source recorded in the file:

```
curl -fsSLO https://raw.githubusercontent.com/AreWeAntiCheatYet/AreWeAntiCheatYet/HEAD/games.json
../../tools/titles-from-awacy.py games.json \
    --commit <the commit it came from> --date <today> > titles/00-upstream.titles
```

## For whoever wires the Store and the games space

The reader is the whole contract: call it, read `state`, show `why_fr` or
`why_en`, and use `size_mb` to say what a "no" just saved. Nothing else
here needs to be understood.

⏳ **Two things are NOT done, and they are not mine to do:**

1. `raye-run-windows` lives in raye-base and reads only
   `/usr/share/raye/windows-profiles`. It ignores `title=` today, so a
   double-clicked installer of a blocked game still opens a space and
   starts. The one-line change it needs is to read `title=`, ask
   `raye-windows-titles --state`, and show `why_*` with a single "open
   anyway" button instead of running it. Until then this data answers
   the Store, not the double-click.
2. `profiles/obs.profile` is **not installed by this package**: raye-base
   already owns that path and dpkg refuses two packages over one file.
   See `REPLACES`. Adopting it is one line in
   `raye-base/debian/raye-release-paths`.

## Never

No way around an anti-cheat, in the data or in the code. A title whose
protection refuses this system is a "no" written in the interface. Going
around it gets the player's account banned, and it is the reason
publishers give for staying away from Linux in the first place.
