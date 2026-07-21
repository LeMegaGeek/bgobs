# Windows PortableApps Install Layout

This document describes where a Windows x64 build of `bgobs` should be placed when OBS is installed through
PortableApps.

## Required Windows Binary

Windows OBS requires a Windows plugin binary:

```text
bgobs.dll
```

A Linux build such as `bgobs.so` cannot be used on Windows. The Windows DLL and its ONNX Runtime DLLs belong in the
plugin bundle's `bin/64bit` directory.

Usually required next to `bgobs.dll`:

```text
libusb-1.0.dll
onnxruntime.dll
onnxruntime_providers_shared.dll
```

Additional provider DLLs may be needed if the Windows build enables GPU execution providers.
For the CaCam ADB mode, the package should also include `adb\adb.exe`,
`adb\AdbWinApi.dll`, and `adb\AdbWinUsbApi.dll` next to `bgobs.dll`.

## PortableApps Location

Close OBS first, extract the ZIP, and run `install-portable.bat`. When prompted,
enter the directory that contains `OBSPortable.exe` (or the legacy
`OBS-StudioPortable.exe` launcher). The script copies the DLLs and plugin data to
both PortableApps locations automatically.

For a manual installation, run `remove-old-installation.bat` first if an older
version was installed, then use the layout below.

PortableApps OBS in this tree loads plugins from the embedded OBS layout. Copy the DLLs from the ZIP into:

```text
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\
```

The plugin data must be copied into both data locations used by this PortableApps layout:

```text
<OBS-StudioPortable>\App\obs-studio\data\obs-plugins\bgobs\
<OBS-StudioPortable>\Data\obs-plugins\bgobs\
```

The final layout should include:

```text
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\bgobs.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\libusb-1.0.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\onnxruntime.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\onnxruntime_providers_shared.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\adb\adb.exe
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\adb\AdbWinApi.dll
<OBS-StudioPortable>\App\obs-studio\obs-plugins\64bit\adb\AdbWinUsbApi.dll
<OBS-StudioPortable>\App\obs-studio\data\obs-plugins\bgobs\models\mediapipe.with_runtime_opt.ort
<OBS-StudioPortable>\Data\obs-plugins\bgobs\models\mediapipe.with_runtime_opt.ort
```

Restart OBS after copying the files. The **CaCam** source and the BGOBS and
Low-Light Enhancement filters should be available.

For the first USB connection, unlock the phone, accept Android's accessory
prompt if it appears, and keep CaCam in the foreground until OBS displays the
first frame.

For ADB, unlock the phone and accept Android's USB debugging prompt. The source
creates an `adb forward` from `127.0.0.1:18080` to CaCam's phone port and can
request Standard, HD, or UHD from the Android app.

## Standard Windows OBS

For a normal Windows OBS install, copy the same plugin folder into:

```text
C:\ProgramData\obs-studio\plugins\
```

Do not install OBS plugins under `C:\Program Files\obs-studio`. OBS documents
`ProgramData` as the supported system-wide Windows plugin location.

## Troubleshooting

- If the filters are not listed, confirm that OBS is 64-bit and that `bgobs.dll` is a Windows x64 DLL.
- If the **CaCam** source is not listed, confirm that `libusb-1.0.dll` is beside `bgobs.dll`.
- If the OBS log says that the Android device is not accessible, install the
  WinUSB driver for the phone's active USB interface, reconnect the phone, and
  restart OBS.
- If the accessory opens but sends no data, unlock the phone and leave CaCam in
  the foreground until the first frame appears.
- If ADB mode reports no authorized phone, unlock the phone and accept the USB
  debugging prompt, or set the device serial in the source properties.
- If ADB mode cannot find `adb`, confirm that `adb\adb.exe` is next to
  `bgobs.dll` or set the ADB path in the source properties.
- If OBS reports missing ONNX Runtime libraries, put the ONNX Runtime DLLs beside `bgobs.dll`.
- If OBS reports missing model files, confirm that the bundle contains `bgobs\data\models`.
- If OBS reports a different BGOBS version than the package, inspect the latest
  log under `<OBS-StudioPortable>\Data\config\obs-studio\logs\` and check which
  plugin path OBS loaded. Remove a duplicate only after confirming that it is an
  obsolete copy and not another active plugin.
- Restart OBS after replacing plugin files.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
>
> SPDX-License-Identifier: GPL-3.0-or-later
