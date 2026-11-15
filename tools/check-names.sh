#!/bin/sh
# Fails when a resource or a known window title in the patched tree still
# shows the project name to the user.
#
# Left out on purpose: comments, strings marked "do not translate"
# (registry paths, class names), the test programs, and the version
# descriptions programs check for "Wine" to tell builtin DLLs apart.

cd "$(dirname "$0")/.." || exit 1
[ -f dlls/appwiz.cpl/addons.c ] || { echo "check-names: run it in a Wine tree" >&2; exit 2; }

found=$(grep -rn --include='*.rc' -E '"[^"]*\bWine\b' dlls programs |
    grep -v -E '^[^:]+:[0-9]+:[[:space:]]*(/?\*|//)' |
    grep -v -E '#msgctxt#do not translate#|VER_FILEDESCRIPTION_STR|/tests/|^programs/winetest/')

titles=$(grep -n -E 'L"[^"]*\bWine\b[^"]*"' \
    programs/notepad/dialog.c programs/wordpad/wordpad.c programs/explorer/startmenu.c \
    programs/winemenubuilder/winemenubuilder.c programs/wscript/main.c dlls/winmm/joystick.c \
    dlls/midimap/midimap.c |
    grep -v -E 'WINE_(TRACE|ERR|WARN|FIXME)|Software.{1,4}Wine')

# names programs read from the system and show: input devices, the ACM
# codecs and their copyright, MCI, DirectDraw devices, WMI, MIDI ports
devices=$(grep -n -i -E '"[^"]*\bwine\b' dlls/dinput/mouse.c dlls/dinput/keyboard.c \
    dlls/msacm32/pcmconverter.c dlls/msadp32.acm/msadp32.c dlls/msg711.acm/msg711.c \
    dlls/l3codeca.acm/mpegl3.c dlls/msgsm32.acm/msgsm32.c dlls/imaadp32.acm/imaadp32.c \
    dlls/mcicda/mcicda.c dlls/mciwave/mciwave.c dlls/mciavi32/wnd.c dlls/mciavi32/info.c \
    dlls/mmsystem.dll16/driver.c dlls/ddraw/ddraw.c dlls/msvcrt/exit.c dlls/winealsa.drv/alsamidi.c |
    grep -v -E ':[0-9]+:#include|WINE_(TRACE|ERR|WARN|FIXME)|\b(TRACE|ERR|WARN|FIXME)\(')
wmi=$(grep -Hn -E 'L"Wine (Audio Device|Disk Drive|disk|CD_ROM)' dlls/wbemprox/builtin.c)

loader=$(grep -n -E '^(Name|GenericName|Comment)(\[[^]]*\])?=.*Wine' loader/wine.desktop)

if [ -n "$found$titles$devices$wmi$loader" ]; then
    echo "check-names: the project name is still visible here:" >&2
    printf '%s\n' "$found" "$titles" "$devices" "$wmi" "$loader" | grep -v '^$' | sed 's/^/  /' >&2
    exit 1
fi
echo "check-names: no user-visible project name left"
