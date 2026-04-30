# FaceVeil

![Release](https://img.shields.io/github/v/release/nyabi021/FaceVeil?style=flat&color=6366f1)
![Downloads](https://img.shields.io/github/downloads/nyabi021/FaceVeil/total?style=flat&color=10b981)
![Last Commit](https://img.shields.io/github/last-commit/nyabi021/FaceVeil?style=flat&color=f59e0b)
![License](https://img.shields.io/badge/app%20code-MIT-8b5cf6?style=flat)

Local desktop app that automatically mosaics faces in your photos. Drop in images or folders, pick a model, get anonymized copies — nothing ever leaves your machine.

## Install

Download from [Releases](https://github.com/nyabi021/FaceVeil/releases/latest):

- **macOS** (Apple Silicon, macOS 12+) — open the `.dmg`, drag to Applications
- **Windows** (x64, Windows 10+) — unzip, run `FaceVeil.exe`

## Use

1. Drop images or folders onto the window
2. Pick a model — **Fast** or **Accurate**
3. Choose an output folder
4. Click **Start**

Originals are never modified. Enable **Review each image** to inspect detections before saving, exclude false positives, add missed faces, or skip individual images.

Supported inputs: `.jpg` `.jpeg` `.png` `.bmp` `.tif` `.tiff` `.webp`.

## Build from source

Requires CMake 3.24+, a C++20 compiler, Qt 6 available to CMake, OpenCV 4, ONNX Runtime, and SCRFD ONNX model files.

Put SCRFD models in `models/` before running the app, for example:

- `models/2.5g_bnkps.onnx` — Fast
- `models/10g_bnkps.onnx` — Accurate

Model files are not committed to this repository. You can also launch the app and use **Browse…** to select a custom SCRFD `.onnx` file.

### macOS

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/FaceVeil.app
```

Install dependencies with Homebrew:

```bash
brew install cmake opencv onnxruntime
```

### Windows (PowerShell)

```powershell
cmake -S . -B build-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\msvc2022_64;C:\opencv\build" `
  -DONNXRUNTIME_ROOT="C:\onnxruntime-win-x64-1.24.4"
cmake --build build-windows --config Release
```

Packaging scripts: [`scripts/package_macos.sh`](scripts/package_macos.sh), [`scripts/package_windows.ps1`](scripts/package_windows.ps1), [`scripts/notarize_macos.sh`](scripts/notarize_macos.sh).

## Privacy

No network calls. Images are read from disk, processed locally, and written to the output folder you pick.

## License

**Application source code** — MIT License. SPDX identifier: `MIT`. See [LICENSE](LICENSE).

Copyright © 2026 Nyabi.

**Bundled SCRFD models** — **NOT MIT.** Provided by [InsightFace](https://github.com/deepinsight/insightface) for **non-commercial research use only**. If you redistribute FaceVeil commercially, replace the bundled models with ones you have commercial rights to, or obtain a commercial licence from InsightFace. See the [InsightFace Model Zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md) for details.

**Third-party runtime dependencies** — Qt (LGPL-3.0 / GPL-3.0 / commercial), OpenCV (Apache-2.0), ONNX Runtime (MIT). Each retains its own license.

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
