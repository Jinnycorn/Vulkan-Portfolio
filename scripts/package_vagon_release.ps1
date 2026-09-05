[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string] $BuildDirectory = "build/vagon",

    [Parameter(Mandatory = $false)]
    [string] $OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repoRoot $BuildDirectory
}
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $repoRoot $OutputDirectory
}

$stageRoot = Join-Path $outputRoot "VulkanPortfolio_Vagon"
$runDirectory = Join-Path $stageRoot "x64/Release"
$zipPath = Join-Path $outputRoot "VulkanPortfolio-Vagon-Windows-x64.zip"

if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

$exeCandidates = @(
    (Join-Path $repoRoot "x64/Release/Ex14_Bistro.exe"),
    (Join-Path $buildRoot "examples/Ex14_Bistro/Release/Ex14_Bistro.exe"),
    (Join-Path $buildRoot "Release/Ex14_Bistro.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $exePath) {
    $exePath = Get-ChildItem -LiteralPath $repoRoot -Filter "Ex14_Bistro.exe" -File -Recurse |
        Where-Object { $_.FullName -match "[\\/]Release[\\/]" } |
        Select-Object -ExpandProperty FullName -First 1
}
if (-not $exePath) {
    throw "Ex14_Bistro.exe was not found. Build the Release target first."
}

Copy-Item -LiteralPath $exePath -Destination $runDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot "examples/Ex14_Bistro/RenderGraph.json") -Destination $runDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot "examples/Ex14_Bistro/DescriptorPoolSize.txt") -Destination $runDirectory

$runtimeAssetPaths = @(
    "assets/shaders",
    "assets/characters/Leonard/Bboy Hip Hop Move.fbx",
    "assets/characters/Leonard/texture_0.png",
    "assets/characters/Leonard/texture_1.png",
    "assets/characters/Leonard/texture_2.png",
    "assets/characters/Leonard/texture_3.png",
    "assets/characters/Leonard/texture_4.png",
    "assets/models/AmazonLumberyardBistroMorganMcGuire/exterior_cache.bin",
    "assets/models/AmazonLumberyardBistroMorganMcGuire/LowRes",
    "assets/textures/golden_gate_hills_4k",
    "assets/Noto_Sans_KR/static/NotoSansKR-SemiBold.ttf"
)
foreach ($relativePath in $runtimeAssetPaths) {
    $source = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required runtime content is missing: $relativePath"
    }

    $destination = Join-Path $stageRoot (Split-Path $relativePath -Parent)
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
}

# Some historical LowRes files were accidentally stored as an LFS pointer inside
# an LFS object. Replace only those invalid image placeholders in the staged
# package so texture loading remains deterministic on a clean Vagon machine.
$neutralPng = [Convert]::FromBase64String(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
)
$replacedPointers = @()
Get-ChildItem -LiteralPath (Join-Path $stageRoot "assets") -File -Recurse |
    Where-Object { $_.Length -lt 512 } |
    ForEach-Object {
        $bytes = [IO.File]::ReadAllBytes($_.FullName)
        $text = [Text.Encoding]::ASCII.GetString($bytes)
        if ($text.StartsWith("version https://git-lfs.github.com/spec/v1")) {
            [IO.File]::WriteAllBytes($_.FullName, $neutralPng)
            $replacedPointers += $_.FullName.Substring($stageRoot.Length + 1)
        }
    }
if ($replacedPointers.Count -gt 0) {
    Write-Warning ("Replaced {0} malformed nested LFS image pointers with a neutral PNG:" -f $replacedPointers.Count)
    $replacedPointers | ForEach-Object { Write-Warning "  $_" }
}

$dllSearchRoots = @(
    (Join-Path $repoRoot "x64/Release"),
    (Join-Path $buildRoot "examples/Ex14_Bistro/Release"),
    (Join-Path $buildRoot "vcpkg_installed/x64-windows/bin"),
    (Join-Path $repoRoot "vcpkg_installed/x64-windows/bin")
)
$dlls = foreach ($searchRoot in $dllSearchRoots) {
    if (Test-Path -LiteralPath $searchRoot) {
        Get-ChildItem -LiteralPath $searchRoot -Filter "*.dll" -File
    }
}
$dlls | Sort-Object Name -Unique | Copy-Item -Destination $runDirectory -Force

if ($env:VCToolsRedistDir) {
    $vcRuntime = Join-Path $env:VCToolsRedistDir "x64/Microsoft.VC143.CRT"
    if (Test-Path -LiteralPath $vcRuntime) {
        Get-ChildItem -LiteralPath $vcRuntime -Filter "*.dll" -File |
            Copy-Item -Destination $runDirectory -Force
    }
}

$readme = @"
Vulkan Portfolio - Vagon Application Streaming build

Vagon executable:
  x64/Release/Ex14_Bistro.exe

Keep the ZIP directory structure unchanged. The executable expects the assets
folder to remain two levels above its working directory.

Controls:
  W/A/S/D : move
  Mouse   : look
  ESC     : close

Runtime requirement:
  A Vagon machine with a Vulkan-capable GPU and current graphics driver.

Packaging note:
  Historical LowRes image entries that contain nested Git LFS pointer text are
  replaced with a valid neutral 1x1 PNG so a clean deployment cannot fail while
  decoding those malformed source assets.
"@
Set-Content -LiteralPath (Join-Path $stageRoot "README-VAGON.txt") -Value $readme -Encoding UTF8

$requiredPackageFiles = @(
    "x64/Release/Ex14_Bistro.exe",
    "x64/Release/RenderGraph.json",
    "x64/Release/DescriptorPoolSize.txt",
    "assets/shaders/pbrForward.vert.spv",
    "assets/shaders/pbrDeferred.frag.spv",
    "assets/shaders/deferredLighting.comp.spv",
    "assets/shaders/post.frag.spv",
    "assets/characters/Leonard/Bboy Hip Hop Move.fbx",
    "assets/characters/Leonard/texture_0.png",
    "assets/characters/Leonard/texture_1.png",
    "assets/characters/Leonard/texture_2.png",
    "assets/characters/Leonard/texture_3.png",
    "assets/characters/Leonard/texture_4.png",
    "assets/models/AmazonLumberyardBistroMorganMcGuire/exterior_cache.bin",
    "assets/textures/golden_gate_hills_4k/specularGGX.ktx2",
    "assets/textures/golden_gate_hills_4k/diffuseLambertian.ktx2",
    "assets/textures/golden_gate_hills_4k/outputLUT.png",
    "assets/Noto_Sans_KR/static/NotoSansKR-SemiBold.ttf"
)
foreach ($relativePath in $requiredPackageFiles) {
    $path = Join-Path $stageRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Package validation failed; missing: $relativePath"
    }
    if ((Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Package validation failed; empty file: $relativePath"
    }
}

$largeLfsFiles = @(
    "assets/characters/Leonard/Bboy Hip Hop Move.fbx",
    "assets/models/AmazonLumberyardBistroMorganMcGuire/exterior_cache.bin",
    "assets/textures/golden_gate_hills_4k/specularGGX.ktx2",
    "assets/textures/golden_gate_hills_4k/diffuseLambertian.ktx2"
)
foreach ($relativePath in $largeLfsFiles) {
    $path = Join-Path $stageRoot $relativePath
    if ((Get-Item -LiteralPath $path).Length -lt 1024) {
        throw "Package validation failed; Git LFS content was not materialized: $relativePath"
    }
}

$remainingPointers = @()
Get-ChildItem -LiteralPath (Join-Path $stageRoot "assets") -File -Recurse |
    Where-Object { $_.Length -lt 512 } |
    ForEach-Object {
        $bytes = [IO.File]::ReadAllBytes($_.FullName)
        $text = [Text.Encoding]::ASCII.GetString($bytes)
        if ($text.StartsWith("version https://git-lfs.github.com/spec/v1")) {
            $remainingPointers += $_.FullName
        }
    }
if ($remainingPointers.Count -gt 0) {
    throw "Package validation failed; unresolved Git LFS pointers remain: $($remainingPointers -join ', ')"
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Compress-Archive -LiteralPath $stageRoot -DestinationPath $zipPath -CompressionLevel Optimal

$zip = Get-Item -LiteralPath $zipPath
$hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
Write-Host "Created: $($zip.FullName)"
Write-Host "Size: $($zip.Length) bytes"
Write-Host "SHA256: $($hash.Hash)"
