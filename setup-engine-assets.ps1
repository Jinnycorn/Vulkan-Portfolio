param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$targetDir = Join-Path $repoRoot "assets\textures\golden_gate_hills_4k"
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

$baseUrl = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Environments/main"
$files = @(
    @{
        Url = "$baseUrl/papermill/ggx/specular.ktx2"
        Name = "specularGGX.ktx2"
    },
    @{
        Url = "$baseUrl/papermill/lambertian/diffuse.ktx2"
        Name = "diffuseLambertian.ktx2"
    },
    @{
        Url = "$baseUrl/outputLUT.png"
        Name = "outputLUT.png"
    }
)

foreach ($file in $files) {
    $destination = Join-Path $targetDir $file.Name
    if ((Test-Path $destination) -and -not $Force) {
        Write-Host "Already present: $($file.Name)"
        continue
    }

    Write-Host "Downloading $($file.Name) ..."
    Invoke-WebRequest -Uri $file.Url -OutFile $destination -UseBasicParsing

    if (-not (Test-Path $destination) -or
        (Get-Item $destination).Length -eq 0) {
        throw "Download failed: $($file.Url)"
    }
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
