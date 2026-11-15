# Roblox Studio, the editor. Separate from the Roblox player, which its
# publisher blocks here: Studio carries no such block that anyone has
# found, and vinegarhq/vinegar runs RobloxStudioBeta.exe through this
# layer today (cmd/vinegar/bootstrapper_run.go, master, read 2026-11-06).
# The filenames below were taken from setup.rbxcdn.com, which serves all
# three (HTTP 200, 2026-11-06), not from memory.
# Nothing extra is asked for: the ready space already brings the core
# fonts, Microsoft's Visual C++ runtime and Direct3D on Vulkan.
# Its sign-in page is Edge WebView2, which Studio installs itself; it
# needs to be told it runs on Windows 8 (raye-day.reg / raye-night.reg).
# opens= and files= hand it what it opens on Windows: places and models
# from Files (types in raye-windows.xml), and roblox-studio links, among
# them the roblox-studio-auth one "Login via Browser" comes back through.
# vinegarhq/vinegar registers the same (data/org.vinegarhq.Vinegar.desktop
# at 45a1165).
# Studio ships mimalloc, which crashes unless it is told to take over
# malloc for the whole process (vinegarhq/kombucha, ntdll kombucha_hacks).
name="Roblox Studio"
match="robloxstudio*"
verbs=""
env="MIMALLOC_FORCE_REDIRECT=1"
title="roblox-studio"
opens="application/x-roblox-place;application/x-roblox-model;x-scheme-handler/roblox-studio;x-scheme-handler/roblox-studio-auth"
files="rbxl rbxlx rbxm rbxmx"
