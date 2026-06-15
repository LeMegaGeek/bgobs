# Comment devenir beau gosse dans OBS

BGOBS, pour **Beau Gosse OBS**, est un plugin OBS Studio qui retire ou floute l'arriere-plan d'une webcam tout en cherchant un contour plus propre autour du sujet.

La version `0.3.0` ajoute une source **CaCam USB** pour utiliser un telephone
Android comme camera OBS sans partage de connexion USB, sans ADB et sans
modifier les routes reseau du PC.

## Ce Que Fait BGOBS

- ajoute un filtre OBS nomme **Rend-moi beau gosse** dans l'interface francaise ;
- expose des styles de masque **Naturel**, **Studio**, **Net** et **Performance** ;
- propose un **apercu du masque** pour regler le contour rapidement ;
- applique un lissage temporel pour limiter les contours qui tremblent ;
- affine les bords avec l'image source pour reduire les halos ;
- conserve le flou d'arriere-plan et l'amelioration basse lumiere du projet d'origine.
- ajoute une source OBS **CaCam USB** compatible avec le preset `USB BGOBS` de
  l'application Android CaCam.

L'objectif n'est pas seulement de supprimer le fond. BGOBS vise une image plus propre en direct : moins de bord crante, moins de halo autour des cheveux et des epaules, moins d'instabilite d'une image a l'autre.

## Version 0.3.0

Cette version ajoute l'integration CaCam USB :

- nouvelle source OBS : `bgobs_cacam_usb_source` ;
- activation Android Open Accessory cote PC via `libusb` charge dynamiquement ;
- reception de frames NV21 depuis CaCam Android ;
- conversion YUV vers BGRA avec OpenCV avant sortie video OBS ;
- aucun partage de connexion USB et aucun debogage USB requis.

Le filtre BGOBS reste separe : ajoute la source **CaCam USB**, puis ajoute le
filtre **Rend-moi beau gosse** sur cette source.

## Version 0.2.0

Cette version pose la base propre du nouveau projet :

- nom court : **BGOBS** ;
- nom long : **Beau Gosse OBS** ;
- module OBS : `bgobs` ;
- binaire Linux : `bgobs.so` ;
- binaire Windows : `bgobs.dll` ;
- dossier de donnees OBS : `bgobs` ;
- filtre principal : `bgobs_background_removal` ;
- filtre portrait : `bgobs_enhance_portrait` ;
- bundle ID : `net.lemegageek.bgobs` ;
- coeur Rust : `crates/bgobs-core`.

Le C++ garde l'integration OBS, les textures, les proprietes et ONNX Runtime. Rust prend progressivement en charge les traitements purs du masque, plus faciles a tester sans OBS.

## Utilisation Dans OBS

Avec une source existante :

1. Ajoute ou selectionne une source video.
2. Ouvre les filtres de la source.
3. Ajoute **Rend-moi beau gosse**.
4. Choisis un style BGOBS.
5. Active **Apercu du masque** si tu veux regler le contour finement.
6. Desactive l'apercu quand le resultat te convient.

Avec CaCam en USB direct :

1. Branche le telephone Android au PC en USB.
2. Dans CaCam, choisis le preset **USB BGOBS** et demarre le flux.
3. Dans OBS, ajoute la source **CaCam USB**.
4. Accepte l'autorisation USB sur le telephone si Android la demande.
5. Ajoute le filtre **Rend-moi beau gosse** sur la source **CaCam USB**.

Une lumiere frontale douce reste souvent plus efficace qu'un modele plus lourd. Les contours difficiles viennent souvent d'un contre-jour, d'un fond trop proche de la couleur des cheveux, ou d'une webcam trop compressee.

## Installation Linux Utilisateur

Pour une installation OBS locale sans droits administrateur :

```text
~/.config/obs-studio/plugins/bgobs/
├── bin/64bit/bgobs.so
└── data/
```

Les bibliotheques ONNX Runtime doivent etre accessibles au chargement du plugin. Pour notre build local, elles sont placees a cote du `.so` :

```text
~/.config/obs-studio/plugins/bgobs/bin/64bit/
├── bgobs.so
├── libonnxruntime.so
├── libonnxruntime.so.1
├── libonnxruntime.so.1.23.2
└── libonnxruntime_providers_shared.so
```

Si l'ancien paquet systeme est encore installe, OBS peut aussi afficher l'ancien filtre. Il n'entre plus en collision avec BGOBS `0.3.0`, mais il vaut mieux le retirer :

```bash
sudo apt remove obs-backgroundremoval
```

## Installation Windows PortableApps

Pour OBS PortableApps, le ZIP Windows doit contenir un dossier `bgobs`.

Copie les DLL dans :

```text
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\
```

Copie les donnees dans les deux emplacements utilises par PortableApps :

```text
<OBS-StudioPortable>\App\obs-studio\data\obs-plugins\bgobs\
<OBS-StudioPortable>\Data\obs-plugins\bgobs\
```

La disposition finale doit inclure :

```text
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\bgobs.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\onnxruntime.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\onnxruntime_providers_shared.dll
<OBS-StudioPortable>\App\obs-studio\data\obs-plugins\bgobs\models\
<OBS-StudioPortable>\Data\obs-plugins\bgobs\models\
```

Avant de copier une nouvelle version, lance `remove-old-installation.bat` depuis le ZIP. Il retire les anciennes copies `obs-backgroundremoval` et les anciennes copies `bgobs`.

## Compiler

Rust :

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all --check
```

Plugin OBS local :

```bash
cmake --build build/local-obs --target bgobs
ctest --test-dir build/local-obs --output-on-failure
```

Controles de qualite :

```bash
.venv/bin/gersemi --check CMakeLists.txt
.venv/bin/reuse lint
git diff --check
```

## Origine Et Licence

BGOBS est derive de **OBS Background Removal** de Roy Shilkrot et Kaito Udagawa.

Le projet reste distribue sous licence **GPL-3.0-or-later**. Les modeles embarques ont leurs propres licences dans `data/models/*.license` et doivent rester avec les fichiers correspondants.

<!--
SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
