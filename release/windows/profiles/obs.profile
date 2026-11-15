# OBS Studio. Replaces the same file in raye-base: see ../REPLACES.
# Only two things differ, and neither changes what a user sees today:
# title= links it to titles/00-raye.titles, and the pattern is written
# once instead of twice.
# ⚠️ Do NOT add vcrun to verbs: the ready space already installs
# Microsoft's Visual C++ runtime (raye-windows-prepare, via
# raye-windows-components), and a redundant verb costs minutes of
# winetricks on every new space.
# The installer is named OBS-Studio-<version>-Windows-x64-Installer.exe
# (obsproject/obs-studio release 33.0.1, 2026-10-29); the pattern below
# covers that and the older ...-Full-Installer-x64.exe spelling.
name="OBS Studio"
match="obs-studio-*"
native="com.obsproject.Studio"
title="obs"
