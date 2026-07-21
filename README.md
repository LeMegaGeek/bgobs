# BGOBS — background tools for OBS Studio

BGOBS (**Beau Gosse OBS**) is a free and open-source OBS Studio plugin for
real-time portrait segmentation, background blur, and low-light enhancement.
It is designed for clean, stable webcam edges and keeps video processing on
your computer.

BGOBS is independent third-party software and is not affiliated with or
endorsed by the OBS Project. AI-assisted work is disclosed in
[GENERATED.md](GENERATED.md).

[Download the latest release](https://github.com/LeMegaGeek/bgobs/releases/latest) ·
[Report a bug](https://github.com/LeMegaGeek/bgobs/issues) ·
[Read the documentation](docs/README.md)

## Features

- Remove or blur a camera background without a green screen.
- Choose Natural, Studio, Crisp, or Performance mask presets.
- Preview the mask and tune threshold, edge softness, cleanup, refinement,
  expansion, feathering, and temporal smoothing.
- Improve poorly lit video with the included enhancement models.
- Use several local ONNX segmentation models on CPU or a supported GPU
  execution provider.
- Add the optional **CaCam** source over direct USB or ADB.
- Preserve existing OBS scenes and filter settings across plugin upgrades.

BGOBS works with ordinary OBS video sources. CaCam is not required for the
background filters or enhancement tools.

## Install

Release assets that complete their platform build, native tests, and package
validation are published on
[GitHub Releases](https://github.com/LeMegaGeek/bgobs/releases). Use only a
package actually attached to the selected release. Depending on the release,
the available artifact types can include:

- **Windows 10 or 11 x64:** extract the ZIP and copy the `bgobs` plugin folder
  into the OBS plugin directory. The ZIP also contains helpers for OBS
  PortableApps.
- **macOS 12 or later:** run the universal `.pkg` installer for Intel and Apple
  silicon Macs.
- **Ubuntu 24.04 x86_64:** install the `.deb` package with APT.

If a platform has no attached artifact, BGOBS makes no binary-support claim for
that platform in that version; use the source and build documentation instead.
Close OBS before replacing plugin files, then restart it. Detailed Windows
PortableApps instructions are in
[docs/WINDOWS-PORTABLEAPPS.md](docs/WINDOWS-PORTABLEAPPS.md).

## Use the background filter

1. Add or select a video source in OBS.
2. Open the source's **Filters** dialog.
3. Add **Make me look good** under **Effect Filters**. The label is translated
   when OBS uses a supported locale.
4. Select a BGOBS style and use **Mask preview** while fine-tuning the edge.
5. Disable the preview when the result looks right.

Soft frontal lighting often improves segmentation more than a heavier model.
If hair or hands flicker, try the Natural preset before changing individual
advanced controls.

## Optional CaCam source

BGOBS includes an interoperable receiver for the CaCam USB protocol and an ADB
source mode. **CaCam is a separate, privately developed Android application; it
is not part of this open-source repository and is not required to use BGOBS.**

For direct USB:

1. Select **USB BGOBS** in CaCam and start streaming.
2. Add the **CaCam** source in OBS and select **USB**.
3. Accept Android's accessory permission prompt if it appears.

For ADB:

1. Enable USB debugging on the Android device and authorize the computer.
2. Add the **CaCam** source in OBS and select **ADB**.
3. Choose a quality preset. BGOBS creates a local ADB port forward and reads
   frames from CaCam on `127.0.0.1`.

The wire format is documented independently in
[docs/CACAM-USB-PROTOCOL.md](docs/CACAM-USB-PROTOCOL.md), so compatible senders
can be implemented without access to CaCam's source code.

## Privacy and networking

Model inference and frame processing happen locally. BGOBS does not upload
camera frames or masks. The optional update check contacts GitHub only when it
is enabled; it is disabled in the default configuration. ADB mode communicates
with a locally forwarded loopback port.

## Build and test

The native plugin is C++ with a small Rust library for testable mask operations.
It uses OBS, ONNX Runtime, OpenCV, and libcurl; the optional direct USB source
loads libusb at runtime.

```sh
cargo fmt --all --check
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Platform-specific dependencies and build notes are indexed in
[docs/README.md](docs/README.md). See [CONTRIBUTING.md](CONTRIBUTING.md) before
opening a pull request.

## Project and license

BGOBS is maintained by LeMegaGeek and is derived from the OBS Background
Removal project originally created by Roy Shilkrot and later maintained with
Kaito Udagawa. See [AUTHORS.md](AUTHORS.md) and [NOTICE](NOTICE) for attribution.

BGOBS is distributed under **GPL-3.0-or-later**. Individual models and bundled
third-party components retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), the adjacent `.license` files,
and the texts in `LICENSES/`.

<!--
SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
