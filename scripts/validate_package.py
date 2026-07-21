#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
#
# SPDX-License-Identifier: Apache-2.0

"""Validate the runtime contents of a staged BGOBS package."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def require_file(path: Path, description: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"Missing or empty {description}: {path}")
    return path


def find_one(root: Path, name: str, required_parent: str) -> Path:
    matches = [path for path in root.rglob(name) if required_parent in path.parts]
    if len(matches) != 1:
        rendered = ", ".join(str(path) for path in matches) or "none"
        raise ValueError(f"Expected exactly one {name} below {required_parent}; found: {rendered}")
    return require_file(matches[0], name)


def load_json(path: Path, description: str) -> object:
    require_file(path, description)
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid {description}: {path}: {error}") from error


def validate_data(data_dir: Path, version: str, signing_status: str) -> None:
    manifest_path = require_file(data_dir / "manifest.json", "plugin manifest")
    manifest = load_json(manifest_path, "plugin manifest")
    expected_manifest = {
        "id": "net.lemegageek.bgobs",
        "name": "bgobs",
        "version": version,
    }
    if not isinstance(manifest, dict):
        raise ValueError(f"Plugin manifest must be an object: {manifest_path}")
    for key, expected in expected_manifest.items():
        if manifest.get(key) != expected:
            raise ValueError(
                f"Plugin manifest {key!r} must be {expected!r}, got {manifest.get(key)!r}"
            )

    config_path = require_file(data_dir / "config.json", "default configuration")
    config = load_json(config_path, "default configuration")
    if not isinstance(config, dict) or not isinstance(config.get("check_for_updates"), bool):
        raise ValueError(f"check_for_updates must be a JSON boolean in {config_path}")

    license_path = require_file(data_dir / "LICENSE", "GPL license")
    if "GNU GENERAL PUBLIC LICENSE" not in license_path.read_text(encoding="utf-8"):
        raise ValueError(f"Unexpected license contents: {license_path}")

    require_file(data_dir / "NOTICE", "project notice")
    require_file(data_dir / "THIRD_PARTY_NOTICES.md", "third-party notices")
    licenses_dir = data_dir / "LICENSES"
    for license_name in (
        "Apache-2.0.txt",
        "BSD-3-Clause.txt",
        "CC0-1.0.txt",
        "GPL-3.0-only.txt",
        "GPL-3.0-or-later.txt",
        "MIT.txt",
    ):
        require_file(licenses_dir / license_name, "third-party license text")

    status_path = require_file(data_dir / "BGOBS-SIGNING-STATUS.txt", "signing status")
    status_text = status_path.read_text(encoding="utf-8")
    if f"signing status: {signing_status}" not in status_text:
        raise ValueError(f"Signing status is not {signing_status!r}: {status_path}")
    if f"Version: {version}" not in status_text:
        raise ValueError(f"Signing status version does not match {version}: {status_path}")

    for effect in (
        "blend_images.effect",
        "kawase_blur.effect",
        "mask_alpha_filter.effect",
    ):
        require_file(data_dir / "effects" / effect, "OBS effect")
    require_file(data_dir / "locale" / "en-US.ini", "English locale")

    model_dir = data_dir / "models"
    models = sorted(model_dir.glob("*.ort"))
    if not models:
        raise ValueError(f"No ONNX Runtime model found in {model_dir}")
    for model in models:
        require_file(model.with_name(f"{model.name}.license"), "model license metadata")


def validate(root: Path, platform: str, version: str, signing_status: str) -> None:
    if not root.is_dir():
        raise ValueError(f"Package root does not exist: {root}")

    if platform == "windows":
        plugin_root = root / "bgobs"
        binary_dir = plugin_root / "bin" / "64bit"
        require_file(binary_dir / "bgobs.dll", "Windows plugin binary")
        require_file(binary_dir / "onnxruntime.dll", "ONNX Runtime DLL")
        require_file(binary_dir / "libusb-1.0.dll", "libusb DLL")
        adb_dir = binary_dir / "adb"
        for filename in ("adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll"):
            require_file(adb_dir / filename, "Android platform-tools runtime")
        require_file(adb_dir / "NOTICE.txt", "Android platform-tools notices")
        require_file(root / "THIRD-PARTY-NOTICES" / "libusb.txt", "libusb license")
        require_file(plugin_root / "data" / "onnxruntime-copyright", "ONNX Runtime license")
        require_file(
            plugin_root / "data" / "onnxruntime-third-party-notices.txt",
            "ONNX Runtime third-party notices",
        )
        validate_data(plugin_root / "data", version, signing_status)
    elif platform == "macos":
        bundle = (
            root
            / "Library"
            / "Application Support"
            / "obs-studio"
            / "plugins"
            / "bgobs.plugin"
            / "Contents"
        )
        require_file(bundle / "MacOS" / "bgobs", "macOS plugin binary")
        libusb_runtimes = [
            path
            for path in (bundle / "Frameworks").glob("libusb-1.0*.dylib")
            if path.is_file() and path.stat().st_size > 0
        ]
        if not libusb_runtimes:
            raise ValueError("The macOS package does not contain its universal libusb runtime")
        require_file(bundle / "Resources" / "libusb-copyright", "libusb license")
        require_file(bundle / "Resources" / "onnxruntime-copyright", "ONNX Runtime license")
        require_file(
            bundle / "Resources" / "onnxruntime-third-party-notices.txt",
            "ONNX Runtime third-party notices",
        )
        validate_data(bundle / "Resources", version, signing_status)
    elif platform == "linux":
        find_one(root, "bgobs.so", "obs-plugins")
        onnx_runtimes = [
            path
            for path in root.rglob("libonnxruntime.so*")
            if "bgobs-runtime" in path.parts and path.is_file() and path.stat().st_size > 0
        ]
        if not onnx_runtimes:
            raise ValueError("The Linux package does not contain its private ONNX Runtime library")
        validate_data(root / "usr" / "share" / "obs" / "obs-plugins" / "bgobs", version, signing_status)
        require_file(root / "usr" / "share" / "doc" / "bgobs" / "copyright", "Debian copyright file")
        require_file(
            root / "usr" / "share" / "doc" / "bgobs" / "onnxruntime-copyright",
            "ONNX Runtime license",
        )
        require_file(
            root / "usr" / "share" / "doc" / "bgobs" / "onnxruntime-third-party-notices.txt",
            "ONNX Runtime third-party notices",
        )
    else:
        raise ValueError(f"Unsupported platform: {platform}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Root of the staged package")
    parser.add_argument("--platform", choices=("linux", "macos", "windows"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--signing-status", choices=("signed", "unsigned"), required=True)
    args = parser.parse_args()

    try:
        validate(args.root.resolve(), args.platform, args.version, args.signing_status)
    except ValueError as error:
        print(f"Package validation failed: {error}", file=sys.stderr)
        return 1

    print(f"Validated {args.platform} package contents for BGOBS {args.version} ({args.signing_status}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
