# Changelog

This file records user-visible BGOBS changes. Historical entries before the
BGOBS fork remain available in Git history and their original tags.

## 0.3.19 - 2026-07-21

### Added

- Public project, security, contribution, architecture, release, and
  third-party documentation.
- A standalone specification for the optional CaCam USB protocol.
- Automated coverage for the Rust core, documentation site, and native mask
  processing.

### Changed

- Made English the default language for project documentation while retaining
  the shipped OBS locale translations.
- Clarified that BGOBS is open source and that the separate private CaCam app is
  optional.
- Reused native video textures and improved CPU fallback behavior.
- Moved update checks off latency-sensitive OBS callbacks and made shared
  lifecycle state thread-safe.

### Security

- Added bounds and timeouts to USB, ADB HTTP, and update-metadata handling.
- Required HTTPS and bounded response sizes for release metadata.
- Hardened source shutdown and reconnection against stale worker callbacks.
- Pinned and verified external archives used by the Windows and Linux release
  builds.

## 0.3.18

- Preserve the selected BGOBS style when OBS rebuilds filter properties or
  saves a scene collection.
- Restore the matching preset when an older save says `custom` but contains the
  exact preset values.

## 0.3.17

- Preserve the selected BGOBS style when OBS applies a mask preset; manual
  adjustments still switch the style to Custom.

## 0.3.16

- Fill the OBS source canvas with a centered crop so stream aspect-ratio
  changes no longer add variable internal black bars.

## 0.3.15

- Start CaCam in ADB mode with an explicit frame rate instead of retaining an
  old manual override.
- Keep the CaCam source rectangle stable across stream quality changes.
- Document Low at 12 FPS as the smooth setting observed on the MI8 ADB test
  device.

## 0.3.14

- Synchronize the version embedded in the OBS manifest and loaded module.
- Include the Windows ADB process and path-quoting fix from 0.3.13.

## 0.3.13

- Run CaCam ADB commands on Windows without visible `cmd.exe` windows.
- Quote ADB paths containing spaces or special characters correctly.

## 0.3.12

- Copy the packaged `adb` directory recursively in the PortableApps installer.
- Update Windows instructions for the CaCam source and its ADB mode.

## 0.3.11

- Rename **CaCam USB** to **CaCam** and add direct USB and ADB connection modes.
- Add Rotten, Low, Standard, HD, and UHD source qualities plus a separate BGOBS
  optimization option.
- Package Android platform-tools in the Windows ZIP when available at build
  time.

## 0.3.10

- Ignore Android's ADB interface while selecting the Open Accessory video
  interface.
- Improve the Windows diagnostic when the accessory interface lacks a WinUSB
  driver.

## 0.3.9

- Add a detailed-log message when libusb cannot see an Android Open Accessory
  device.
- Clarify diagnostics for Windows devices exposed only through MTP/WPD.

## 0.3.8

- Show the BGOBS version in the CaCam source properties.
- Add a detailed USB logging option that is disabled by default.

## 0.3.6

- Distinguish an open USB interface from a completed CaCam handshake.
- Warn after five seconds without accessory data and suggest unlocking the
  phone and keeping CaCam in the foreground.

## 0.3.5

- Reduce direct USB latency with unbuffered OBS output and stale-frame drops.
- Compensate slowly for Android/host monotonic-clock drift.
- Improve Android Open Accessory negotiation and reconnection, including Xiaomi
  devices.
- Add an OBS PortableApps installer and package libusb for Windows.

## 0.3.4

- Read direct USB data with a larger reusable libusb buffer.
- Reduce sender data and discard stale USB frames.
- Keep existing HTTP and RTSP behavior separate from the direct USB source.

## 0.3.3

- Use the local OBS video clock for direct USB frames to prevent a frozen image.
- Reuse BGRA buffers and package `libusb-1.0.dll` beside the Windows plugin.

## 0.3.2

- Handle a USB transfer containing both a BGOBS header and frame payload.

## 0.3.1

- Publish the first official Windows x64 BGOBS package for portable OBS.

## 0.3.0

- Add the direct CaCam source over Android Open Accessory and libusb.
- Receive NV21 frames and convert them to OBS video through OpenCV.
- Keep the BGOBS effect filter independent from the optional CaCam source.

## 0.2.0

- Establish the BGOBS name, module identifiers, bundle identifier, data layout,
  and Rust core.

<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
