param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$OpenCvRoot = $env:OpenCV_DIR,
    [string]$OnnxRuntimeRoot = $env:ONNXRUNTIME_ROOT,
    [string]$Generator = "Ninja",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RootDir "build-windows"
$DistDir = Join-Path $RootDir "dist/windows/FaceVeil"
$ExePath = Join-Path $BuildDir "FaceVeil.exe"

# ── Tool sanity ────────────────────────────────────────────────────
foreach ($tool in @("cmake")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool not found on PATH: $tool"
    }
}

# ── Qt root resolution ─────────────────────────────────────────────
# Accept a wide variety of Qt install layouts (e.g. C:\Qt\6.7.0\msvc2022_64
# or a CMake package dir like .../lib/cmake/Qt6). The canonical marker is
# bin\qmake.exe under the kit root.
function Resolve-QtRootPath {
    param([string]$Path)

    if (-not $Path) {
        foreach ($candidate in @($env:QT_ROOT, $env:QTDIR, $env:Qt6_DIR)) {
            if ($candidate) { $Path = $candidate; break }
        }
    }
    if (-not $Path) { return $null }
    if (-not (Test-Path $Path)) { return $null }

    $resolved = (Resolve-Path $Path).Path

    # If the path points into lib\cmake\Qt6, climb up to the kit root.
    for ($i = 0; $i -lt 4; $i++) {
        if (Test-Path (Join-Path $resolved "bin/qmake.exe")) {
            return $resolved
        }
        $parent = Split-Path $resolved -Parent
        if (-not $parent -or $parent -eq $resolved) { break }
        $resolved = $parent
    }

    # Final check so callers get a clear error rather than a mystery miss.
    if (Test-Path (Join-Path $resolved "bin/qmake.exe")) { return $resolved }
    return $null
}

$QtRoot = Resolve-QtRootPath $QtRoot
if (-not $QtRoot) {
    throw "QtRoot was not provided or does not look like a Qt kit (no bin\qmake.exe). Pass -QtRoot C:\Qt\6.x.x\msvc2022_64 or set QT_ROOT."
}
if (-not $OpenCvRoot -or -not (Test-Path $OpenCvRoot)) {
    throw "OpenCvRoot was not provided or does not exist. Pass -OpenCvRoot C:\opencv\build or set OpenCV_DIR."
}
if (-not $OnnxRuntimeRoot -or -not (Test-Path $OnnxRuntimeRoot)) {
    throw "OnnxRuntimeRoot was not provided or does not exist. Pass -OnnxRuntimeRoot C:\onnxruntime-win-x64 or set ONNXRUNTIME_ROOT."
}

$OpenCvRoot = (Resolve-Path $OpenCvRoot).Path
$OnnxRuntimeRoot = (Resolve-Path $OnnxRuntimeRoot).Path

Write-Host "Qt root:          $QtRoot"
Write-Host "OpenCV root:      $OpenCvRoot"
Write-Host "ONNX Runtime:     $OnnxRuntimeRoot"

# ── Configure + build ──────────────────────────────────────────────
# Note: every -D argument is wrapped in double quotes so PowerShell
# interpolates $vars. Bare `-DKEY=$var` is parsed as a parameter-style
# token and passed through literally — which previously poisoned the
# CMake cache with strings like "$BuildType" and broke the ninja build.
cmake -S $RootDir -B $BuildDir `
    -G $Generator `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DCMAKE_PREFIX_PATH=$QtRoot;$OpenCvRoot" `
    "-DONNXRUNTIME_ROOT=$OnnxRuntimeRoot" `
    "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

cmake --build $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }

# Ninja emits directly to $BuildDir; multi-config generators use $BuildDir\$BuildType.
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $BuildDir "$BuildType/FaceVeil.exe"
}
if (-not (Test-Path $ExePath)) {
    throw "FaceVeil.exe was not found after build (looked in $BuildDir and $BuildDir\$BuildType)."
}

# ── Assemble dist tree ─────────────────────────────────────────────
if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item $ExePath $DistDir

$windeployqt = Join-Path $QtRoot "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe was not found under $QtRoot\bin"
}
& $windeployqt --release --compiler-runtime (Join-Path $DistDir "FaceVeil.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed (exit $LASTEXITCODE)" }

# ── ONNX Runtime DLL ───────────────────────────────────────────────
$onnxCandidates = @(
    (Join-Path $OnnxRuntimeRoot "lib/onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "bin/onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "runtimes/win-x64/native/onnxruntime.dll")
)
$onnxDll = $onnxCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $onnxDll) {
    throw "onnxruntime.dll was not found. Checked:`n  $([string]::Join("`n  ", $onnxCandidates))"
}
Copy-Item $onnxDll $DistDir -Force
Write-Host "Bundled: $onnxDll"

# ── OpenCV DLLs ────────────────────────────────────────────────────
# Probe the usual layouts (kit root, parent, x64\...\bin, bin). Prefer
# opencv_world*.dll (monolithic) when present, otherwise fall back to the
# individual module DLLs. Exclude debug builds (*d.dll).
$searchRoots = @(
    $OpenCvRoot,
    (Join-Path $OpenCvRoot ".."),
    (Join-Path $OpenCvRoot "x64"),
    (Join-Path $OpenCvRoot "bin")
) | Where-Object { Test-Path $_ } | ForEach-Object { (Resolve-Path $_).Path } | Select-Object -Unique

$worldDlls = foreach ($root in $searchRoots) {
    Get-ChildItem -Path $root -Recurse -Filter "opencv_world*.dll" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch 'd\.dll$' }
}
$worldDlls = @($worldDlls) | Select-Object -Unique -First 1
if ($worldDlls) {
    Copy-Item $worldDlls.FullName $DistDir -Force
    Write-Host "Bundled: $($worldDlls.FullName)"
} else {
    $moduleDlls = foreach ($root in $searchRoots) {
        Get-ChildItem -Path $root -Recurse -Filter "opencv_*.dll" -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notmatch 'd\.dll$' }
    }
    $moduleDlls = @($moduleDlls) | Select-Object -Unique
    if (-not $moduleDlls) {
        throw "OpenCV DLLs were not found under $($searchRoots -join ', '). Set -OpenCvRoot to the OpenCV build root (e.g. C:\opencv\build)."
    }
    foreach ($dll in $moduleDlls) {
        Copy-Item $dll.FullName $DistDir -Force
    }
    Write-Host "Bundled $(@($moduleDlls).Count) OpenCV module DLL(s)."
}

# ── Third-party notices ────────────────────────────────────────────
Copy-Item (Join-Path $RootDir "THIRD_PARTY_NOTICES.txt") $DistDir -Force

Write-Host ""
Write-Host "✅ Packaged app: $DistDir"
Write-Host "   Run with:     $DistDir\FaceVeil.exe"
