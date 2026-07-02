# FaceVeil

![Release](https://img.shields.io/github/v/release/nyattic/FaceVeil?style=flat&color=6366f1)
![Downloads](https://img.shields.io/github/downloads/nyattic/FaceVeil/total?style=flat&color=10b981)
![Last Commit](https://img.shields.io/github/last-commit/nyattic/FaceVeil?style=flat&color=f59e0b)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-8b5cf6?style=flat)

Local desktop app that automatically mosaics faces in your photos. Drop in images or folders, pick a model, get anonymized copies — your photos are processed entirely on your machine and never uploaded.

## Install

Download from [Releases](https://github.com/nyattic/FaceVeil/releases/latest):

- **macOS** (Apple Silicon, macOS 15+) — open the `.dmg`, drag to Applications
- **Windows** (x64, Windows 10+) — unzip, run `FaceVeil.exe`
- **Linux** (x86_64) — download the `.AppImage`, `chmod +x` it, and run it

The first time you use a built-in model, FaceVeil downloads it once (3–17 MB) from Hugging Face and caches it; after that it runs offline.

## Use

1. Drop images or folders onto the window
2. Pick a model — **Fast** or **Accurate**
3. Choose an output folder
4. Click **Start**

Originals are never modified. Enable **Review each image** to inspect detections before saving, exclude false positives, add missed faces, leave an image unsaved, or explicitly copy the original.

FaceVeil refuses to start if two inputs would write to the same output path, so existing results are not silently overwritten.

Supported inputs: `.jpg` `.jpeg` `.png` `.bmp` `.tif` `.tiff` `.webp`.

## Build from source

Requires CMake 3.24+, a C++20 compiler, Qt 6 available to CMake (with the Linguist tools for UI translations), OpenCV 4, ONNX Runtime, spdlog, Exiv2 (optional, for metadata preservation), and SCRFD ONNX model files.

The built-in models are **not bundled** and **not committed** to this repository. The app downloads them on first use (with an integrity check) and caches them under the platform data directory. To pre-place them for offline use, drop them in `models/`:

- `models/2.5g_bnkps.onnx` — Fast
- `models/10g_bnkps.onnx` — Accurate

You can also launch the app and use **Browse…** to select a custom SCRFD `.onnx` file.

Only load custom ONNX models from sources you trust. FaceVeil checks basic SCRFD tensor compatibility before processing, but ONNX files are still executable model inputs handled by native runtime libraries.

### macOS

```bash
cmake -S . -B build
cmake --build build
open build/FaceVeil.app
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
  -DONNXRUNTIME_ROOT="C:\onnxruntime-win-x64-1.24.4"
cmake --build build-windows --config Release
```

spdlog must also be discoverable by CMake — for example install it with [vcpkg](https://vcpkg.io) (`vcpkg install spdlog`) and add the vcpkg entries to `CMAKE_PREFIX_PATH`.

### Linux

Install the build dependencies (Debian/Ubuntu example):

```bash
sudo apt install cmake ninja-build build-essential pkg-config \
  qt6-base-dev qt6-tools-dev libopencv-dev libspdlog-dev libexiv2-dev
```

ONNX Runtime is detected via `pkg-config libonnxruntime` when available; otherwise point CMake at an ONNX Runtime release:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64
cmake --build build
./build/FaceVeil
```

### Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Packaging scripts: [`scripts/package_macos.sh`](scripts/package_macos.sh), [`scripts/package_windows.ps1`](scripts/package_windows.ps1), [`scripts/package_linux.sh`](scripts/package_linux.sh), [`scripts/notarize_macos.sh`](scripts/notarize_macos.sh).

## Privacy

Your images never leave your device — they are read from disk, processed locally, and written to the output folder you pick. FaceVeil makes only two kinds of network request, and neither sends any image or personal data: a one-time download of the face-detection model (from Hugging Face) the first time you use a built-in model, and a check at launch against the GitHub Releases API to see whether a newer version exists. The update check can be turned off under **Advanced Options → Check for updates on startup**, and supplying your own model with **Browse…** avoids the model download entirely.

## License

**Application source code** — GNU General Public License v3.0 or later. SPDX identifier: `GPL-3.0-or-later`. You may use, study, share, and modify FaceVeil, including for commercial purposes; if you distribute it or a derivative, you must do so under the GPL and make the corresponding source available. See [LICENSE](LICENSE).

> The application was previously licensed under PolyForm Noncommercial 1.0.0. It moved to the GPL v3.0-or-later starting with v1.1.0 because FaceVeil now links [Exiv2](https://exiv2.org/) (GPL-2.0-or-later) for metadata preservation. Versions released earlier under PolyForm Noncommercial remain under that license.

Copyright © 2026 Nyabi.

FaceVeil is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

**SCRFD models** — the built-in models are **not distributed with FaceVeil**; the app downloads them on first use from a [Hugging Face mirror](https://huggingface.co/RuteNL/SCRFD-face-detection-ONNX). They originate from [InsightFace](https://github.com/deepinsight/insightface) and are available for **non-commercial research use only**, under their own terms separate from the application license (the mirror's Apache-2.0 tag does not override InsightFace's terms). See the [InsightFace Model Zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md) for details.

**Third-party runtime dependencies** — Qt (LGPL-3.0 / GPL-3.0 / commercial), OpenCV (Apache-2.0), ONNX Runtime (MIT), Exiv2 (GPL-2.0-or-later) with its own dependencies (Brotli, Expat, inih, zlib, GNU gettext), spdlog and {fmt} (MIT). Each retains its own license; the full texts are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and are bundled with each release.

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
```
