# Affinity, Canva's Photo, Designer and Publisher in one app. It is a
# .NET Framework 4.8 program, so the space gets Microsoft's runtime in
# place of Mono, and says it is Windows 11 as the app expects.
# Direct3D goes to the built-in renderer: on DXVK it crashed making its
# first document, with this it opens and draws documents.
name="Affinity"
match="affinity?x64*"
verbs="dotnet48 win11"
env="WINEDLLOVERRIDES=d3d10core,d3d11,dxgi=b"
title="affinity"
