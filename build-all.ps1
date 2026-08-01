param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($env:OS -ne "Windows_NT") {
    throw "This build script supports Windows only."
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "_build-all"
$toolsDir = Join-Path $repoRoot ".tools"
$vcpkgRoot = Join-Path $toolsDir "vcpkg"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"

$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machinePath;$userPath"

foreach ($command in @("git", "cmake")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "'$command' is missing. Run .\setup-basic.ps1 first."
    }
}

$programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio 2022 C++ Build Tools were not found. Run .\setup-basic.ps1 first."
}
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw "The Visual Studio C++ workload is missing. Run .\setup-basic.ps1 first."
}

$vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Process")
if ([string]::IsNullOrWhiteSpace($vulkanSdk)) {
    $vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "User")
}
if ([string]::IsNullOrWhiteSpace($vulkanSdk)) {
    $vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Machine")
}
if ([string]::IsNullOrWhiteSpace($vulkanSdk)) {
    $sdkRoot = "C:\VulkanSDK"
    if (Test-Path $sdkRoot) {
        $latestSdk = Get-ChildItem $sdkRoot -Directory |
            Sort-Object { [version]($_.Name -replace "[^0-9.]", "") } -Descending |
            Select-Object -First 1
        if ($latestSdk) {
            $vulkanSdk = $latestSdk.FullName
        }
    }
}
if ([string]::IsNullOrWhiteSpace($vulkanSdk) -or
    -not (Test-Path (Join-Path $vulkanSdk "Include\vulkan\vulkan.h"))) {
    throw "Vulkan SDK was not found. Run .\setup-basic.ps1 first."
}
$env:VULKAN_SDK = $vulkanSdk
$env:Path = "$(Join-Path $vulkanSdk "Bin");$env:Path"

New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
    Write-Host "Cloning vcpkg ..."
    & git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone vcpkg."
    }
}

if (-not (Test-Path $vcpkgExe)) {
    Write-Host "Bootstrapping vcpkg ..."
    & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to bootstrap vcpkg."
    }
}

$env:VCPKG_ROOT = $vcpkgRoot
$cacheDir = Join-Path $toolsDir "vcpkg-cache"
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
$env:VCPKG_DEFAULT_BINARY_CACHE = $cacheDir

Write-Host ""
Write-Host "Installing the complete dependency manifest ..."
Push-Location $repoRoot
try {
    & $vcpkgExe install --triplet x64-windows "--x-manifest-root=$repoRoot"
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg dependency installation failed."
    }
} finally {
    Pop-Location
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing the previous full-build directory ..."
    Remove-Item -Recurse -Force $buildDir
}

$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
Write-Host ""
Write-Host "Configuring all Vulkan examples ..."
$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows",
    "-DHLAB_BASIC_ONLY=OFF"
)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host ""
Write-Host "Building every target ($Configuration) ..."
& cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "The full repository build failed."
}

$outputDir = Join-Path $repoRoot "x64\$Configuration"
$executables = Get-ChildItem $outputDir -Filter "Ex*.exe" -ErrorAction SilentlyContinue |
    Sort-Object Name

Write-Host ""
Write-Host "Full build completed. Executables:"
$executables | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host ""
Write-Host "Run an example with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\run-example.ps1 Ex03_Triangle13"
