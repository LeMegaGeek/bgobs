# Third-party notices

BGOBS includes or interoperates with third-party software and model assets. The
BGOBS license does not replace their licenses. Exact file-level licensing is
recorded with SPDX notices, `REUSE.toml`, adjacent `.license` files, and the
license texts in `LICENSES/`.

## Embedded model assets

The source tree contains original ONNX models under `models/` and optimized
runtime copies under `data/models/`. Each binary has an adjacent `.license`
file. The current inventory is:

| Model asset                                 | SPDX license |
| ------------------------------------------- | ------------ |
| SINet Softmax                               | MIT          |
| MediaPipe segmentation                      | Apache-2.0   |
| PPHumanSeg                                  | Apache-2.0   |
| Robust Video Matting MobileNetV3            | GPL-3.0-only |
| Selfie Multiclass                           | Apache-2.0   |
| Selfie Segmentation                         | Apache-2.0   |
| Semantic Guided Low-Light Image Enhancement | MIT          |
| TBEFN                                       | BSD-3-Clause |
| TCMonoDepth                                 | MIT          |
| URetinex-Net                                | MIT          |

The sidecar files contain the applicable copyright holders and are the
authoritative record for each copy. Distributions must keep each model together
with its license information.

## Build and runtime components

Depending on the platform and build configuration, BGOBS builds against or
packages components including:

- OBS Studio and its development interfaces;
- ONNX Runtime;
- OpenCV;
- libcurl and a TLS backend;
- libusb for the optional direct USB source;
- Rust and C/C++ runtime libraries;
- Android platform-tools for the optional packaged ADB client on Windows.

These projects are not relicensed by BGOBS. Their exact versions are selected
by `buildspec.json`, `buildspec.props`, `vcpkg.json`, lock files, and release
automation. Binary distributors are responsible for including all notices and
source offers required by the exact components they ship. Android
platform-tools are governed by Google's distribution terms and must not be
assumed to use the BGOBS license.

## Other inherited assets

`demo.gif`, documentation images, OBS resource templates, workflow files, and
other inherited material carry file-level or sidecar SPDX metadata. Consult
those notices before copying an asset separately from the repository.

If a package's contents disagree with this overview, the package's actual
files, SPDX metadata, and corresponding license texts take precedence. Please
report missing or inconsistent notices as a bug.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
