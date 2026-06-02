# Background Mask Post-Processing

This document tracks the local mask-processing pipeline used to improve rough or badly defined portrait edges after
the segmentation model has produced its foreground mask.

## Signal Convention

The model output is treated as a foreground confidence mask. The post-processing module converts it into a background
alpha mask:

- `255` means background is fully selected.
- `0` means foreground is protected.
- Intermediate values keep soft transitions around hair, hands, shoulders, and motion blur.

The implementation lives in `src/background/mask-post-processing.cpp`, with unit coverage in
`tests/background/mask-post-processing-test.cpp`.

## Pipeline

1. Resize the model mask to the OBS frame size with linear interpolation.
2. Convert foreground confidence into a background mask.
3. Apply soft thresholding when `edge_softness` is enabled, using a smooth transition instead of a binary cut.
4. Remove small background pinholes inside the foreground with `foreground_cleanup`.
5. Remove tiny isolated background components with `contour_filter`.
6. Smooth the contour with `smooth_contour`, using `stackBlur` when available and `blur` on older OpenCV builds.
7. Expand or contract the mask with `mask_expansion`.
8. Feather broad transitions with `feather`.
9. Refine only the edge band with the source image, so strong visual edges are preserved and weak edges are blended.
10. Apply temporal smoothing between frames, with asymmetric blending that protects foreground details from disappearing
    too quickly.

## Edge Controls

| OBS setting | Internal field | Default | Purpose |
| --- | --- | ---: | --- |
| Threshold | `threshold` | `0.50` | Base foreground/background decision point. |
| Edge softness | `edgeSoftness` | `0.05` | Keeps useful alpha values around the threshold. |
| Edge refinement | `edgeRefinement` | `0.25` | Uses source-image color continuity to sharpen real edges and soften weak ones. |
| Foreground cleanup | `foregroundCleanup` | `0.15` | Removes small accidental background holes in the subject. |
| Contour filter | `contourFilter` | `0.05` | Drops tiny background islands. |
| Smooth contour | `smoothContour` | `0.50` | Stabilizes jagged model output before expansion and feathering. |
| Feather | `feather` | `0.00` | Adds a wider, deliberately soft alpha transition. |
| Temporal smooth factor | `temporalSmoothFactor` | `0.85` | Reduces frame-to-frame flicker. |

## Tuning Notes

- Raise `edge_refinement` first when the contour is noisy but the camera image has a visible color boundary.
- Raise `edge_softness` when the model output is too binary or makes the subject look cut out.
- Raise `foreground_cleanup` when small transparent holes appear inside the face, clothes, or hands.
- Lower `smooth_contour` and `temporal_smooth_factor` if fast hand movement starts to smear.
- Keep `feather` low for webcam use. It is useful for stylized blur, but can look hazy with virtual backgrounds.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
>
> SPDX-License-Identifier: GPL-3.0-or-later  
