# Security policy

## Supported versions

Security fixes are made on the current development branch and released in the
latest BGOBS version. Older releases should be considered unsupported unless a
release announcement explicitly says otherwise.

## Report a vulnerability privately

Do not open a public issue for a vulnerability that could expose users before a
fix is available. Email `d.github@chey.net` with the subject `BGOBS security`,
or use GitHub's private vulnerability-reporting interface if it is enabled for
the repository.

Include:

- the affected BGOBS version and platform;
- a minimal reproduction or proof of concept;
- the expected impact and required attacker access;
- relevant logs with secrets and personal data removed;
- whether the issue affects the background filters, update checker, package
  installer, ADB mode, or direct USB parser.

You should receive an acknowledgement when the report is read. Response and
release timing depend on severity, reproducibility, maintainer availability,
and coordination with affected dependencies. Please allow a reasonable period
for investigation before public disclosure.

## Scope and safe research

Good-faith testing of your own BGOBS installation and devices is welcome. Do
not access other people's systems or data, disrupt public services, distribute
malware, or publish credentials or identifiable camera footage. The separate
CaCam Android application is not part of this repository; reports that affect
only CaCam should be identified clearly so they can be routed correctly.

For ordinary crashes, incorrect output, and feature requests, use the public
[issue tracker](https://github.com/LeMegaGeek/bgobs/issues).

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
