<!-- Keep each new release bilingual, using `## English` followed by `## 한국어`.
     The app uses these headings to select release notes for its UI language. -->

# Redactly 1.8.1

## English

Video processing is faster on macOS, and batch runs now protect existing
output files and report incomplete results more clearly.

### Improvements
- Face detection models now use static input dimensions so CoreML can run the
  whole graph instead of leaving unsupported work on the CPU
- Video output uses Apple's VideoToolbox hardware H.264 encoder when available,
  with automatic fallback to software encoding
- Redactly checks every planned output before processing and refuses to start
  if a file would be overwritten or two inputs would produce the same path
- Runs with failures, skipped files, or outputs containing no redacted regions
  now finish as **Review required** with a clearer summary
- Very low detection thresholds no longer promote weak video detections into
  thousands of false tracks that obscure unrelated parts of the frame
- Update notifications now show the release notes in the app's selected
  language, with a choice to update now or postpone

## 한국어

macOS에서 동영상 처리 속도가 향상되었으며, 이제 일괄 처리 시 기존 출력
파일을 보호하고 완료되지 않은 작업 결과를 더욱 명확하게 알려줍니다.

### 개선 사항
- 얼굴 탐지 모델에 고정 입력 크기를 적용하여 CoreML이 일부 작업을 CPU에
  맡기지 않고 전체 그래프를 실행할 수 있도록 개선했습니다
- 가능한 경우 Apple의 VideoToolbox 하드웨어 H.264 인코더를 사용하며,
  사용할 수 없으면 소프트웨어 인코딩으로 자동 전환됩니다
- 처리 전에 생성할 모든 출력 경로를 확인하며, 기존 파일을 덮어쓰거나 두
  입력 파일이 같은 경로에 저장될 경우 작업을 시작하지 않습니다
- 실패하거나 건너뛴 파일 또는 가림 처리된 영역이 없는 출력물이 있으면
  작업 상태를 **검토 필요**로 표시하고 더욱 명확한 요약을 제공합니다
- 탐지 임계값이 매우 낮더라도 약한 동영상 탐지가 수천 개의 잘못된 트랙으로
  승격되어 화면의 무관한 영역을 가리지 않도록 개선했습니다
- 업데이트 알림에서 앱 설정 언어에 맞는 릴리스 노트를 표시하고, 바로
  업데이트하거나 나중으로 미룰 수 있습니다

---

# Redactly 1.8.0

Video face detection is more accurate: it now analyzes video at the full
detection resolution, so it catches faces it used to miss and stops covering
signage it mistook for a face.

## Improvements
- The fast face model now analyzes video at the full detection resolution
  instead of a third of it, so smaller and partly turned faces that used to
  slip through are covered
- Fewer false masks: a logo or sign that briefly resembles a face no longer
  stays covered for the rest of the shot
- Masks follow faces more faithfully — they no longer glide into place a few
  frames early, and stay on a face through brief detection dropouts

## Details
- The bundled fast model ships with a fixed 640 px input; it is now adapted in
  memory at load time to run at the requested video resolution, with a probe
  check and automatic fallback to its native size, or to the CPU, if a GPU
  backend can't run it
- A track needs several confident detections to be kept, so sparse false
  positives are dropped — while a short but clearly detected face is still
  covered
- A track that goes too long without a confident detection ends instead of
  coasting indefinitely, and moving faces get more leeway than static signage

---

# Redactly 1.7.1

Fixes video redaction failing on macOS with GPU acceleration, and stops a
redaction mask from sliding into place when a new face appears.

## Fixes
- Video redaction on macOS no longer stops with a CoreML error partway
  through: the face and license-plate models that Apple's GPU backend can't
  run now fall back to the CPU automatically, while the models it can run
  stay on the GPU
- A newly appeared face is now covered where it appears, instead of the mask
  sliding in from where a different face left the frame

## Details
- Each detector is exercised once as it loads; if the GPU backend rejects it,
  that detector is rebuilt on the CPU. Photo detection, the fast 640 px face
  model, and the Windows/Linux GPU paths are unaffected
- Detection that falls back to the CPU now runs across all cores instead of a
  single thread, cutting the accurate face model's analysis time on
  high-resolution video by roughly two-thirds
- A track that loses its face no longer drifts across the frame to grab an
  unrelated new one, and a gap that would imply an implausibly fast jump is
  left uninterpolated rather than drawn as a slide

---

# Redactly 1.7.0

Video redaction is dramatically faster, and mosaics now fully cover close-up
faces.

## Improvements
- Video processing is several times faster — a 3-minute 1080p clip that used to
  take over ten minutes now finishes in under a minute
- Blur redaction is no longer the bottleneck: it renders at a fraction of the
  previous cost with the same visual strength
- Mosaics on large, close-up faces are now properly coarse, so a face stays
  unrecognizable no matter how much of the frame it fills

## Details
- Blur now runs on a downscaled copy of each region and is scaled back up,
  capping the kernel cost while preserving the same blur strength
- The soft-edge feather mask is computed once per face size and reused across
  frames regardless of position, instead of being rebuilt for every frame
- The mosaic block count is capped so a face is always reduced to at most a
  dozen cells across, keeping large faces obscured
- Frame redaction runs in parallel across CPU cores, and encode writes overlap
  the masking work, so both passes use the machine more fully

---

# Redactly 1.6.1

Video tracking is now scene-cut aware, fixing mosaic ghosting in fast-cut
footage.

## Fixes
- Mosaics no longer linger for up to a second after a hard cut, drifting
  across the new shot
- A mosaic can no longer jump to a different person who appears in a
  similar position right after a cut

## Details
- The analysis pass now detects shot boundaries, and tracks stop cleanly at
  every cut: no track survives a scene change, no gap is interpolated
  across one, and track ends are never extended past one — in both tracking
  directions
- A cut is only declared when the frame-to-frame change stands well above
  the recent motion level **and** persists for the following frames, so
  camera flashes, strobes, and fast pans within a shot do not split tracks
- Coverage inside a shot — through motion blur, side profiles, and brief
  occlusions — is unchanged
- The log reports how many scene cuts each video contained

---

# Redactly 1.6.0

Adds video redaction (beta) and GPU acceleration.

**Videos (beta).** Drop `.mp4`, `.mov`, or `.m4v` files (H.264/HEVC) alongside your
photos and Redactly redacts faces and license plates in every frame. Videos
are processed in two passes: every frame is analyzed and detections are linked
into tracks — bidirectionally, with gap interpolation and temporal smoothing —
so coverage holds through motion blur, side profiles, and brief occlusions;
the second pass applies the redaction and encodes the result. Decoding is
paced to the analysis speed, so memory use stays bounded even for long,
high-resolution videos. Output is always
an H.264 MP4 with the original audio (re-encoded to AAC only when the source
codec doesn't fit MP4), container metadata — including GPS — removed, and
rotation baked into the pixels. A **Video quality** preset in Settings picks
between **High (near-original)**, **Balanced**, and **Smaller files**.

Video support is a beta: detection coverage and processing speed are still
being validated across a wider range of real-world footage, so check the
output before sharing it — every video is listed in the end-of-run summary,
and one in which nothing was detected is called out explicitly, same as
photos. On Linux the video pipeline has only been exercised by automated
tests so far, not by hand.

Video processing uses the FFmpeg bundled with the app, or an FFmpeg found on
`PATH`. Known limits in this release: variable-frame-rate input is converted
to a constant frame rate, 10-bit/HDR input is rejected rather than silently
degraded, other codecs and containers (VP9/AV1, WebM/MKV) are reported as
unsupported, and videos are processed without the review step. Stopping a
run mid-video removes the partial output file. Bundling FFmpeg grows the
downloads by roughly 40–100 MB depending on the platform.

**GPU acceleration.** Detection now runs on the GPU where available — CoreML
on macOS, DirectML on Windows — with automatic CPU fallback and a Settings
toggle (on by default). The Windows release bundles the DirectML build of
ONNX Runtime together with `DirectML.dll`, so any DirectX 12 capable GPU —
NVIDIA, AMD, and Intel alike — is accelerated out of the box (Windows 10
1903 or later); the log reports the backend as DirectML instead of CPU, and
machines without a DirectX 12 GPU keep working on the CPU. On Apple Silicon
the face models run roughly 9–13× faster, which is also what makes per-frame
video analysis practical. Bundling DirectML grows the Windows download by
roughly 20 MB.

Video encoding on Windows also moves to the GPU when one is available. The
encoder is picked automatically per machine — NVENC on NVIDIA, AMF on AMD,
Quick Sync on Intel — by test-encoding a few frames before each video, and
falls back to CPU x264 when no hardware encoder works or when GPU
acceleration is switched off in Settings. The log names the encoder used
for each video. Hardware encoders trade a little compression efficiency
for speed, so files may come out somewhat larger than CPU-encoded ones at
the same quality preset.

**Faster photo batches.** When review is off, images are processed in
parallel (up to four at a time), preserving the original log and progress
order.

---

# Redactly 1.5.2

Adds a **Soft edges** option to the Advanced panel. When enabled, the edge of
each obscured region fades into the photo like an airbrush instead of ending
in a hard cutoff. The fade only extends outward from the detected area, so
the region itself stays fully obscured. It works with every anonymization
method and both shapes, and is reflected in the style preview and the review
dialog. Off by default.

---

# Redactly 1.5.1

Polishes the Korean translation. The header subtitle drops the redundant
privacy pitch, "redact" is now consistently rendered as 가리다 (a few strings
still said 편집/"edit"), and the Copy Original prompt matches the phrasing of
the other confirmation dialogs.

---

# Redactly 1.5.0

Adds a light/dark theme. A new **Settings** dialog — opened from the gear
button in the header — lets you pick **System**, **Light**, or **Dark**;
System follows the OS appearance and switches live when it changes. The
language and startup update-check options now live in this dialog too.

Also fixes two important issues. **Stop** now takes effect immediately during
a batch, and closing the window mid-run no longer hangs the app. And when
**Preserve original metadata** is on, the embedded EXIF preview thumbnail —
which still showed the unredacted image — is now stripped from the output.

Batch runs are more robust and honest: a corrupt or unreadable image no longer
aborts the rest of the batch, images saved with no regions redacted are called
out per image and in the summary, runs that couldn't start report a failure
instead of "Done", and an input file that already lives in the output folder is
skipped instead of being overwritten in place.

In the review dialog, Esc (or closing the window) now skips just the current
image instead of cancelling the whole batch, **Cancel All** asks for
confirmation first, and the preview supports scroll-to-zoom and right-drag
panning so small faces in large photos can be adjusted precisely. Review also
works fully from the keyboard — arrow keys select a box, Return toggles it,
Delete excludes it — and holding **Space** previews the anonymized result
before saving. Korean users get localized Yes/No dialog buttons and the
previously untranslated license-plate download prompt. Each release now ships
a `SHA256SUMS` file so downloads can be verified.

Quality-of-life improvements: input rows show thumbnails and selected entries
can be removed with Delete or a right-click menu, unsupported files are
filtered out at drop time and the list highlights while dragging, Advanced
Options shows a live sample of the chosen anonymization style, an **Open
Output Folder** button and the elapsed time appear when a run finishes, the
Settings button and Advanced Options toggle are reachable by keyboard
(Settings also via the standard Preferences shortcut), and the app follows
the system font size.

---

# Redactly 1.4.0

The app is now called **Redactly** (formerly FaceVeil), reflecting that it
redacts more than faces.

Adds license plate anonymization. A new **Detect** option lets you anonymize
faces, license plates, or both; the license plate detector (an MIT-licensed
YOLOv9 model from the open-image-models project) is downloaded on first use,
like the face models. Existing behavior is unchanged — faces stay the default.

Note: settings and downloaded models live under a new **Redactly** data folder,
so the models are downloaded once more on first launch.

---

# Redactly 1.3.0

Adds an update check: at launch Redactly asks the GitHub Releases API whether a
newer version exists and, if so, shows a link in the header. No image or
personal data is sent, and the check can be disabled under Advanced Options →
Check for updates on startup.

---

# Redactly 1.2.0

Adds Linux support: Redactly now builds on Linux and ships a self-contained
`.AppImage` in each release alongside the macOS and Windows downloads.

---

# Redactly 1.1.2

Fixes opening images with non-ASCII (e.g. Korean) filenames on Windows, which
previously failed with "No mapping for the Unicode character exists in the
target multi-byte code page."

---

# Redactly 1.1.1

Lowers the default mosaic block size from 28 to 14 pixels for a finer default
mosaic.

---

# Redactly 1.1.0

Adds full metadata and quality preservation for photographers, a rounded mask
shape, a clearer model-download flow, and inline validation. Redactly's own
license moves to the GPL v3.0-or-later.

## License
- **The application source code is now under the GNU General Public License v3.0 or later (previously PolyForm Noncommercial 1.0.0).** You may use, study, share, and modify Redactly — including commercially — provided that distributed copies and derivatives remain under the GPL with corresponding source available. The change is required because Redactly now links **Exiv2** (GPL-2.0-or-later) for metadata handling. Versions released earlier under PolyForm Noncommercial remain under that license.

## Features
- **Preserve original metadata** (Output → checkbox, off by default). When on, Redactly copies EXIF / IPTC / XMP and the ICC color profile from the original, and keeps the original format and bit depth at maximum quality — ideal for archiving high-resolution photos. When off, output carries no metadata (GPS, camera, and timestamps are stripped) for privacy.
- **Lossless / maximum-quality encoding**: JPEG is written at quality 100 with no chroma subsampling; PNG, WebP, and TIFF are written losslessly. 16-bit and alpha channels are preserved.
- **Mask shape** (Advanced): **Rectangle** or **Rounded (ellipse)** that follows the face and leaves corners untouched.
- **Download button** for built-in models: a model that isn't on disk shows a Download button next to its path, so you no longer have to press Start to trigger the download.
- Inline validation: missing model / inputs / output folder are now flagged in the status bar and on the offending field, not just in the log.
- Empty-state hint in the input list, and a tidied header layout.

## Notes
- **"Copy Original"** in review now makes a byte-exact copy of the source when *Preserve original metadata* is on (no re-encode); with the option off it re-encodes at maximum quality without metadata.

---

# Redactly 1.0.0

The first 1.0 release. Redactly is now licensed for noncommercial use, speaks English and Korean, fetches its face-detection models on first use instead of bundling them, works with current ONNX Runtime, and adds new anonymization styles, a run summary, and diagnostic logging.

## License
- **The application source code is now under the PolyForm Noncommercial License 1.0.0 (previously MIT).** Personal and other noncommercial use is permitted; commercial use is not. Versions released earlier under MIT remain MIT.

## Fixes
- Fixed a startup failure on ONNX Runtime 1.27+ where a dangling type-info view made every model load fail with "input must be a float tensor"
- SCRFD outputs are now matched by tensor shape and per-stride anchor count instead of output order, so compatible models decode reliably and an incompatible model fails loudly instead of silently skipping faces

## Features
- Anonymization style selector: **Mosaic** (pixelate), **Gaussian blur**, or **Solid fill** (blackout)
- End-of-run summary reporting anonymized / copied / skipped / failed counts
- The current version is shown in the app header

## Localization
- The interface is available in **English** and **한국어 (Korean)**, switchable live from the header. The initial language follows your system locale.

## Models & privacy
- SCRFD models are no longer bundled in the download. On first use of a built-in model, Redactly fetches it once from Hugging Face with a SHA-256 integrity check and caches it locally. Your images are always processed on-device and never uploaded.

## Logging
- Logging now runs through spdlog: colored console output plus a rotating log file under the platform data directory (`~/Library/Application Support/Redactly/logs` on macOS)

## Download

- **macOS (Apple Silicon)** — `Redactly-1.0.0-arm64.dmg`, signed and notarized. Requires macOS 15+.
- **Windows (x64)** — `Redactly-1.0.0-windows-x64.zip`. Unzip and run `Redactly.exe`. Requires Windows 10 or later.

---

# Redactly 0.1.2

A maintenance release focused on safer batch processing, clearer review behavior, and basic automated test coverage.

## Security
- Custom ONNX model selection now checks for an existing `.onnx` file and rejects files larger than 512 MB
- Custom model loading now shows a trust warning before accepting the file
- SCRFD model validation now checks input tensor type/shape compatibility and output tensor element types before processing

## Stability
- Processing now refuses to start when multiple inputs would write to the same output path
- Preflight logging now reports the number of supported images found and confirms output path uniqueness
- Cancellation is checked between more processing stages, reducing the time between pressing Stop and the run ending

## UX
- Review mode now separates **Do Not Save** from **Copy Original**, avoiding accidental original-image output
- Closing a review dialog now cancels the whole run instead of implicitly saving
- Review-mode undo/redo hints now use the platform-native shortcut text

## Tests
- Added a CTest target covering supported image extensions, recursive image scanning, de-duplication, and mosaic behavior
- README now documents how to run the test suite

## Download

- **macOS (Apple Silicon)** — `Redactly-0.1.2-arm64.dmg`, signed and notarized. Requires macOS 12+.
- **Windows (x64)** — `Redactly-0.1.2-windows-x64.zip`. Unzip and run `Redactly.exe`. Requires Windows 10 or later.

---

# Redactly 0.1.1

A maintenance release: security, stability, and UX improvements. No feature changes.

## Security
- Path-traversal defence resolves symlinks inside the output tree
- Files > 1 GiB or images > 200 MP are skipped to prevent decode bombs
- Minimised macOS hardened-runtime entitlements
- Compiler hardening flags (`-fstack-protector-strong`, `_FORTIFY_SOURCE=2`, PIE, MSVC `/guard:cf`)
- ONNX model output tensors are validated before decode

## Stability
- Atomic image writes (temp-file + rename)
- Cooperative thread shutdown; `QThread::terminate()` removed
- Directory scan skips permission errors and breaks symlink cycles
- Case-insensitive input de-duplication
- Refuses to run when the output folder is inside any input

## Performance
- SCRFD session is reused across runs with the same model
- Review dialog uses a downscaled preview (<= 1600 px long edge) for huge images
- Dropped a redundant `cvtColor` in BGR -> QImage conversion

## UX
- Per-image progress stages (Loading / Detecting / Reviewing / Applying mosaic / Saving)
- Undo / Redo in Review mode (Cmd+Z / Cmd+Shift+Z, 64-step history)
- Settings and window geometry persist across launches
- Drag-and-drop shows the correct cursor for invalid drops
- Activity log capped at 5,000 lines

## Packaging
- Windows script: qmake-based Qt detection, stricter dependency checks
- macOS script: dynamic Homebrew prefix, upfront signing-identity validation
- Notarization script: optional timeout, clearer failure messages

## Download

- **macOS (Apple Silicon)** — `Redactly-0.1.1-arm64.dmg`, signed and notarized. Requires macOS 12+.
- **Windows (x64)** — `Redactly-0.1.1-windows-x64.zip`. Unzip and run `Redactly.exe`. Requires Windows 10 or later.
