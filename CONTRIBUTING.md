# Contributing to BGOBS

Thank you for helping improve BGOBS. Bug reports, documentation fixes,
translations, tests, and code changes are welcome.

## Before starting

- Search [open issues](https://github.com/LeMegaGeek/bgobs/issues) before filing
  a duplicate.
- For a large change, open an issue first so its design and platform impact can
  be discussed.
- Keep changes focused. Avoid mixing formatting-only rewrites with functional
  changes.
- Do not include secrets, proprietary SDK files, private CaCam source code, or
  recordings that identify people without their consent.

## Development workflow

1. Fork the repository and create a branch from `main`.
2. Make the smallest complete change that solves the problem.
3. Add or update tests and documentation where appropriate.
4. Run the relevant checks below.
5. Open a pull request explaining the problem, the solution, and how it was
   tested.

## Licensing and attribution

Contributions are accepted under **GPL-3.0-or-later**, the project's license.
Use concise REUSE-compatible headers on new source files:

```text
SPDX-FileCopyrightText: YEAR NAME OR ORGANIZATION
SPDX-License-Identifier: GPL-3.0-or-later
```

Use the file's existing license when modifying third-party or differently
licensed material. Do not replace existing copyright notices with your own.
Binary assets need a neighboring `.license` file unless `REUSE.toml` already
covers them.

A DCO sign-off (`git commit -s`) and a cryptographic commit signature are
welcome, but neither is required unless a repository rule displayed by GitHub
explicitly says otherwise. A pull request still needs a clear author identity
and must contain only material the contributor is allowed to submit.

## AI-assisted contributions

AI tools may be used as assistants. Contributors remain responsible for every
submitted line and must:

- understand and review the result;
- verify behavior, licensing, security, and privacy implications;
- remove fabricated claims, dependencies, citations, and APIs;
- disclose material AI assistance in the pull request when it helps reviewers
  assess provenance or risk.

Do not submit unreviewed generated output. See [GENERATED.md](GENERATED.md) for
the project's own disclosure.

## Checks

Run the portable checks from the repository root:

```sh
cargo fmt --all --check
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
git diff --check
```

For documentation-site changes:

```sh
cd pages
npm ci
npm audit
npm test
npx prettier --check .
npm run build
```

For native changes, configure a build with `BUILD_TESTING=ON`, build the plugin,
and run CTest. On Clang or GCC, also use a separate sanitizer build when the
changed code can be covered:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`BGOBS_ENABLE_SANITIZERS=ON` enables AddressSanitizer and
UndefinedBehaviorSanitizer. `BGOBS_ENABLE_THREAD_SANITIZER=ON` enables
ThreadSanitizer in a separate build; these modes are intentionally mutually
exclusive.

Hardware-specific changes should include the tested OBS version, operating
system, architecture, GPU or USB device where relevant, and an OBS log with
sensitive paths or identifiers removed.

## Reporting security issues

Do not publish exploit details in a normal issue. Follow
[SECURITY.md](SECURITY.md) instead.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
