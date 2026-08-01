param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($env:OS -ne "Windows_NT") {
    throw "This setup script supports Windows only."
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "_build-basic"

function Refresh-Path {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machinePath;$userPath"
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$Override = ""
    )

    if ($SkipInstall) {
        throw "Required package '$Id' is missing. Run again without -SkipInstall."
    }

    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is required. Install or update 'App Installer' from Microsoft Store."
    }

    $arguments = @(
        "install", "--id", $Id, "--exact",
        "--accept-package-agreements", "--accept-source-agreements",
        "--silent"
    )
    if ($Override) {
        $arguments += @("--override", $Override)
    }

    Write-Host "Installing $Id ..."
    & winget @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install $Id (exit code $LASTEXITCODE)."
    }
    Refresh-Path
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Install-WingetPackage -Id "Kitware.CMake"
}

$programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
$hasCppTools = $false
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $hasCppTools = -not [string]::IsNullOrWhiteSpace($vsInstall)
}
if (-not $hasCppTools) {
    Install-WingetPackage -Id "Microsoft.VisualStudio.2022.BuildTools" -Override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
}

$vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Process")
if ([string]::IsNullOrWhiteSpace($vulkanSdk)) {
    $vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "User")
}
if ([string]::IsNullOrWhiteSpace($vulkanSdk)) {
    $vulkanSdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Machine")
}

if ([string]::IsNullOrWhiteSpace($vulkanSdk) -or -not (Test-Path (Join-Path $vulkanSdk "Include\vulkan\vulkan.h"))) {
    Install-WingetPackage -Id "KhronosGroup.VulkanSDK"

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

if ([string]::IsNullOrWhiteSpace($vulkanSdk) -or -not (Test-Path (Join-Path $vulkanSdk "Include\vulkan\vulkan.h"))) {
    throw "Vulkan SDK was not found. Install it, reopen PowerShell, and run this script again."
}

$env:VULKAN_SDK = $vulkanSdk
$env:Path = "$(Join-Path $vulkanSdk "Bin");$env:Path"

Write-Host ""
Write-Host "Configuring asset-free Vulkan smoke test ..."
& cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64 -DHLAB_BASIC_ONLY=ON
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host ""
Write-Host "Building Basic_Context ($Configuration) ..."
& cmake --build $buildDir --config $Configuration --target Basic_Context --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

$exe = Join-Path $buildDir "$Configuration\Basic_Context.exe"
if (-not (Test-Path $exe)) {
    throw "Executable was not created: $exe"
}

Write-Host ""
Write-Host "Running Basic_Context ..."
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Basic_Context failed (exit code $LASTEXITCODE)."
}

Write-Host ""
Write-Host "Success: Vulkan SDK and GPU initialization are working."
