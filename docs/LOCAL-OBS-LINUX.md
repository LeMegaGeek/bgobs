# Local Linux OBS build

This guide covers a developer build against an OBS installation and dependency
prefix already available on the machine. Package names and paths vary by
distribution; see the Fedora and openSUSE notes in this directory for examples.

## Configure

At minimum, install a C++ toolchain, CMake, OBS development headers, ONNX
Runtime, OpenCV, libcurl, Rust, and the libraries required by the selected OBS
build. Then configure from the repository root:

```sh
cmake -S . -B build/local-obs -G Ninja \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

If dependencies live outside system paths, add their prefixes with
`CMAKE_PREFIX_PATH` or use the appropriate CMake cache options documented by
the configure output. Do not copy machine-specific absolute paths into a
committed preset.

## Build and test

```sh
cmake --build build/local-obs --parallel
ctest --test-dir build/local-obs --output-on-failure
```

Also run the Rust and repository checks from [CONTRIBUTING.md](../CONTRIBUTING.md).
For a diagnostic build, configure a separate directory with
`BGOBS_ENABLE_SANITIZERS=ON`. ThreadSanitizer uses another separate directory
with `BGOBS_ENABLE_THREAD_SANITIZER=ON`.

## Install for manual testing

Prefer a per-user OBS plugin directory so a development build cannot overwrite
a packaged system plugin. A typical Linux layout is:

```text
~/.config/obs-studio/plugins/bgobs/
├── bin/64bit/bgobs.so
└── data/
```

The exact output paths depend on the selected generator and install prefix.
Use `cmake --install` with a staging prefix first, inspect the resulting tree,
then copy that tree into the user plugin directory. Keep the required ONNX
Runtime libraries and model files with the plugin package.

Start OBS from a terminal, confirm that the log identifies the expected BGOBS
version and path, then exercise the changed filter or source. Remove the local
copy after testing if a system package should take precedence again.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
