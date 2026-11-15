# minecraft.net's Windows launcher (MinecraftInstaller.exe) is a Windows
# Runtime app: here it quits at once without a window. Java Edition has a
# Linux launcher, so the Store gets the download instead. Bedrock only
# installs through the Microsoft Store.
name="Minecraft"
match="minecraftinstaller* minecraftlauncher*"
verbs=""
native="com.mojang.Minecraft"
title="minecraft"
