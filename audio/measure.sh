#!/bin/sh
# Sound measurements for the Windows program layer, run in the test VM as
# the desktop user, in one ssh session (the user's PipeWire stops with it).
#
#   measure.sh <layer-bin-dir> [section...]
#
# Sections: devices wasapi follow exclusive asio load (all by default).
# Needs audiotest64.exe and audiotest32.exe next to it
# (x86_64-w64-mingw32-gcc -O2 -o audiotest64.exe audiotest.c -lole32 -luuid -lwinmm,
# same with i686 for the 32-bit one), pactl, pw-metadata.
#
# The loop: two null sinks; the default input is a source fed by the first
# one's monitor, so what a program plays comes back on its input.

set -u
bin=$1; shift
here=$(cd "$(dirname "$0")" && pwd)
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WINEPREFIX=${WINEPREFIX:-$HOME/.local/share/raye/audio-test-$(basename "$(dirname "$bin")")}
export WINEDEBUG=${WINEDEBUG:--all} WINEDLLOVERRIDES="mscoree,mshtml=" DISPLAY=
unset WAYLAND_DISPLAY

modules=""
cleanup() {
    "$bin/wineserver" -k 2>/dev/null
    for m in $modules; do pactl unload-module "$m"; done
    pw-metadata -n settings 0 clock.min-quantum 0 >/dev/null 2>&1
    pkill -f 'while :; do :; done' 2>/dev/null
}
trap cleanup EXIT

modules="$modules $(pactl load-module module-null-sink sink_name=at_out sink_properties=device.description=Test-Output-1)"
modules="$modules $(pactl load-module module-null-sink sink_name=at_out2 sink_properties=device.description=Test-Output-2)"
modules="$modules $(pactl load-module module-remap-source source_name=at_in master=at_out.monitor source_properties=device.description=Test-Input)"
pactl set-default-sink at_out
pactl set-default-source at_in

[ -d "$WINEPREFIX" ] || { "$bin/wineboot" -i >/dev/null 2>&1; "$bin/wineserver" -w; }
"$bin/wineserver" -p    # programs then start in a fraction of a second
at() { (cd "$here" && "$bin/wine" audiotest64.exe "$@"); }
at32() { (cd "$here" && "$bin/wine" audiotest32.exe "$@"); }
busy() { for i in $(seq "$(nproc)"); do sh -c 'while :; do :; done' & done; }

# the switch must happen once the program runs, not while the layer starts
switch_later() { (sleep "$1"; pactl set-default-sink at_out2; sleep 4; pactl set-default-sink at_out) & }

[ $# -gt 0 ] || set -- devices wasapi follow exclusive asio load
for section; do
    echo "=== $section"
    case $section in
    devices) at devices ;;
    wasapi)
        at wasapi 0 - 8
        at wasapi 0 loop 8 ;;
    follow)
        # default output changes 3 s after the clicks start; clicks heard
        # after that come from the new output
        echo "-- loopback on the default output"
        switch_later 3
        AT_VERBOSE=1 at wasapi 0 loop 12 | grep -E "heard click|clicks"
        wait
        echo "-- Stereo Mix"
        switch_later 3
        AT_CAPTURE="Stereo" AT_VERBOSE=1 at wasapi 0 - 12 | grep -E "heard click|clicks|capturing"
        wait ;;
    exclusive) at exclusive ;;
    asio)
        for f in 256 128 64 32; do at asio $f 10 | grep -vE "^(ASIO driver found|can do)"; done
        echo "-- 32-bit program"
        at32 asio 128 6 | grep -E "^(driver|callbacks|round)" ;;
    load)
        for f in 256 128 64; do
            half=$(awk "BEGIN { print $f / 48000 * 1000 / 2 }")
            echo "-- $f frames, $half ms of work per callback, every CPU busy"
            busy
            FAKE_LOAD_MS=$half at asio $f 10 | grep -E "^(callbacks|cpu|round|callback interval)"
            pkill -f 'while :; do :; done'; sleep 1
        done ;;
    esac
done
