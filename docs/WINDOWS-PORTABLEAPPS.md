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
<OBS-StudioPortable>\App\obs-studio\data\obs-plugins\bgobs\models\mediapipe.with_runtime_opt.ort
<OBS-StudioPortable>\Data\obs-plugins\bgobs\models\mediapipe.with_runtime_opt.ort
```

Restart OBS after copying the files. The **CaCam USB** source and the BGOBS and
Low-Light Enhancement filters should be available.

## Standard Windows OBS

For a normal Windows OBS install, copy the same plugin folder into:

```text
C:\ProgramData\obs-studio\plugins\
```

Do not install OBS plugins under `C:\Program Files\obs-studio`. The upstream project documents `ProgramData` as the
supported Windows plugin location.

## Troubleshooting

- If the filters are not listed, confirm that OBS is 64-bit and that `bgobs.dll` is a Windows x64 DLL.
- If the **CaCam USB** source is not listed, confirm that `libusb-1.0.dll` is beside `bgobs.dll`.
- If OBS reports missing ONNX Runtime libraries, put the ONNX Runtime DLLs beside `bgobs.dll`.
- If OBS reports missing model files, confirm that the bundle contains `bgobs\data\models`.
- If OBS still reports version `1.3.x`, it is loading an older DLL from another folder. Open the latest log under
  `<OBS-StudioPortable>\Data\config\obs-studio\logs\`, search for `obs-backgroundremoval`, delete the old path shown
  there, and then copy BGOBS again.
- If a scene still shows `Suppression de l'arrière-plan`, rename that existing filter instance in OBS. Filter instance
  names are saved in scene collections and do not automatically follow the plugin locale.
- Restart OBS after replacing plugin files.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
>
> SPDX-License-Identifier: GPL-3.0-or-later  
