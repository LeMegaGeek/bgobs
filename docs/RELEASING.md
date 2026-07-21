# Releasing BGOBS

This checklist describes the intended release gate. A Git tag or a locally
built archive is not sufficient evidence that every platform package works.

## 1. Prepare one version

Choose a semantic version and update every version-bearing project file in the
same change, including `VERSION`, `buildspec.json`, `data/manifest.json`, and
any platform metadata that embeds the plugin version. Add the release to
`CHANGELOG.md` and create matching notes under `release-notes/`.

Search the repository for both the old and new versions. Historical release
notes should retain the version they document.

## 2. Validate source and documentation

From a clean checkout of the exact release commit, run:

```sh
cargo fmt --all --check
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
reuse lint
git diff --check
```

Run the website checks in `pages/`, configure the native tests, build the
plugin, and run CTest. Use the sanitizer configurations for parser, lifecycle,
threading, or image-buffer changes when the platform supports them.

## 3. Exercise integrations

Test a normal camera source in OBS and record the OBS version, operating system,
architecture, and BGOBS commit. Verify:

- creation, settings changes, scene save/reload, deactivation, and destruction;
- at least one background model and mask preset;
- enhancement behavior when changed by the release;
- clean shutdown without new sanitizer or OBS-log errors.

When CaCam compatibility changed, test only the transports included in the
release claim. Record the CaCam version and device separately. A successful
Android test does not validate Windows, macOS, or Linux packaging.

## 4. Tag and build

Create a `vX.Y.Z` tag only after the release commit is reviewed and the portable
checks pass. Push the tag, then monitor every enabled platform workflow. Build
artifacts must come from the tagged commit, not from an uncommitted local tree.

Do not claim a platform in the README or release announcement merely because a
workflow file exists. A platform is published for that version only after its
build, packaging checks, and appropriate manual smoke test pass.

## 5. Inspect the draft release

For every artifact that will be published:

- verify its filename and embedded version;
- inspect the archive or package layout;
- confirm the module, models, locales, required runtime libraries, license, and
  third-party notices are present;
- scan for source trees, credentials, developer paths, crash dumps, and other
  accidental files;
- generate and verify SHA-256 checksums;
- install it on a clean or representative OBS setup and perform a smoke test.

Keep debug-symbol packages separate and label them clearly. Publish only the
artifacts that passed. Release notes must distinguish verified binaries from
source-only or pending platforms.

## 6. Publish and verify

Publish the GitHub release without moving or recreating the tag. Check that the
public release page, download links, checksum file, license links, and website
all refer to the same version. Confirm that a fresh user can identify the right
package without reading the workflow logs.

If a serious problem appears, mark or remove the affected binary from the
release page, document the limitation, and issue a new version. Do not silently
replace a tagged source revision.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
