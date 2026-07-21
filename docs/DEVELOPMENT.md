# Development workflow

BGOBS is developed publicly at <https://github.com/LeMegaGeek/bgobs>. The
repository history retains attribution to the project from which BGOBS was
derived; contributors should not need a second remote for normal development.

## Get the source

```sh
git clone https://github.com/LeMegaGeek/bgobs.git
cd bgobs
git switch main
```

Create a focused topic branch and keep it current with `origin/main`. Never
force-push another contributor's branch or publish credentials, recordings, OBS
profiles, device serials, or private CaCam source code.

## Validate changes

Run the checks relevant to the change:

```sh
cargo fmt --all --check
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
git diff --check
```

For native code, use a build with tests enabled:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run `reuse lint` when the REUSE tool is available. Website changes also require
the npm checks listed in [CONTRIBUTING.md](../CONTRIBUTING.md).

Hardware-dependent changes need a manual OBS test. Record the operating system,
architecture, OBS version, BGOBS revision, camera or GPU model where relevant,
and whether the test used direct USB or ADB. Sanitize logs before attaching
them to an issue or pull request.

## Submit a change

Follow [CONTRIBUTING.md](../CONTRIBUTING.md), open a pull request against
`main`, and describe both automated and manual coverage. A pull request must not
change release metadata unless it is specifically preparing a release.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
