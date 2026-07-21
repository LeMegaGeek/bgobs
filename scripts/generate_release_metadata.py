#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
#
# SPDX-License-Identifier: Apache-2.0

"""Validate the release asset set and generate checksums plus an SPDX SBOM."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys


def digest(path: Path, algorithm: str) -> str:
    value = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def expected_names(version: str) -> tuple[str, ...]:
    return (
        f"bgobs-{version}-linux-x86_64-debug-symbols.ddeb",
        f"bgobs-{version}-linux-x86_64-unsigned.deb",
        f"bgobs-{version}-macos-universal-debug-symbols.tar.zst",
        f"bgobs-{version}-windows-x64-debug-symbols.zip",
        f"bgobs-{version}-windows-x64-unsigned.zip",
    )


def macos_package_names(version: str) -> tuple[str, str]:
    return (
        f"bgobs-{version}-macos-universal.pkg",
        f"bgobs-{version}-macos-universal-unsigned.pkg",
    )


def validate_assets(directory: Path, version: str) -> list[Path]:
    semver = r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?(?:\+[0-9A-Za-z][0-9A-Za-z.-]*)?"
    if not re.fullmatch(semver, version):
        raise ValueError(f"Invalid release version: {version}")

    expected = set(expected_names(version))
    macos_candidates = set(macos_package_names(version))
    actual = {path.name for path in directory.iterdir() if path.is_file()}
    missing = sorted(expected - actual)
    selected_macos = actual & macos_candidates
    unexpected = sorted(actual - expected - macos_candidates)
    if missing or unexpected or len(selected_macos) != 1:
        raise ValueError(
            "Release assets do not match. "
            f"Missing={missing}; unexpected={unexpected}; "
            f"macOS packages={sorted(selected_macos)} (expected exactly one signed or unsigned package)"
        )

    assets = [directory / name for name in sorted(expected | selected_macos)]
    empty = [path.name for path in assets if path.stat().st_size == 0]
    if empty:
        raise ValueError(f"Release assets are empty: {empty}")
    return assets


def write_checksums(directory: Path, version: str, assets: list[Path]) -> Path:
    output = directory / f"SHA256SUMS-{version}.txt"
    content = "".join(f"{digest(path, 'sha256')}  {path.name}\n" for path in assets)
    output.write_text(content, encoding="utf-8", newline="\n")
    return output


def write_sbom(directory: Path, version: str, assets: list[Path]) -> Path:
    file_entries = []
    relationships = [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": "SPDXRef-Package-BGOBS",
        }
    ]
    sha1_values = []
    for index, path in enumerate(assets, start=1):
        sha1 = digest(path, "sha1")
        sha256 = digest(path, "sha256")
        sha1_values.append(sha1)
        spdx_id = f"SPDXRef-File-{index}"
        file_entries.append(
            {
                "SPDXID": spdx_id,
                "fileName": f"./{path.name}",
                "checksums": [
                    {"algorithm": "SHA1", "checksumValue": sha1},
                    {"algorithm": "SHA256", "checksumValue": sha256},
                ],
                "licenseConcluded": "NOASSERTION",
                "licenseInfoInFiles": ["NOASSERTION"],
                "copyrightText": "NOASSERTION",
            }
        )
        relationships.append(
            {
                "spdxElementId": "SPDXRef-Package-BGOBS",
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": spdx_id,
            }
        )

    verification_code = hashlib.sha1("".join(sorted(sha1_values)).encode("ascii")).hexdigest()
    namespace_hash = hashlib.sha256("".join(sorted(sha1_values)).encode("ascii")).hexdigest()[:16]
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"BGOBS-{version}-release-assets",
        "documentNamespace": (
            f"https://github.com/LeMegaGeek/bgobs/releases/download/v{version}/"
            f"BGOBS-{version}-{namespace_hash}"
        ),
        "creationInfo": {
            "created": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "creators": ["Tool: BGOBS-generate-release-metadata"],
        },
        "packages": [
            {
                "name": "BGOBS release artifacts",
                "SPDXID": "SPDXRef-Package-BGOBS",
                "versionInfo": version,
                "downloadLocation": (
                    f"https://github.com/LeMegaGeek/bgobs/releases/tag/v{version}"
                ),
                "filesAnalyzed": True,
                "packageVerificationCode": {
                    "packageVerificationCodeValue": verification_code,
                },
                "licenseConcluded": "GPL-3.0-or-later",
                "licenseDeclared": "GPL-3.0-or-later",
                "copyrightText": "NOASSERTION",
            }
        ],
        "files": file_entries,
        "relationships": relationships,
    }

    output = directory / f"BGOBS-{version}.spdx.json"
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    try:
        directory = args.artifact_dir.resolve(strict=True)
        assets = validate_assets(directory, args.version)
        checksum_path = write_checksums(directory, args.version, assets)
        sbom_path = write_sbom(directory, args.version, assets)
    except (OSError, ValueError) as error:
        print(f"Release metadata generation failed: {error}", file=sys.stderr)
        return 1

    print(f"Validated {len(assets)} assets and wrote {checksum_path.name} and {sbom_path.name}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
