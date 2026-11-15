# raye-wine

The layer that runs Windows programs on Raye Linux: a Wine release with
Raye's patches, built as the `raye-wine` package and installed in
`/opt/raye-wine`, out of the way of any other copy on the system.

- `UPSTREAM` the release it is built from
- `patches/` Raye's patches, applied in order
- `capture/`, `audio/` screen capture and audio helpers
- `release/` what the package installs next to it
- `docs/` which games and programs work, and how that was checked
- `tests/` the tests

The French texts come from the release's own catalog: `tools/po-rename.py
tools/names-fr.tsv po/fr.po` applies Raye's names to it when the release
moves.

## Build

    ./build.sh --check    # every patch applies to the release
    ./build.sh            # the .deb lands in out/

## Test

    python3 -m pytest tests

## License

Raye's own code is GPL-3.0-or-later; the patches are LGPL-2.1-or-later,
like Wine. See `LICENSE`.

## Links

- Website: https://rayelinux.com
- Discord: https://discord.gg/TrxhUkabyB (questions, bug reports, news)
