# Jeux, programmes Windows et anti-triche sur Raye

Ce que Raye peut promettre aujourd'hui pour un jeu ou un programme Windows,
et ce qu'il ne promet pas. Chaque ligne a sa source ; rien n'est déduit d'un
lanceur qui s'est simplement ouvert. Les mêmes réponses sont lues par
`raye-windows-titles` (`release/windows/titles/`), et c'est ce fichier-là qui
fait foi : si les deux divergent, il a raison et celui-ci est à refaire
(`tools/titles-doc.py games.json` régénère les tableaux).

Données : liste AreWeAntiCheatYet (`games.json`, commit `6c26305`, relue le
2026-11-06), plus ce que Raye a vérifié lui-même (`50-raye.titles`).

## Les trois chemins

1. **Un programme Windows sans anti-triche** : double-clic sur le `.exe` ou le
   `.msi`, il s'installe dans son propre espace et prend une entrée dans
   Démarrer. C'est la couche Windows de Raye.
2. **Un jeu vendu sur Steam** : Steam pour Raye (dans le Store), puis Proton.
   C'est Steam qui télécharge Proton et, quand le jeu en a besoin, le module
   Linux d'Easy Anti-Cheat ou de BattlEye. Raye ne livre aucun de ces modules
   et n'en a pas besoin sur ce chemin.
3. **Un jeu dont l'anti-triche refuse ce système** : non. Raye le dit avant
   le téléchargement, avec la raison. Aucun contournement, ni dans les données
   ni dans le code : contourner un anti-triche fait bannir le compte du joueur,
   et c'est l'argument des éditeurs pour rester loin de Linux.

Où l'utilisateur le voit aujourd'hui : Steam pour Raye est dans le Store
(« Essentiels » et « Jouer ensemble »), Heroic aussi. Le Store n'a **pas
encore de fiche pour un jeu Windows** : la réponse d'un titre n'apparaît
qu'au double-clic sur son installeur, quand Raye le reconnaît (Roblox,
le lanceur Windows de Minecraft). Une fiche de jeu qui dirait « passe par
Steam + Proton » reste à faire.

## Ce qui a été essayé sur Raye

Essais du 2026-11-11 et du 2026-11-12 avec raye-wine 11.21, **sans carte graphique** : Direct3D
passe par le rendu logiciel (llvmpipe), donc un programme qui s'affiche ici ne
dit rien de sa vitesse sur un vrai PC, et un jeu qui ne s'affiche pas ici peut
très bien tourner avec un GPU. Deux endroits :

- **chroot** : raye-wine seul, sans bureau, sans son, sans Vulkan. Dit si le
  programme démarre sur la couche, pas s'il s'intègre à Raye.
- **la VM de test** : Raye 0.3, plus raye-wine 11.21 et les raye-base de travail
  (rien de cela n'est encore publié : la 0.3 publiée installe winehq-stable),
  session Plasma, double-clic depuis Fichiers. Dit ce que voit l'utilisateur.

Chaque essai a été refait par un second contrôle ; ce tableau ne garde que ce
que ce contrôle a confirmé.

| Programme | Où | Résultat |
|---|---|---|
| Minecraft : Java Edition | la VM de test | Le Store installe « Minecraft Launcher » (Flathub `com.mojang.Minecraft`), le lanceur s'ouvre jusqu'à « Sign in with Microsoft ». Pas plus loin sans compte : aucune partie jouée. |
| Minecraft : `.deb` de minecraft.net | chroot | Refusé par apt (`libgdk-pixbuf2.0-0`, ancien nom) ; Raye fournit ce nom depuis le 2026-11-11 et apt l'accepte alors. Au double-clic, Fichiers ouvre le `.deb` comme une archive : le chemin normal reste le Store. |
| Minecraft : lanceur Windows (`MinecraftInstaller.exe`) | chroot, la VM de test | Quitte aussitôt sans fenêtre (`Windows.Foundation.UniversalApiContract` introuvable : c'est une appli Windows Runtime). Au double-clic, Raye le dit maintenant (« Minecraft ne peut pas fonctionner sur Raye… »), puis propose le Store, qui ouvre la fiche Minecraft Launcher. Si l'on choisit quand même « Windows » : rien à l'écran, et un espace de 450 Mo reste derrière (défaut ouvert). |
| Minecraft : Bedrock pour Windows | - | Ne s'installe que par le Microsoft Store : impossible ici. Même message que ci-dessus. |
| VLC 3.0.24 (Windows) | la VM de test | **En partie** : au double-clic Raye propose VLC pour Raye ; avec « Windows », l'installeur va au bout, trois entrées dans Démarrer et une icône sur le bureau, un son joué (flux PipeWire « VLC media player »), désinstallé par `raye-windows uninstall` (espace, entrées et icône retirés). **Aucune vidéo essayée** : pas marqué « works ». |
| WinRAR 7.23 | la VM de test | **Marche** (avec le correctif non publié) : double-clic, installation, entrée Démarrer, ouverture depuis Démarrer, un zip de 61,7 Mo extrait à l'identique (empreintes comparées). La désinstallation échouait (« failed to open C:\\Program ») : corrigé dans raye-base (iso c575148). Contrôlé le 2026-11-12 : avec le lanceur de la 0.3 publiée, le bouton Désinstaller de Paramètres dit « Couldn't uninstall » ; avec le lanceur corrigé, la commande que Paramètres exécute désinstalle (rc 0, espace retiré). Le bouton lui-même reste à refaire une fois raye-base reconstruit. |
| Chocolate Doom 3.1.1 + Freedoom (SDL2, Direct3D 9) | la VM de test | Double-clic sur le `.exe` : plein écran, démo, nouvelle partie, déplacement et tir au clavier, flux son « Chocolate Doom ». Quelques secondes de jeu seulement. |
| Paint.NET 5.2 | chroot, la VM de test | **Refusé par son installeur** : « Setup cannot continue because its files have been modified or are damaged » (après environ une minute sans rien à l'écran, le temps que Raye installe les composants .NET). Le fichier est intact. Cause trouvée : l'installeur demande l'empreinte SHA-256 du certificat de signature, que la couche ne savait pas donner. Corrigée dans raye-wine (pas encore dans un paquet) ; avec ce correctif l'installeur dépasse ce contrôle puis plante (0xC0000005) en interrogeant la carte graphique (`DxgiAdapterExtensions.IsAdvancedEnough`), ici sans GPU : à refaire sur un vrai PC. |
| Spotify (installeur Windows) | chroot | S'installe (22 s), aucune fenêtre en 60 s. Au double-clic, Raye propose Spotify pour Raye. |
| Epic Games Launcher 20.3.3 | chroot | S'installe (77 s), se met à jour puis plante en boucle. Au double-clic, Raye propose Heroic (Store). |
| Steam (client Windows) | chroot | S'installe, télécharge sa mise à jour (2,3 Go), puis ouvre sa fenêtre de connexion, mais sans aucun texte (libellés absents). Au double-clic, Raye propose Steam pour Raye : c'est le bon chemin, Proton compris. |
| Steam pour Raye (Store, Flathub) | la VM de test | Installé depuis le Store (1,4 Go), se met à jour (483 Mo) et va jusqu'à « Sign in with account name » / QR. Pas plus loin sans compte : aucun jeu, ni Proton, essayé. |
| Notepad++ 8.9.9 | la VM de test | Son profil n'était pas reconnu quand l'installeur était lancé depuis Téléchargements (motif développé contre le dossier) : corrigé, l'espace s'appelle bien `notepad-plus-plus`. |
| 7-Zip 26.06 | la VM de test | **Marche** : installé par double-clic, entrée « 7-Zip File Manager », ouverture depuis Démarrer, désinstallé depuis Paramètres sans erreur (contrôle du 2026-11-12). |
| Notepad++ 8.9.9 (installation et désinstallation) | la VM de test | S'installe par double-clic, entrée Démarrer, désinstallé par `raye-windows uninstall` (rapport de la couche Windows du 2026-11-11). Sa barre de menus s'affiche à la verticale. |
| FL Studio 2026 (26.1.7) | la VM de test | **En partie.** Installation par double-clic (avec Edge WebView2) ; version d'essai sans compte. Le pilote Raye ASIO s'ouvre (48 kHz, tampon 256, choisi depuis le panneau ASIO de FL). Un installeur de VST (Surge XT) s'installe dans l'espace de FL et FL le trouve et le charge ; Sytrus joue sans coupure. Défauts : l'entrée Démarrer de FL n'ouvre rien tant que raye-store n'est pas reconstruit (arguments faits d'un seul NUL) ; « Wine midi mapper » visible dans ses réglages MIDI (corrigé dans le code, pas encore dans un paquet) ; entrées MIDI PipeWire en échec ; Raye ASIO refuse 44,1 kHz ; lecture d'un projet de ~40 pistes **hachée** (1 850 trous en 5 min à 256, 71 en 92 s à 512) avec le rendu logiciel de cette VM, ce qui ne dit rien d'un vrai PC. |
| Roblox Studio 0.747 | la VM de test | **En partie.** Installation par double-clic (espace de 3,1 Go), démarre et ouvre sa page de connexion. Pas de mode hors ligne : il faut un compte. La page de connexion ne prend aucune frappe au clavier et sa vue web s'arrête au bout d'environ 65 s. L'entrée Démarrer n'ouvre rien tant que raye-store n'est pas reconstruit. Une fenêtre « Partager l'écran » s'ouvre toute seule au démarrage (défaut de la capture, voir plus bas). |
| Roblox (le jeu) | la VM de test | **Refusé, exprès** : au double-clic sur l'installeur, Raye dit « Roblox ne peut pas fonctionner sur Raye. Roblox bloque ce système exprès ; c'est à son éditeur de l'autoriser. » Fermer n'installe rien. Aucun contournement. |
| OBS Studio 33 (Windows) | la VM de test | **Pas prêt.** Avec le raye-wine publié, s'arrête au démarrage (fonction GDI32 manquante, corrigée dans le code, pas encore dans un paquet). Avec le correctif : enregistre 60 s avec l'écran, le son du PC et le micro, mais l'image arrive 40 à 150 ms après le son selon la charge, et OBS affiche à chaque démarrage sa propre fenêtre d'avertissement qui nomme la couche. |
| OBS Studio (Raye) | la VM de test | **Marche** : image et son en phase, pas de trou. C'est celle qu'il faut prendre. |
| Discord (Windows) | la VM de test | S'installe, arrive à l'écran de connexion. Une fenêtre « Partager l'écran » s'ouvre toute seule au démarrage (défaut ouvert). Partage d'écran non essayé : il faut un compte. Au double-clic, Raye propose d'abord Discord pour Raye. |
| TikTok LIVE Studio 1.39.6 | chroot | S'installe ; au lancement, sans carte graphique, son processus graphique plante en boucle. À refaire sur un vrai PC (et il faut un compte pour passer en direct). |

Défauts trouvés et corrigés dans le code le 2026-11-11 (pas encore dans un paquet publié) : « Désinstaller » dans Démarrer
ouvrait le Store sur « Cette application n'est pas disponible » pour un
programme Windows (il ouvre maintenant Paramètres > Applications installées) ;
une commande de désinstallation sans guillemets avec des espaces (WinRAR)
échouait ; un profil n'était pas reconnu depuis Téléchargements.

Reste vu : après une désinstallation, une entrée cachée de l'espace reste dans
`~/.local/share/applications` (corrigé dans le Store, pas encore dans l'image
essayée) ; pendant l'installation des composants (.NET, polices), rien
n'indique à l'utilisateur que quelque chose se passe.

## Ce qu'il reste à essayer sur une vraie carte graphique

Rien de ce qui suit n'a été vu sur un vrai GPU. The test VM n'en a pas, et le PC du
propriétaire est le seul endroit prévu pour ça.

- **Tout jeu en Direct3D 11 ou 12** (DXVK, VKD3D-Proton) : ici, sans Vulkan,
  Raye retombe sur le Direct3D en OpenGL, qui n'est pas celui d'un vrai PC.
- **Les jeux par Steam + Proton**, dont un des titres à Easy Anti-Cheat ou
  BattlEye activés par l'éditeur (liste plus bas) : en jeu, en ligne.
- **Rocket League hors ligne** (Steam, Proton) : sa fiche dit les deux choses.
- **Minecraft Java** : une vraie partie après connexion (Store, Flathub).
- **Roblox Studio** : la page de connexion (frappe au clavier, vue web qui
  s'arrête), puis la vue 3D après connexion.
- **FL Studio** : la lecture d'un gros projet sans craquements, la charge du
  processeur, le rendu en WAV/MP3.
- **Le client Windows de Steam et d'Epic** au-delà de leur mise à jour : à
  essayer sur un vrai PC avant de dire qu'ils ne marchent pas ; on propose de
  toute façon Steam pour Raye et Heroic.
- **Paint.NET** avec le correctif crypt32 : son installeur plante sur la question
  de la carte graphique sans GPU.
- **La capture d'écran des programmes Windows** (OBS, Discord, TikTok LIVE
  Studio pour Windows) : noire dans le paquet raye-wine publié ; corrigée dans
  le code (une seule source demandée au portail, chaque écran découpé dans un
  partage du bureau entier) et prouvée dans la VM de test avec la bibliothèque
  reconstruite, pas encore dans un paquet. Restent à mesurer sur un vrai PC :
  le décalage image/son, le 1080p60 et la capture de jeu.

## Les listes

### Bloqués par leur anti-triche (53)

| Titre | Anti-triche | Pourquoi (affiché avant le téléchargement) | Source |
|---|---|---|---|
| 2XKO | Vanguard | Refusé ici : son anti-triche est un pilote noyau Windows. | AWACY 6c26305: Denied |
| Apex Legends | Easy Anti-Cheat, Hyperion | Refusé ici : son anti-triche est une protection que son éditeur a retirée ici. | AWACY 6c26305: Denied |
| Arena Breakout: Infinite | anti-cheat-expert | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Battlefield 1 | fairfight, EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Battlefield 6 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Battlefield REDSEC | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Battlefield V | fairfight, EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Battlefield™ 2042 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Breachers | Easy Anti-Cheat | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Call of Duty: Black Ops 7 | ricochet | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Concord | Easy Anti-Cheat | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Counter-Strike 2 (ESEA) | esea | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Counter-Strike 2 (ESL) | esl-wire | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Counter-Strike 2 (FACEIT) | faceit | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Counter-Strike: Global Offensive (ESEA) | esea | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Counter-Strike: Global Offensive (FACEIT) | faceit | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| CRSED: Cuisine Royale | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Delta Force: Hawk Ops | anti-cheat-expert | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Destiny 2 | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS FC™ 24 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS FC™ 25 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS FC™ 26 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS™ FIFA 23 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS™ Madden NFL 24 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS™ Madden NFL 25 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS™ Madden NFL 26 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| EA SPORTS™ WRC 24 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Exoborne | Easy Anti-Cheat, anybrain | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| F1® 24 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| F1® 25 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Farlight 84 | netease-yidun | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| FiveM | unknown-custom | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Forefront | Easy Anti-Cheat | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Fortnite | Easy Anti-Cheat | Epic n'a pas activé la partie Linux de son anti-triche ; le jeu refuse de démarrer ici. | areweanticheatyet.com games.json @ 6c26305 (2026-10-24): Denied, Easy Anti-Cheat |
| Ghost of Tsushima Director's Cut | playstation-overlay | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Ghosts of Tabor | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Grand Theft Auto V | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Grand Theft Auto VI | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| League of Legends | Vanguard | Refusé ici : son anti-triche est un pilote noyau Windows. | AWACY 6c26305: Denied |
| Marathon | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Plants vs Zombies: Garden Warfare 2 | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Plants vs. Zombies: Battle for Neighborville | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Post Scriptum | Easy Anti-Cheat | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Roblox | Hyperion | Roblox bloque ce système exprès ; c'est à son éditeur de l'autoriser. | areweanticheatyet.com games.json @ 6c26305 (2026-10-24): Denied, Hyperion, update "Wine Support Yanked" 2024-03-01 |
| Rust | Easy Anti-Cheat, VAC | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| skate. | EA Javelin | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Spectre Divide | BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Teamfight Tactics | Vanguard | Refusé ici : son anti-triche est un pilote noyau Windows. | AWACY 6c26305: Denied |
| Tom Clancy's Rainbow Six® Siege | fairfight, BattlEye | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Tom Clancy's Rainbow Six® Siege X | fairfight, BattlEye, r6-shieldguard | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| Valorant | Vanguard | Son anti-triche est un pilote noyau Windows : aucune couche ne peut l'exécuter. Riot a dit non. | areweanticheatyet.com games.json @ 6c26305 (2026-10-24): Denied, Vanguard, 2024-01-26 "Vanguard developer claims Linux has too much attack surface" |
| Vampire: The Masquerade - Bloodhunt | Easy Anti-Cheat, anti-cheat-expert | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |
| World of Tanks (ESL) | esl-wire | Son éditeur n'a pas activé la partie Linux de son anti-triche. | AWACY 6c26305: Denied |

### Bloqués pour une autre raison (1)

| Titre | Pourquoi (affiché avant le téléchargement) | Source |
|---|---|---|
| Minecraft for Windows (launcher and Bedrock Edition) | Son lanceur Windows et l'édition Bedrock exigent le Microsoft Store et le Windows Runtime, absents de ce système. L'édition Java a un lanceur Linux : il est dans le Store. | measured 2026-11-11, raye-wine 11.21: MinecraftInstaller.exe from minecraft.net quits with "Could not load file or assembly 'Windows.Foundation.UniversalApiContract'" and no window; minecraft.net/download (read 2026-11-11) offers Linux only the Java launcher (Debian, Arch, other distributions) and installs the Windows one through the Microsoft Store |

### Anti-triche activé pour Linux par l'éditeur, vendus sur Steam (153)

Chemin : Steam pour Raye (Store) puis Proton. Aucun n'a encore été joué sur Raye.

| Titre | Anti-triche | Steam |
|---|---|---|
| 7 Days to Die | Easy Anti-Cheat | [251570](https://store.steampowered.com/app/251570) |
| Albion Online | Easy Anti-Cheat, BattlEye | [761890](https://store.steampowered.com/app/761890) |
| ARK: Survival Ascended | BattlEye | [2399830](https://store.steampowered.com/app/2399830) |
| ARK: Survival Evolved | BattlEye, VAC | [346110](https://store.steampowered.com/app/346110) |
| ARK: Survival of the Fittest | BattlEye | [407530](https://store.steampowered.com/app/407530) |
| ArmA 3 | BattlEye | [107410](https://store.steampowered.com/app/107410) |
| ArmA Reforger | BattlEye | [1874880](https://store.steampowered.com/app/1874880) |
| Armello | VAC | [290340](https://store.steampowered.com/app/290340) |
| Awesomenauts - the 2D moba | VAC | [204300](https://store.steampowered.com/app/204300) |
| Back 4 Blood | Easy Anti-Cheat | [924970](https://store.steampowered.com/app/924970) |
| BATTALION: Legacy | Easy Anti-Cheat, faceit | [489940](https://store.steampowered.com/app/489940) |
| BattleCore Arena | Easy Anti-Cheat | [689570](https://store.steampowered.com/app/689570) |
| Battlefield 2 | PunkBuster | [24860](https://store.steampowered.com/app/24860) |
| Battlefield 4 | PunkBuster, fairfight | [1238860](https://store.steampowered.com/app/1238860) |
| Battlefield: Bad Company 2 | PunkBuster | [24960](https://store.steampowered.com/app/24960) |
| Black Squad | BattlEye, nprotect-gameguard | [550650](https://store.steampowered.com/app/550650) |
| Bless Unleashed | Easy Anti-Cheat | [1254120](https://store.steampowered.com/app/1254120) |
| Blindfire | Easy Anti-Cheat | [2854480](https://store.steampowered.com/app/2854480) |
| BrainBread 2 | VAC | [346330](https://store.steampowered.com/app/346330) |
| Brawlhalla | Easy Anti-Cheat | [291550](https://store.steampowered.com/app/291550) |
| Caliber | Easy Anti-Cheat | [307950](https://store.steampowered.com/app/307950) |
| Call of Duty | PunkBuster | [2620](https://store.steampowered.com/app/2620) |
| Call of Duty 2 | PunkBuster | [2630](https://store.steampowered.com/app/2630) |
| Call of Duty 4: Modern Warfare | PunkBuster | [7940](https://store.steampowered.com/app/7940) |
| Call of Duty: Black Ops | treyarch-anti-cheat, VAC, PunkBuster | [42700](https://store.steampowered.com/app/42700) |
| Call of Duty: Black Ops II | VAC, treyarch-anti-cheat | [202970](https://store.steampowered.com/app/202970) |
| Call of Duty: Ghosts | VAC | [209160](https://store.steampowered.com/app/209160) |
| Call of Duty: Infinite Warfare | VAC | [292730](https://store.steampowered.com/app/292730) |
| Call of Duty: Modern Warfare 2 | VAC | [10180](https://store.steampowered.com/app/10180) |
| Call of Duty: Modern Warfare 3 | VAC | [42680](https://store.steampowered.com/app/42680) |
| Call of Duty: World at War | PunkBuster, treyarch-anti-cheat | [10090](https://store.steampowered.com/app/10090) |
| Call of Duty: WWII | VAC | [476600](https://store.steampowered.com/app/476600) |
| Call of Duty®: Black Ops III | VAC, treyarch-anti-cheat | [311210](https://store.steampowered.com/app/311210) |
| Chivalry: Medieval Warfare | VAC | [219640](https://store.steampowered.com/app/219640) |
| Counter-Strike | VAC | [10](https://store.steampowered.com/app/10) |
| Counter-Strike: Condition Zero | VAC | [80](https://store.steampowered.com/app/80) |
| Crossout | Easy Anti-Cheat | [386180](https://store.steampowered.com/app/386180) |
| Cry of Fear | VAC | [223710](https://store.steampowered.com/app/223710) |
| Day of Defeat | VAC | [30](https://store.steampowered.com/app/30) |
| Day of Defeat: Source | VAC | [300](https://store.steampowered.com/app/300) |
| Day of Infamy | VAC, BattlEye | [447820](https://store.steampowered.com/app/447820) |
| DayZ | BattlEye, VAC | [221100](https://store.steampowered.com/app/221100) |
| Dead By Daylight | Easy Anti-Cheat | [381210](https://store.steampowered.com/app/381210) |
| Deadlock | VAC | [1422450](https://store.steampowered.com/app/1422450) |
| Deathmatch Classic | VAC | [40](https://store.steampowered.com/app/40) |
| Deceit | Easy Anti-Cheat | [466240](https://store.steampowered.com/app/466240) |
| Depth | VAC | [274940](https://store.steampowered.com/app/274940) |
| Diabotical Rogue | Easy Anti-Cheat | [2805060](https://store.steampowered.com/app/2805060) |
| DiRT Rally | VAC | [310560](https://store.steampowered.com/app/310560) |
| DJMAX RESPECT V | xigncode3 | [960170](https://store.steampowered.com/app/960170) |
| DotA 2 | VAC | [570](https://store.steampowered.com/app/570) |
| Dota Underlords | VAC | [1046930](https://store.steampowered.com/app/1046930) |
| Dungeon Defenders | VAC | [65800](https://store.steampowered.com/app/65800) |
| Dying Light | VAC | [304430](https://store.steampowered.com/app/304430) |
| Dystopia | VAC | [912630](https://store.steampowered.com/app/912630) |
| Elden Ring | Easy Anti-Cheat | [1245620](https://store.steampowered.com/app/1245620) |
| Enlisted: Reinforced | Easy Anti-Cheat, BattlEye | [2051620](https://store.steampowered.com/app/2051620) |
| Eternal Return | xigncode3, uncheater | [528010](https://store.steampowered.com/app/528010) |
| EZ2ON REBOOT : R | xigncode3 | [1477590](https://store.steampowered.com/app/1477590) |
| Far Cry 5 | Easy Anti-Cheat | [552520](https://store.steampowered.com/app/552520) |
| For Honor | Easy Anti-Cheat | [304390](https://store.steampowered.com/app/304390) |
| Foxhole | VAC | [505460](https://store.steampowered.com/app/505460) |
| Gemini: Binary Conflict | Easy Anti-Cheat | [1293660](https://store.steampowered.com/app/1293660) |
| GrandChase | x-trap, nprotect-gameguard | [985810](https://store.steampowered.com/app/985810) |
| GRID Autosport | VAC | [255220](https://store.steampowered.com/app/255220) |
| Gunfire Reborn | unknown-custom | [1217060](https://store.steampowered.com/app/1217060) |
| Half-Life | VAC | [70](https://store.steampowered.com/app/70) |
| Halo Infinite | arbiter, Easy Anti-Cheat | [1240440](https://store.steampowered.com/app/1240440) |
| Halo: The Master Chief Collection | Easy Anti-Cheat | [976730](https://store.steampowered.com/app/976730) |
| Harry Potter: Quidditch Champions | Easy Anti-Cheat | [2878600](https://store.steampowered.com/app/2878600) |
| Hell Let Loose | Easy Anti-Cheat | [686810](https://store.steampowered.com/app/686810) |
| Hero Siege | Easy Anti-Cheat | [269210](https://store.steampowered.com/app/269210) |
| Hired Ops | saberclaw | [374280](https://store.steampowered.com/app/374280) |
| Hoard | VAC | [63000](https://store.steampowered.com/app/63000) |
| Homefront | VAC | [55100](https://store.steampowered.com/app/55100) |
| Hurtworld | Easy Anti-Cheat | [393420](https://store.steampowered.com/app/393420) |
| Insurgency | VAC, BattlEye | [222880](https://store.steampowered.com/app/222880) |
| Insurgency: Sandstorm | Easy Anti-Cheat | [581320](https://store.steampowered.com/app/581320) |
| Ironsight | Easy Anti-Cheat | [1940990](https://store.steampowered.com/app/1940990) |
| KartRider: Drift | nexon-game-security | [1184140](https://store.steampowered.com/app/1184140) |
| Killer Klowns from Outer Space: The Game | Easy Anti-Cheat | [1556100](https://store.steampowered.com/app/1556100) |
| Killing Floor | VAC | [1250](https://store.steampowered.com/app/1250) |
| Killing Floor 2 | VAC | [232090](https://store.steampowered.com/app/232090) |
| Knockout City | Easy Anti-Cheat | [1301210](https://store.steampowered.com/app/1301210) |
| KurtzPel | nprotect-gameguard | [844870](https://store.steampowered.com/app/844870) |
| Left 4 Dead 2 | VAC | [550](https://store.steampowered.com/app/550) |
| Lost Planet: Extreme Condition | VAC | [6510](https://store.steampowered.com/app/6510) |
| MEGATON MUSASHI W: WIRED | Easy Anti-Cheat | [2450450](https://store.steampowered.com/app/2450450) |
| MicroVolts: Recharged | xigncode3 | [1426440](https://store.steampowered.com/app/1426440) |
| Microworks | VAC | [1233410](https://store.steampowered.com/app/1233410) |
| Midair | Easy Anti-Cheat | [439370](https://store.steampowered.com/app/439370) |
| Mini Royale | Easy Anti-Cheat | [1657090](https://store.steampowered.com/app/1657090) |
| Momentum Mod | VAC | [1802710](https://store.steampowered.com/app/1802710) |
| Mordhau | VAC | [629760](https://store.steampowered.com/app/629760) |
| Mount & Blade II: Bannerlord | BattlEye | [261550](https://store.steampowered.com/app/261550) |
| Multiversus | Easy Anti-Cheat | [1818750](https://store.steampowered.com/app/1818750) |
| MY HERO ULTRA RUMBLE | Easy Anti-Cheat | [1607250](https://store.steampowered.com/app/1607250) |
| NARAKA: BLADEPOINT | neac-protect | [1203220](https://store.steampowered.com/app/1203220) |
| No More Room in Hell | VAC | [224260](https://store.steampowered.com/app/224260) |
| NosTale | x-trap, nprotect-gameguard | [550470](https://store.steampowered.com/app/550470) |
| Nuclear Dawn | VAC | [17710](https://store.steampowered.com/app/17710) |
| Out of Reach | VAC | [327090](https://store.steampowered.com/app/327090) |
| Overwatch 2 | warden | [2357570](https://store.steampowered.com/app/2357570) |
| Phantasy Star Online 2 (NA) | nprotect-gameguard | [1056640](https://store.steampowered.com/app/1056640) |
| Pirates, Vikings, and Knights II | VAC | [17570](https://store.steampowered.com/app/17570) |
| PlanetSide 2 | BattlEye | [218230](https://store.steampowered.com/app/218230) |
| POLYGON | Easy Anti-Cheat | [1241100](https://store.steampowered.com/app/1241100) |
| Predecessor | Easy Anti-Cheat | [961200](https://store.steampowered.com/app/961200) |
| Quake 4 | PunkBuster | [2210](https://store.steampowered.com/app/2210) |
| Quake Champions | unknown-custom | [611500](https://store.steampowered.com/app/611500) |
| Quake III Arena | PunkBuster | [2200](https://store.steampowered.com/app/2200) |
| Quake Live | PunkBuster, VAC | [282440](https://store.steampowered.com/app/282440) |
| Red Faction®: Armageddon™ | VAC | [55110](https://store.steampowered.com/app/55110) |
| Ricochet | VAC | [60](https://store.steampowered.com/app/60) |
| Riders Republic | BattlEye | [2290180](https://store.steampowered.com/app/2290180) |
| Robocraft | Easy Anti-Cheat | [301520](https://store.steampowered.com/app/301520) |
| Rogue Company | Easy Anti-Cheat | [872200](https://store.steampowered.com/app/872200) |
| Sea of Thieves | Easy Anti-Cheat | [1172620](https://store.steampowered.com/app/1172620) |
| Shatterline | Easy Anti-Cheat | [2087030](https://store.steampowered.com/app/2087030) |
| SMITE | Easy Anti-Cheat | [386360](https://store.steampowered.com/app/386360) |
| Smite 2 | Easy Anti-Cheat | [2437170](https://store.steampowered.com/app/2437170) |
| Splitgate | equ8 | [677620](https://store.steampowered.com/app/677620) |
| Squad | Easy Anti-Cheat, VAC | [393380](https://store.steampowered.com/app/393380) |
| STAR WARS™ Battlefront™ (2015) | fairfight | [1237980](https://store.steampowered.com/app/1237980) |
| STAR WARS™ Battlefront™ II (2017) | fairfight | [1237950](https://store.steampowered.com/app/1237950) |
| STAR WARS™: Squadrons | Easy Anti-Cheat | [1222730](https://store.steampowered.com/app/1222730) |
| Starsiege: Deadzone | Easy Anti-Cheat | [2170420](https://store.steampowered.com/app/2170420) |
| Suicide Squad: Kill the Justice League | Easy Anti-Cheat | [315210](https://store.steampowered.com/app/315210) |
| Sunset World Online | Easy Anti-Cheat | [1903270](https://store.steampowered.com/app/1903270) |
| Super Animal Royale | unknown-custom | [843380](https://store.steampowered.com/app/843380) |
| Sven Co-op | VAC | [225840](https://store.steampowered.com/app/225840) |
| Synergy | VAC | [17520](https://store.steampowered.com/app/17520) |
| Team Fortress 2 | VAC | [440](https://store.steampowered.com/app/440) |
| Team Fortress Classic | VAC | [20](https://store.steampowered.com/app/20) |
| The Crew™ Motorfest | BattlEye | [2698940](https://store.steampowered.com/app/2698940) |
| The Culling | Easy Anti-Cheat | [437220](https://store.steampowered.com/app/437220) |
| THE FINALS | Easy Anti-Cheat, denuvo-anti-cheat, anybrain | [2073850](https://store.steampowered.com/app/2073850) |
| The Mean Greens: Plastic Warfare | VAC | [360940](https://store.steampowered.com/app/360940) |
| The Ship: Murder Party | VAC | [2400](https://store.steampowered.com/app/2400) |
| Titanfall | fairfight | [1454890](https://store.steampowered.com/app/1454890) |
| Titanfall 2 | fairfight | [1237970](https://store.steampowered.com/app/1237970) |
| Total War: SHOGUN 2 | VAC | [34330](https://store.steampowered.com/app/34330) |
| Totally Accurate Battlegrounds | Easy Anti-Cheat | [823130](https://store.steampowered.com/app/823130) |
| Tower Unite | VAC | [394690](https://store.steampowered.com/app/394690) |
| Tower Wars | VAC | [214360](https://store.steampowered.com/app/214360) |
| TOXIKK | VAC | [324810](https://store.steampowered.com/app/324810) |
| Tree of Savior | VAC, nexon-game-security | [372000](https://store.steampowered.com/app/372000) |
| Unturned | VAC, BattlEye | [304930](https://store.steampowered.com/app/304930) |
| VRChat | Easy Anti-Cheat | [438100](https://store.steampowered.com/app/438100) |
| War Thunder | BattlEye | [236390](https://store.steampowered.com/app/236390) |
| Warframe | unknown-custom | [230410](https://store.steampowered.com/app/230410) |
| Wargame: European Escalation | VAC | [58610](https://store.steampowered.com/app/58610) |
| Zombie Panic! Source | VAC | [17500](https://store.steampowered.com/app/17500) |

### Anti-triche activé pour Linux, sans numéro Steam dans la liste source (42)

La liste source ne dit pas où ils sont vendus. Plusieurs sont pourtant sur Steam : à vérifier un par un avant d'indiquer un chemin.

| Titre | Anti-triche |
|---|---|
| 100% Orange Juice | VAC |
| Armored Core VI: Fires of Rubicon | Easy Anti-Cheat |
| Ballistic Overkill | VAC |
| Black Mesa | VAC |
| Call of Duty: Black Ops IIII | treyarch-anti-cheat |
| Counter-Strike 2 | VAC |
| Counter-Strike: Global Offensive | VAC |
| Counter-Strike: Source | VAC |
| CRSED: F.O.A.D. | Easy Anti-Cheat |
| Cyberless: Online | Easy Anti-Cheat |
| Dark Messiah of Might & Magic | VAC |
| Dead Island | VAC |
| DiRT 4 | VAC |
| Dragonica | ahnlab-hackshield |
| EA SPORTS™ Madden NFL 23 | Easy Anti-Cheat |
| Element Quest Online : 2D MMORPG | Easy Anti-Cheat |
| Evil Dead: The Game | Easy Anti-Cheat |
| Garry's Mod | VAC |
| Half-Life 2: Deathmatch | VAC |
| Half-Life Deathmatch: Source | VAC |
| Half-Life: Opposing Force | VAC |
| Hunt: Showdown 1896 | Easy Anti-Cheat |
| Jabroni Brawl: Episode 3 | VAC |
| Legacy: Steel & Sorcery | Easy Anti-Cheat |
| Level Zero: Extraction | Easy Anti-Cheat |
| LIGHTNITE | Easy Anti-Cheat |
| Lords of the Fallen (2023) | Easy Anti-Cheat |
| New World | Easy Anti-Cheat |
| Outriders | Easy Anti-Cheat |
| Path Of Titans | BattlEye |
| Pixel Gun 3D: PC Edition | Easy Anti-Cheat |
| Red Orchestra: Ostfront 41-45 | VAC |
| s&box | VAC |
| Serious Sam 3 | VAC |
| Tallowmere 2: Curse of the Kittens | VAC |
| Tibia | BattlEye, emac-lab |
| Tom Clancy's Ghost Recon Breakpoint | BattlEye |
| Tom Clancy's The Division | fairfight |
| Tom Clancy's The Division 2 | Easy Anti-Cheat |
| TRIBES 3: Rivals | Easy Anti-Cheat, denuvo-anti-cheat |
| Watch Dogs®: Legion | BattlEye |
| XDefiant | BattlEye |
