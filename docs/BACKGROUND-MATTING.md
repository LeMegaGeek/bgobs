<!-- SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Détourage haute résolution

BGOBS utilise désormais Robust Video Matting (RVM) pour les nouveaux filtres et
pour la migration automatique des anciens filtres de segmentation. RVM reçoit
l’image à sa résolution source et conserve son alpha flottant jusqu’au rendu
OBS. L’inférence s’exécute dans un worker à file de profondeur 1 afin de ne pas
bloquer le rendu.

## Modes

- **Automatique** choisit un ratio RVM adapté à la résolution et conserve une
  sortie fluide sur CPU.
- **Qualité maximale** suit les ratios RVM recommandés pour les cheveux et les
  mouvements rapides.
- **Performance** utilise MediaPipe pour les machines modestes.
- **Ancien moteur** restaure le modèle conservé dans `legacy_model_select`.

Le choix du dispositif est automatique par défaut. Les builds contenant le
provider DirectML peuvent accélérer l’inférence sur les GPU Windows AMD, Intel
et NVIDIA ; le CPU reste toujours disponible en repli. Linux conserve un
chemin CPU portable et peut exposer CUDA/ROCm lorsqu’ils sont fournis par le
runtime installé.

Les collections de scènes existantes sont migrées une fois vers le mode
Automatique. Le modèle précédent est conservé pour permettre un retour au
mode Ancien moteur. Les filtres de profondeur TCMonoDepth ne sont pas migrés.

BGOBS est distribué pour Windows et Linux. Aucune release macOS n’est prévue.
