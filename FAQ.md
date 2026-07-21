# BGOBS frequently asked questions

## What is BGOBS?

BGOBS is an open-source OBS Studio plugin for portrait segmentation,
background removal or blur, and low-light enhancement. It runs model inference
locally on the computer hosting OBS.

## Do I need CaCam?

No. BGOBS filters work with normal cameras, capture cards, media sources, and
other OBS video sources. CaCam is a separate, privately developed Android app
that can provide an optional phone-camera source over direct USB or ADB.

## How do I add the background filter?

Right-click a video source, open **Filters**, and add **Make me look good** under
**Effect Filters**. The displayed name can differ when OBS uses another locale.

## Why is the result transparent?

Background removal outputs transparency so sources lower in the OBS scene can
show through. Add an image, color, browser, or video source below the camera.
If the subject is also transparent, enable **Mask preview** and adjust the model,
threshold, and edge controls.

## Which preset should I use?

Start with **Natural**. Use **Studio** for a controlled, well-lit setup,
**Crisp** for a firmer edge, or **Performance** when inference cost matters more
than fine detail. Soft frontal lighting has a large effect on quality.

## Is video uploaded anywhere?

No. Camera frames, masks, and model inference remain local. The optional update
check contacts GitHub only when enabled, and the default configuration disables
it. ADB source mode uses a loopback port created by `adb forward`.

## Where do I download BGOBS?

Use the assets attached to the
[latest GitHub release](https://github.com/LeMegaGeek/bgobs/releases/latest).
Only platforms actually listed on that release have a published binary for that
version. Source builds are documented in [docs/README.md](docs/README.md).

## Why does OBS not list the plugin?

Open **Help → Log Files → View Current Log** and search for `bgobs`. Common
causes are an incorrect plugin directory, a package for the wrong architecture,
missing ONNX Runtime libraries, or missing model files. Restart OBS after
replacing plugin binaries.

## Why does the CaCam USB source not connect?

- Unlock the Android device and keep CaCam in the foreground for the first
  frame.
- Accept Android's accessory permission prompt.
- On Windows, make sure the active Android accessory interface uses a WinUSB
  driver and that `libusb-1.0.dll` is next to `bgobs.dll`.
- Enable detailed USB logs only while diagnosing; logs can contain device
  identifiers.

Direct USB uses Android Open Accessory, not USB tethering or ADB. See the
[protocol specification](docs/CACAM-USB-PROTOCOL.md) for interoperability
details.

## Why does ADB mode not connect?

- Confirm `adb devices` lists exactly one authorized device, or set its serial
  in the source properties.
- Accept the USB-debugging prompt on the device.
- Confirm the packaged `adb` executable is present or select an explicit ADB
  path.
- Check that the configured local port is not already in use.

## How do I report a bug?

Follow [docs/BUG-REPORTING.md](docs/BUG-REPORTING.md). Include an OBS log,
reproduction steps, operating system, architecture, OBS version, BGOBS version,
and relevant hardware. Remove stream keys, usernames, device serials, and other
sensitive data first.

<!--
SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
