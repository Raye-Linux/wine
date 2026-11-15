#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints the anti-cheat tables of docs/jeux-et-anti-triche.md.

    tools/titles-doc.py games.json

Read from the titles files through raye-windows-titles, so the doc says
what the Store says. games.json (the same Are We Anti-Cheat Yet? list
00-upstream.titles was made from) only adds each game's Steam number.
"""
import importlib.machinery
import importlib.util
import json
import os
import sys

WINDOWS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "release", "windows")
NAMES = {"easy-anti-cheat": "Easy Anti-Cheat", "battleye": "BattlEye", "vac": "VAC",
         "vanguard": "Vanguard", "hyperion": "Hyperion", "ea-anticheat": "EA Javelin",
         "punkbuster": "PunkBuster"}
AWACY = "areweanticheatyet.com games.json @ 6c26305efc3dccdbf84ebf55a2d6118c2778848f"


def load_titles():
    os.environ["RAYE_WINDOWS_TITLES_PATH"] = os.path.join(WINDOWS, "titles")
    path = os.path.join(WINDOWS, "raye-windows-titles")
    loader = importlib.machinery.SourceFileLoader("raye_windows_titles", path)
    mod = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
    loader.exec_module(mod)
    return mod.load()


def anticheat(title):
    names = [NAMES.get(a.strip(), a.strip()) for a in title.get("anticheat", "").split(",")]
    return ", ".join(n for n in names if n) or "—"


def main():
    titles = load_titles()
    with open(sys.argv[1], encoding="utf-8") as f:
        steam = {g["slug"]: (g.get("storeIds") or {}).get("steam") for g in json.load(f)}
    by_name = lambda t: t["name"].lower()
    refused = sorted((t for t in titles.values() if t["state"] == "blocked"), key=by_name)
    blocked = [t for t in refused if t.get("anticheat", "none") not in ("", "none")]
    other = [t for t in refused if t not in blocked]
    enabled = sorted((t for t in titles.values() if t["upstream"] == "runtime-enabled"), key=by_name)
    on_steam = [t for t in enabled if steam.get(t["id"])]
    elsewhere = [t for t in enabled if not steam.get(t["id"])]

    out = ["### Bloqués par leur anti-triche (%d)\n" % len(blocked),
           "| Titre | Anti-triche | Pourquoi (affiché avant le téléchargement) | Source |",
           "|---|---|---|---|"]
    for t in blocked:
        source = t.get("source", "").replace(AWACY, "AWACY 6c26305")
        out.append("| %s | %s | %s | %s |" % (t["name"], anticheat(t), t["why_fr"], source))

    out += ["\n### Bloqués pour une autre raison (%d)\n" % len(other),
            "| Titre | Pourquoi (affiché avant le téléchargement) | Source |", "|---|---|---|"]
    for t in other:
        out.append("| %s | %s | %s |" % (t["name"], t["why_fr"], t.get("source", "")))

    out += ["\n### Anti-triche activé pour Linux par l'éditeur, vendus sur Steam (%d)\n" % len(on_steam),
            "Chemin : Steam pour Raye (Store) puis Proton. Aucun n'a encore été joué sur Raye.\n",
            "| Titre | Anti-triche | Steam |", "|---|---|---|"]
    for t in on_steam:
        app = steam[t["id"]]
        out.append("| %s | %s | [%s](https://store.steampowered.com/app/%s) |"
                   % (t["name"], anticheat(t), app, app))

    out += ["\n### Anti-triche activé pour Linux, sans numéro Steam dans la liste source (%d)\n"
            % len(elsewhere),
            "La liste source ne dit pas où ils sont vendus. Plusieurs sont pourtant sur Steam : "
            "à vérifier un par un avant d'indiquer un chemin.\n",
            "| Titre | Anti-triche |", "|---|---|"]
    for t in elsewhere:
        out.append("| %s | %s |" % (t["name"], anticheat(t)))
    print("\n".join(out))


if __name__ == "__main__":
    main()
