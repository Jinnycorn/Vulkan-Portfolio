param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$targetDir = Join-Path $repoRoot "assets\textures\golden_gate_hills_4k"
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

$lfsBaseUrl = "https://media.githubusercontent.com/media/KhronosGroup/glTF-Sample-Environments/main"
$rawBaseUrl = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Environments/main"
$files = @(
    @{
        Url = "$lfsBaseUrl/papermill/ggx/specular.ktx2"
        Name = "specularGGX.ktx2"
    },
    @{
        Url = "$lfsBaseUrl/papermill/lambertian/diffuse.ktx2"
        Name = "diffuseLambertian.ktx2"
    },
    @{
        Url = "$rawBaseUrl/outputLUT.png"
        Name = "outputLUT.png"
    }
)

function Test-Ktx2File {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path $Path)) {
        return $false
    }

    [byte[]]$expected = 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    [byte[]]$actual = @(Get-Content -Path $Path -Encoding Byte -TotalCount 12)
    if ($actual.Length -ne $expected.Length) {
        return $false
    }

    for ($index = 0; $index -lt $expected.Length; ++$index) {
        if ($actual[$index] -ne $expected[$index]) {
            return $false
        }
    }
    return $true
}

foreach ($file in $files) {
    $destination = Join-Path $targetDir $file.Name
    $isKtx2 = [IO.Path]::GetExtension($file.Name) -eq ".ktx2"
    $existingIsValid = (Test-Path $destination) -and
        (-not $isKtx2 -or (Test-Ktx2File -Path $destination))

    if ($existingIsValid -and -not $Force) {
        Write-Host "Already present: $($file.Name)"
        continue
    }

    if ((Test-Path $destination) -and -not $existingIsValid) {
        Write-Host "Replacing invalid Git LFS pointer or corrupt file: $($file.Name)"
    }

    $partial = "$destination.part"
    Remove-Item -Force -ErrorAction SilentlyContinue $partial
    Write-Host "Downloading $($file.Name) ..."
    Invoke-WebRequest -Uri $file.Url -OutFile $partial -UseBasicParsing

    if (-not (Test-Path $partial) -or (Get-Item $partial).Length -eq 0) {
        throw "Download failed: $($file.Url)"
    }
    if ($isKtx2 -and -not (Test-Ktx2File -Path $partial)) {
        Remove-Item -Force -ErrorAction SilentlyContinue $partial
        throw "Downloaded file is not a valid KTX2 texture: $($file.Url)"
    }

    Move-Item -Force $partial $destination
}

$attribution = @"
Papermill IBL environment assets
Source: https://github.com/KhronosGroup/glTF-Sample-Environments
Generated with: https://github.com/KhronosGroup/glTF-IBL-Sampler
Files are downloaded locally and are not committed to this repository.
"@
Set-Content -Path (Join-Path $targetDir "SOURCE.txt") -Value $attribution -Encoding UTF8

Write-Host ""
Write-Host "Engine IBL assets are ready:"
Write-Host "  $targetDir"
Write-Host ""
Write-Host "Run the complete post-processing window with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\run-example.ps1 Ex11_PostProcessing"
