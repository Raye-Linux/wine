# rekordbox, Pioneer DJ's (AlphaTheta's) DJ app. The Store takes the setup
# out of the maker's zip; it runs quietly with /S once it is told a
# language. rekordbox.exe looks up wine_get_version in ntdll and stalls at
# sign-in when it finds it (erhan.es, "Running Pioneer rekordbox on
# Linux"), so this space hides the version exports (base/0020).
name="rekordbox"
match="install_rekordbox*"
verbs=""
env="WINEHIDEEXPORTS=1"
title="rekordbox"
