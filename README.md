# Comment devenir beau gosse dans OBS

BGOBS, pour **Beau Gosse OBS**, est un plugin OBS Studio qui retire l'arrière-plan d'une webcam ou d'une source vidéo et aide à obtenir un contour plus propre autour du sujet.

La version 0.1 est une première version de transition : le plugin affiche l'identité BGOBS, commence à isoler le traitement du masque dans un cœur Rust, mais conserve encore le nom technique `obs-backgroundremoval` pour rester compatible avec l'installation, les chemins OBS existants et les paquets hérités du projet d'origine.

## Ce que fait BGOBS

- ajoute un filtre OBS nommé **Rend-moi beau gosse** dans l'interface française ;
- segmente le sujet à l'aide des modèles ONNX déjà fournis par le plugin d'origine ;
- transforme la prédiction du modèle en masque d'arrière-plan exploitable par OBS ;
- applique un lissage temporel pour réduire les contours instables d'une image à l'autre ;
- conserve les fonctions existantes de remplacement de fond, flou, couleur transparente et amélioration basse lumière.

L'objectif de BGOBS n'est pas seulement de retirer le fond. Le but est d'obtenir une image qui tient mieux en direct : moins de halo, moins de bord cranté, moins de masque qui tremble.

## Version 0.1

Cette version pose les bases du nouveau projet :

- nom public : **BGOBS** ;
- nom long : **Beau Gosse OBS** ;
- version plugin : `0.1.0` ;
- filtre français : **Rend-moi beau gosse** ;
- cœur Rust : `crates/bgobs-core` ;
- pont C/Rust exposé par `crates/bgobs-core/include/bgobs_core.h` ;
- intégration CMake pour compiler et lier le cœur Rust avec le plugin OBS.

Le code C++ reste majoritaire pour l'instant. Le Rust prend en charge les opérations de masque les plus faciles à isoler proprement : seuil, bord doux, inversion et lissage temporel.

## Utilisation dans OBS

1. Ajoute ou sélectionne une source vidéo.
2. Ouvre les filtres de la source.
3. Ajoute le filtre **Rend-moi beau gosse**.
4. Choisis le modèle de segmentation adapté à ta machine.
5. Ajuste le seuil, le bord doux et le lissage jusqu'à obtenir un contour stable.

Le bon réglage dépend beaucoup de la lumière. Une lumière frontale douce donne souvent un meilleur résultat qu'un modèle plus lourd.

## Installation Linux

Pour une installation utilisateur OBS, le plugin doit être disposé comme ceci :

```text
~/.config/obs-studio/plugins/obs-backgroundremoval/
├── bin/64bit/obs-backgroundremoval.so
└── data/
```

Les bibliothèques ONNX Runtime doivent être accessibles au chargement du plugin. Dans notre installation locale, elles sont placées dans le même dossier que le `.so` :

```text
~/.config/obs-studio/plugins/obs-backgroundremoval/bin/64bit/
├── obs-backgroundremoval.so
├── libonnxruntime.so
├── libonnxruntime.so.1
├── libonnxruntime.so.1.23.2
└── libonnxruntime_providers_shared.so
```

Si OBS charge encore une ancienne version installée dans `/usr/lib`, il faut remplacer le plugin système avec les droits administrateur ou retirer l'ancien paquet.

## Installation Windows PortableApps

Pour OBS PortableApps, installe le plugin dans le layout OBS embarqué :

```text
OBSPortable/
├── App/obs-studio/obs-plugins/64bit/
│   ├── obs-backgroundremoval.dll
│   ├── onnxruntime.dll
│   └── onnxruntime_providers_shared.dll
├── App/obs-studio/data/obs-plugins/obs-backgroundremoval/
└── Data/obs-plugins/obs-backgroundremoval/
```

Avant de copier une nouvelle version, supprime les anciennes copies du plugin dans `App/obs-studio/obs-plugins/64bit`, `App/obs-studio/data/obs-plugins`, `Data/obs-plugins`, `Data/obs-studio/plugins` et `Data/config/obs-studio/plugins`. Une seule copie de `obs-backgroundremoval.dll` doit rester.

Une fois copié, relance OBS et vérifie dans les logs que le plugin charge la version `0.1.0`.

## Compiler

Les commandes Rust utiles :

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all --check
```

Le plugin OBS se compile ensuite avec CMake. Exemple avec un dossier de build local déjà configuré :

```bash
cmake --build build/local-obs --target obs-backgroundremoval
ctest --test-dir build/local-obs --output-on-failure -R mask-post-processing
```

Sur Windows, la distribution est produite par le workflow GitHub Actions **Windows Package**.

## Organisation du projet

```text
crates/bgobs-core/                  Cœur Rust du traitement de masque
crates/bgobs-core/include/          Interface C publique du cœur Rust
src/background/                     Intégration OBS et post-traitement C++
data/locale/                        Libellés affichés dans OBS
data/models/                        Modèles ONNX et licences associées
docs/                               Notes techniques et installation
```

La frontière actuelle est volontairement simple : le C++ reste responsable d'OBS, des textures, des propriétés et du cycle de vie plugin ; Rust gère progressivement les calculs purs, testables sans OBS.

## Qualité

Avant de pousser une modification, vérifier au minimum :

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all --check
.venv/bin/reuse lint
.venv/bin/gersemi --check CMakeLists.txt
```

Pour les changements C ou C++ :

```bash
.venv/bin/clang-format --dry-run --Werror src/background/mask-post-processing.cpp src/plugin-support.c
```

## Origine et licence

BGOBS est dérivé de **OBS Background Removal** de Roy Shilkrot et Kaito Udagawa.

Le projet reste distribué sous licence **GPL-3.0-or-later**. Les modèles embarqués ont leurs propres licences dans `data/models/*.license` et doivent être conservés avec les fichiers correspondants.

Les contributions destinées au projet d'origine doivent respecter les règles de contribution upstream. Pour BGOBS, l'objectif est de garder un historique propre, des changements relisibles et des tests reproductibles.

<!--
SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
