# Windows PortableApps Install Layout

This document describes where a Windows x64 build of `obs-backgroundremoval` should be placed when OBS is installed
through PortableApps.

## Required Windows Binary

Windows OBS requires a Windows plugin binary:

```text
obs-backgroundremoval.dll
```

A Linux build such as `obs-backgroundremoval.so` cannot be used on Windows. The Windows DLL and its ONNX Runtime DLLs
belong in the plugin bundle's `bin/64bit` directory.

Usually required next to `obs-backgroundremoval.dll`:

```text
onnxruntime.dll
onnxruntime_providers_shared.dll
```

Additional provider DLLs may be needed if the Windows build enables GPU execution providers.

## PortableApps Location

Close OBS first. If an older version was installed before, run `remove-old-installation.bat` from the ZIP before
copying the new folder. When prompted, enter the directory that contains `OBS-StudioPortable.exe`.

Then copy the complete `obs-backgroundremoval` plugin folder into:

```text
<OBS-StudioPortable>\Data\obs-studio\plugins\
```

The final layout should look like this:

```text
<OBS-StudioPortable>\Data\obs-studio\plugins\obs-backgroundremoval\bin\64bit\obs-backgroundremoval.dll
<OBS-StudioPortable>\Data\obs-studio\plugins\obs-backgroundremoval\data\models\mediapipe.with_runtime_opt.ort
<OBS-StudioPortable>\Data\obs-studio\plugins\obs-backgroundremoval\data\locale\en-US.ini
<OBS-StudioPortable>\Data\obs-studio\plugins\obs-backgroundremoval\data\effects\mask_alpha_filter.effect
```

Restart OBS after copying the files. The Background Removal and Low-Light Enhancement filters should appear in a video
source's filter list.

## Standard Windows OBS

For a normal Windows OBS install, copy the same plugin folder into:

```text
C:\ProgramData\obs-studio\plugins\
```

Do not install OBS plugins under `C:\Program Files\obs-studio`. The upstream project documents `ProgramData` as the
supported Windows plugin location.

## Troubleshooting

- If the filters are not listed, confirm that OBS is 64-bit and that `obs-backgroundremoval.dll` is a Windows x64 DLL.
- If OBS reports missing ONNX Runtime libraries, put the ONNX Runtime DLLs beside `obs-backgroundremoval.dll`.
- If OBS reports missing model files, confirm that the bundle contains `obs-backgroundremoval\data\models`.
- If OBS still reports version `1.3.x`, it is loading an older DLL from another folder. Open the latest log under
  `<OBS-StudioPortable>\Data\obs-studio\logs\`, search for `obs-backgroundremoval`, delete the old path shown there,
  and then copy the new `obs-backgroundremoval` folder again.
- Restart OBS after replacing plugin files.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
>
> SPDX-License-Identifier: GPL-3.0-or-later  
