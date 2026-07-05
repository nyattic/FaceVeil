# Redactly

![Release](https://img.shields.io/github/v/release/nyattic/Redactly?style=flat&color=6366f1)
![Downloads](https://img.shields.io/github/downloads/nyattic/Redactly/total?style=flat&color=10b981)
![Last Commit](https://img.shields.io/github/last-commit/nyattic/Redactly?style=flat&color=f59e0b)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-8b5cf6?style=flat)

Local desktop app that automatically redacts faces and license plates in your photos and videos. Drop in images, videos, or folders, choose what to detect, get anonymized copies — your files are processed entirely on your machine and never uploaded.

## Install

Download from [Releases](https://github.com/nyattic/Redactly/releases/latest):

- **macOS** (Apple Silicon, macOS 15+) — open the `.dmg`, drag to Applications
- **Windows** (x64, Windows 10+) — unzip, run `Redactly.exe`. GPU acceleration needs Windows 10 1903+ and a DirectX 12 capable GPU (NVIDIA, AMD, or Intel); without one, detection runs on the CPU.
- **Linux** (x86_64) — download the `.AppImage`, `chmod +x` it, and run it

The first time you use a built-in model, Redactly downloads it once (3–17 MB) and caches it; after that it runs offline. The face models come from Hugging Face; the license plate model comes from the open-image-models project on GitHub.

## Use

1. Drop images, videos, or folders onto the window
2. Choose what to detect — **Faces**, **License plates**, or **both**
3. For faces, pick a model — **Fast** or **Accurate**
4. Choose an output folder
5. Click **Start**

Originals are never modified. Enable **Review each image** to inspect detections before saving, exclude false positives, add missed faces, leave an image unsaved, or explicitly copy the original.

Redactly refuses to start if two inputs would write to the same output path, so existing results are not silently overwritten.

Supported inputs: `.jpg` `.jpeg` `.png` `.bmp` `.tif` `.tiff` `.webp` images, and `.mp4` `.mov` `.m4v` videos (H.264/HEVC, 8-bit SDR). Video support is currently in **beta** — check the output before sharing it. On Linux the video pipeline is covered by automated tests but has not been manually tested yet.

Detection runs on the GPU where available — CoreML on macOS, DirectML on Windows (bundled with the release and accelerating NVIDIA, AMD, and Intel GPUs alike) — with automatic CPU fallback and a Settings toggle (on by default).

Videos are processed in two passes — detection with bidirectional tracking, then encoding — so faces stay covered through motion blur and brief occlusions. On Windows, encoding uses the GPU's hardware encoder (NVENC, AMF, or Quick Sync) when one works, falling back to CPU x264 otherwise or when GPU acceleration is off in Settings. Output is always H.264 MP4 with the original audio (re-encoded to AAC only when the source codec doesn't fit MP4), container metadata removed, and rotation baked into the pixels. Variable frame rate input is converted to a constant frame rate; 10-bit/HDR input is rejected rather than silently degraded. Video processing uses an FFmpeg bundled next to the app when present, otherwise an FFmpeg found on `PATH`. Videos are processed without the review step, and the video quality preset lives in Settings.

## Build from source

Requires CMake 3.24+, a C++20 compiler, Qt 6 available to CMake (with the Linguist tools for UI translations; Qt Svg is optional and gives a crisp settings icon, falling back to a glyph without it), OpenCV 4, ONNX Runtime, spdlog, and Exiv2 (optional, for metadata preservation). FFmpeg is not a build dependency, but video processing needs `ffmpeg` and `ffprobe` at runtime (bundled next to the app, or on `PATH`). The detection models (SCRFD for faces, YOLOv9 for license plates) are not build dependencies — the app downloads them on first use, or you can pre-place them (see below).

The built-in models are **not bundled** and **not committed** to this repository. The app downloads them on first use (with an integrity check) and caches them under the platform data directory. To pre-place them for offline use, drop them in `models/`:

- `models/2.5g_bnkps.onnx` — Fast (faces)
- `models/10g_bnkps.onnx` — Accurate (faces)
- `models/yolo-v9-t-512-license-plates-end2end.onnx` — License plates

You can also launch the app and use **Browse…** to select a custom SCRFD `.onnx` file.

Only load custom ONNX models from sources you trust. Redactly checks basic SCRFD tensor compatibility before processing, but ONNX files are still executable model inputs handled by native runtime libraries.

### macOS

```bash
cmake -S . -B build
cmake --build build
open build/Redactly.app
```

Install dependencies with Homebrew:

```bash
brew install cmake qt opencv onnxruntime spdlog exiv2
```

### Windows (PowerShell)

```powershell
cmake -S . -B build-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\msvc2022_64;C:\opencv\build" `
  -DONNXRUNTIME_ROOT="C:\onnxruntime-win-x64-1.20.1"
cmake --build build-windows --config Release
```

spdlog must also be discoverable by CMake — for example install it with [vcpkg](https://vcpkg.io) (`vcpkg install spdlog`) and add the vcpkg entries to `CMAKE_PREFIX_PATH`.

Any ONNX Runtime build works for development, but official Windows releases use the DirectML build so detection runs on the GPU: the `Microsoft.ML.OnnxRuntime.DirectML` NuGet package staged into an `include`/`lib` layout, with `DirectML.dll` from the `Microsoft.AI.DirectML` package placed next to `onnxruntime.dll` (see the Windows job in `.github/workflows/release.yml`). `scripts/package_windows.ps1` refuses to package without `DirectML.dll`.

### Linux

Install the build dependencies (Debian/Ubuntu example):

```bash
sudo apt install cmake ninja-build build-essential pkg-config \
  qt6-base-dev qt6-tools-dev libqt6svg6-dev libopencv-dev libspdlog-dev libexiv2-dev
```

ONNX Runtime is detected via `pkg-config libonnxruntime` when available; otherwise point CMake at an ONNX Runtime release:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64
cmake --build build
./build/Redactly
```

### Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The video I/O tests exercise a real FFmpeg round trip and are skipped when FFmpeg is not installed.

Packaging scripts: [`scripts/package_macos.sh`](scripts/package_macos.sh), [`scripts/package_windows.ps1`](scripts/package_windows.ps1), [`scripts/package_linux.sh`](scripts/package_linux.sh), [`scripts/notarize_macos.sh`](scripts/notarize_macos.sh).

## Privacy

Your images and videos never leave your device — they are read from disk, processed locally (video encoding runs through a local FFmpeg process), and written to the output folder you pick. Redactly makes only two kinds of network request, and neither sends any image or personal data: a one-time download of a detection model the first time you use each built-in model — the face models from Hugging Face, or the license plate model from the open-image-models project on GitHub — and a check at launch against the GitHub Releases API to see whether a newer version exists. The update check can be turned off under **Settings → Check for updates on startup**, and supplying your own model with **Browse…** avoids the model download entirely.

## License

**Application source code** — GNU General Public License v3.0 or later. SPDX identifier: `GPL-3.0-or-later`. You may use, study, share, and modify Redactly, including for commercial purposes; if you distribute it or a derivative, you must do so under the GPL and make the corresponding source available. See [LICENSE](LICENSE).

> The application was previously licensed under PolyForm Noncommercial 1.0.0. It moved to the GPL v3.0-or-later starting with v1.1.0 because Redactly now links [Exiv2](https://exiv2.org/) (GPL-2.0-or-later) for metadata preservation. Versions released earlier under PolyForm Noncommercial remain under that license.

Copyright © 2026 Nyabi.

Redactly is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

**SCRFD models** — the built-in models are **not distributed with Redactly**; the app downloads them on first use from a [Hugging Face mirror](https://huggingface.co/RuteNL/SCRFD-face-detection-ONNX). They originate from [InsightFace](https://github.com/deepinsight/insightface) and are available for **non-commercial research use only**, under their own terms separate from the application license (the mirror's Apache-2.0 tag does not override InsightFace's terms). See the [InsightFace Model Zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md) for details.

**License plate model** — the built-in license plate detector is **not distributed with Redactly**; the app downloads it on first use from the [open-image-models](https://github.com/ankandrew/open-image-models) project by ankandrew, which is MIT-licensed. It is a YOLOv9-architecture model (see [Citation](#citation)) and is downloaded at runtime and cached locally, under its upstream project's terms. Confirm the current terms with the open-image-models project before any commercial or redistribution use.

**Third-party runtime dependencies** — Qt (LGPL-3.0 / GPL-3.0 / commercial), OpenCV (Apache-2.0), ONNX Runtime (MIT), DirectML (proprietary Microsoft license permitting redistribution; bundled with Windows releases only, as `DirectML.dll`), Exiv2 (GPL-2.0-or-later) with its own dependencies (Brotli, Expat, inih, zlib, GNU gettext), spdlog and {fmt} (MIT), and FFmpeg (LGPL-2.1-or-later with optional GPL components), which is invoked as a separate process for video decoding and encoding. Each retains its own license; the full texts are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and are bundled with each release.

## Citation

```bibtex
@misc{guo2021sample,
  title={Sample and Computation Redistribution for Efficient Face Detection},
  author={Jia Guo and Jiankang Deng and Alexandros Lattas and Stefanos Zafeiriou},
  year={2021},
  eprint={2105.04714},
  archivePrefix={arXiv},
  primaryClass={cs.CV}
}

@misc{wang2024yolov9,
  title={YOLOv9: Learning What You Want to Learn Using Programmable Gradient Information},
  author={Chien-Yao Wang and Hong-Yuan Mark Liao},
  year={2024},
  eprint={2402.13616},
  archivePrefix={arXiv},
  primaryClass={cs.CV}
}
```
