<!--
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
# Background matting

BGOBS pairs each inference result with the exact captured source frame, its
sequence number, and its capture timestamp. The inference queue holds at most
one waiting frame; when inference falls behind, a newer frame replaces the
pending one rather than allowing latency to grow without bound. The filter
uploads the matched source, reconstructed foreground, and alpha only when a
new result is ready. RVM and Background Matting V2 (BMv2) both provide a
reconstructed foreground and a soft foreground alpha, so edge pixels are
composited from the model's decontaminated foreground instead of the original
background-contaminated camera pixels.

The filter properties show measured capture-to-render delay and the count of
replaced queued frames. The 100 ms figure is a target, not a promise: a warning
is shown when the latest measured delay exceeds it. A slow configuration can
also reduce the displayed frame rate; it should be measured at the intended
camera resolution and OBS output settings.

## Automatic and calibrated studio modes

Automatic mode uses Robust Video Matting (RVM). The separate **Studio
calibrated (experimental, BMv2)** mode uses the Background Matting V2
MobileNetV2 HD ONNX model and a clean-background reference. This is distinct
from the existing aesthetic **Studio** preset.

To capture a reference, select Studio calibrated and click **Capture the empty
background**. Leave the camera view empty during the three-second countdown
and the following one-second capture. The averaged frame is saved locally per
OBS filter instance. Capture again after changing the camera, crop, rotation,
or resolution. A missing or size-mismatched reference switches back to
automatic RVM and reports why. The reference is stored under OBS's module
configuration directory in `studio-backgrounds/`; it is not included in
scenes or sent over the network.

BMv2's ONNX export includes a fixed TopK operation and can be expensive on
CPU. In a local synthetic 1920x1080 benchmark on an AMD Ryzen 7 8745H Linux
machine, with four ONNX Runtime CPU threads, after three warmups and thirty
measured inferences, model latency averaged 155 ms (158 ms p95, 160 ms maximum). This
inference-only figure excludes image conversion and OBS compositing, exceeds
the 100 ms target, and does not establish real-time 1080p30 performance. It is
a reference measurement, not a guarantee for other CPUs, GPUs, camera inputs,
or OBS scenes. Studio calibrated remains
experimental until Windows DirectML and CPU fallback have been verified on a
real Windows machine and representative recorded clips show acceptable
quality, latency, memory use, and stability. Windows builds ship the pinned
ONNX Runtime DirectML and Microsoft DirectML redistributable runtimes; their
license and third-party notices are included with the package.

## Validation before a stable release

Use the same replayable recordings for the current and candidate versions:
fine hair, glasses, fingers, fast gestures, low light, and clothing close in
color to the background. Include both phone-camera and ordinary webcam sources
when available. Compare edge detail and halos as well as latency and frame
rate. Keep the original clips and record machine, OS, resolution, OBS version,
BGOBS version, and inference device with the results.

Exercise image/alpha pairing, resolution changes, reconnects, filter
disable/reactivation, and stopping the source while inference is in progress.
Run a 30-minute stability and memory check on the Windows reference machine
and verify its packaged DirectML GPU path plus CPU fallback. The current Linux
portable tests cover RVM tensor/state behavior, BMv2 inference shape/finiteness,
and general mask processing; they do not replace OBS-render or Windows GPU
smoke tests.

Do not publish a stable release claiming the quality or 1080p30 goal until the
before/after video and measured results are available. If BMv2 misses the
speed or quality criteria, keep it experimental and retain automatic RVM as
the default.
