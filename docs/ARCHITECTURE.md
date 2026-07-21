# BGOBS architecture

BGOBS is a native OBS module with C and C++ integration code plus a small Rust
library for pure, testable mask operations. Models and shader effects are
packaged as plugin data.

## Main components

| Area                  | Main paths                                                     | Responsibility                                                                                                         |
| --------------------- | -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| OBS module            | `src/plugin-main.c`, `src/*-info.c`                            | Register sources and filters, expose localized names, and connect OBS callbacks.                                       |
| Background filter     | `src/background-filter.cpp`, `src/background/`                 | Run segmentation, post-process masks, and composite or blur the background.                                            |
| Rust core             | `crates/bgobs-core/`                                           | Implement deterministic mask operations behind a C-compatible FFI boundary.                                            |
| Enhancement filter    | `src/enhance-filter.cpp`                                       | Apply the selected low-light enhancement model.                                                                        |
| Model adapters        | `src/models/`, `src/ort-utils/`                                | Configure ONNX Runtime sessions and translate model inputs and outputs.                                                |
| Optional phone source | `src/cacam-usb-source.cpp`                                     | Receive frames from a compatible sender over Android Open Accessory or launch/read CaCam through local ADB forwarding. |
| Update check          | `src/update-checker/`                                          | Query release metadata over HTTPS when the optional update check is enabled.                                           |
| Plugin data           | `data/`                                                        | Store locales, effects, configuration defaults, and optimized models.                                                  |
| Build and packaging   | `CMakeLists.txt`, `cmake/`, `packaging/`, `.github/workflows/` | Build platform modules and assemble release artifacts.                                                                 |

## Frame flow

For an ordinary OBS source, OBS supplies a frame to the background or
enhancement filter. The C++ layer prepares model input, invokes ONNX Runtime,
normalizes model output, and applies the selected effect. Background-mask
post-processing is described in [MASK-POST-PROCESSING.md](MASK-POST-PROCESSING.md).

The optional direct USB source follows a separate path:

```text
Android accessory bulk stream
  → bounded message parser
  → NV21 frame validation
  → OpenCV color conversion
  → OBS asynchronous video frame
  → optional BGOBS effect filter
```

ADB mode starts or selects the Android sender, establishes an `adb forward` to
a loopback port, retrieves local health and snapshot endpoints, decodes the
image, and produces the same OBS source output. Details of the direct USB wire
format are in [CACAM-USB-PROTOCOL.md](CACAM-USB-PROTOCOL.md).

## State and concurrency

OBS can create, update, deactivate, and destroy sources from different callback
contexts. Worker-owned transport and update-check state must not outlive the
OBS object that created it. Cross-thread flags use synchronization primitives,
and callbacks copy any shared value they need rather than retaining an
unprotected reference.

Graphics resources belong to the OBS graphics context. Texture allocation and
destruction must follow OBS's graphics rules, while CPU-side model and parser
state stays outside the render critical section where practical.

## Trust boundaries

- Model and image dimensions, message lengths, HTTP response sizes, and JSON
  fields are untrusted until validated.
- USB and ADB are local transports but not authentication mechanisms.
- Update metadata is remote input and must use HTTPS, bounded responses, and
  timeouts; it must never block an OBS rendering callback.
- Logs can expose paths, addresses, and device identifiers and must be
  sanitized before publication.
- CaCam is a separate private product. BGOBS depends only on its documented
  interoperability surfaces, not on access to its implementation.

## Testing boundaries

Pure Rust and mask-processing behavior should be covered without launching OBS.
Native CTest targets cover deterministic C++ code. Plugin lifecycle, rendering,
USB enumeration, ADB authorization, GPU providers, and platform packaging still
require integration tests on the relevant host and hardware.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
