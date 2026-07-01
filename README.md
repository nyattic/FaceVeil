# FaceVeil

![Release](https://img.shields.io/github/v/release/nyattic/FaceVeil?style=flat&color=6366f1)
![Downloads](https://img.shields.io/github/downloads/nyattic/FaceVeil/total?style=flat&color=10b981)
![Last Commit](https://img.shields.io/github/last-commit/nyattic/FaceVeil?style=flat&color=f59e0b)
![License](https://img.shields.io/badge/app%20code-PolyForm%20Noncommercial%201.0.0-8b5cf6?style=flat)

Local desktop app that automatically mosaics faces in your photos. Drop in images or folders, pick a model, get anonymized copies — your photos are processed entirely on your machine and never uploaded.

## Install

Download from [Releases](https://github.com/nyattic/FaceVeil/releases/latest):

- **macOS** (Apple Silicon, macOS 15+) — open the `.dmg`, drag to Applications
- **Windows** (x64, Windows 10+) — unzip, run `FaceVeil.exe`

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

Requires CMake 3.24+, a C++20 compiler, Qt 6 available to CMake, OpenCV 4, ONNX Runtime, spdlog, and SCRFD ONNX model files.

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
brew install cmake qt opencv onnxruntime spdlog
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

### Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Packaging scripts: [`scripts/package_macos.sh`](scripts/package_macos.sh), [`scripts/package_windows.ps1`](scripts/package_windows.ps1), [`scripts/notarize_macos.sh`](scripts/notarize_macos.sh).

## Privacy

Your images never leave your device — they are read from disk, processed locally, and written to the output folder you pick. The only network request FaceVeil ever makes is a one-time download of the face-detection model (from Hugging Face) the first time you use a built-in model. Supply your own model with **Browse…** and it makes no network calls at all.

## License

**Application source code** — PolyForm Noncommercial License 1.0.0. SPDX identifier: `PolyForm-Noncommercial-1.0.0`. Personal and other noncommercial use is permitted; **commercial use is not**. See [LICENSE](LICENSE).

Copyright © 2026 Nyabi.

**SCRFD models** — the built-in models are **not distributed with FaceVeil**; the app downloads them on first use directly from their source. They originate from [InsightFace](https://github.com/deepinsight/insightface) and are available for **non-commercial research use only**, under their own terms separate from the application license. See the [InsightFace Model Zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md) for details.

**Third-party runtime dependencies** — Qt (LGPL-3.0 / GPL-3.0 / commercial), OpenCV (Apache-2.0), ONNX Runtime (MIT), spdlog and {fmt} (MIT). Each retains its own license; the full texts are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and are bundled with each release.

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
