# Local Linux OBS Build

This is the private local build workflow used for testing `obs-backgroundremoval` against the OBS installation on this
machine. It keeps the generated build tree under `build/local-obs` and the local dependency sysroot under `.deps`.

## Configure

Run CMake from the repository root:

```bash
SYSROOT="$PWD/.deps/sysroot"
PKG_CONFIG_PATH="$PWD/.deps/pkgconfig:$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$PWD/.deps/onnxruntime/lib/pkgconfig" \
PKG_CONFIG_SYSROOT_DIR="$SYSROOT" \
cmake -S . -B build/local-obs -G Ninja \
  -DOS_LINUX=Ubuntu \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DCMAKE_PREFIX_PATH="$SYSROOT/usr;$PWD/.deps/onnxruntime" \
  -DCURL_INCLUDE_DIR="$SYSROOT/usr/include/x86_64-linux-gnu" \
  -DCURL_LIBRARY="$SYSROOT/usr/lib/x86_64-linux-gnu/libcurl.so" \
  -DCMAKE_C_FLAGS="-isystem $SYSROOT/usr/include" \
  -DCMAKE_CXX_FLAGS="-isystem $SYSROOT/usr/include" \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_INSTALL_RPATH='$ORIGIN'
```

## Build And Test

```bash
cmake --build build/local-obs --parallel
ctest --test-dir build/local-obs --output-on-failure
```

For formatting and metadata checks:

```bash
.venv/bin/clang-format --dry-run --Werror \
  src/background-filter.cpp \
  src/background/mask-post-processing.cpp \
  src/background/mask-post-processing.hpp \
  tests/background/mask-post-processing-test.cpp
.venv/bin/gersemi --check CMakeLists.txt
.venv/bin/reuse lint
git diff --check
```

## Install For Manual OBS Testing

Use this only after the build and tests pass. The command installs the plugin into the system OBS plugin directory and
copies the ONNX Runtime shared libraries needed by the local build.

```bash
sudo cmake --install build/local-obs --prefix /usr
sudo install -m 0755 .deps/onnxruntime/lib/libonnxruntime.so* /usr/lib/x86_64-linux-gnu/obs-plugins/
sudo install -m 0755 .deps/onnxruntime/lib/libonnxruntime_providers*.so /usr/lib/x86_64-linux-gnu/obs-plugins/
```

After installation, start OBS from a terminal and confirm that the Background Removal filter loads. The filter logs the
effective mask settings when it is created.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
>
> SPDX-License-Identifier: GPL-3.0-or-later  
