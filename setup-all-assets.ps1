param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$assetsRoot = Join-Path $repoRoot "assets"
$modelsRoot = Join-Path $assetsRoot "models"
$charactersRoot = Join-Path $assetsRoot "characters\Leonard"
$downloadRoot = Join-Path $repoRoot ".tools\asset-downloads"
$bistroDownloads = Join-Path $downloadRoot "bistro"
$bistroModelRoot = Join-Path $modelsRoot "AmazonLumberyardBistroMorganMcGuire"

$driveName = ([IO.Path]::GetPathRoot($repoRoot)).Substring(0, 1)
$freeBytes = (Get-PSDrive -Name $driveName).Free
if ($freeBytes -lt 10GB) {
    throw "At least 10 GB of free disk space is required. Available: $([math]::Round($freeBytes / 1GB, 1)) GB."
}

New-Item -ItemType Directory -Force -Path $modelsRoot | Out-Null
New-Item -ItemType Directory -Force -Path $charactersRoot | Out-Null
New-Item -ItemType Directory -Force -Path $bistroDownloads | Out-Null
New-Item -ItemType Directory -Force -Path $bistroModelRoot | Out-Null

function Download-File {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if ((Test-Path $Destination) -and -not $Force) {
        Write-Host "Already present: $(Split-Path -Leaf $Destination)"
        return
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    $partial = "$Destination.part"
    if ($Force) {
        Remove-Item -Force -ErrorAction SilentlyContinue $Destination, $partial
    }

    Write-Host "Downloading $(Split-Path -Leaf $Destination) ..."
    & curl.exe -L --fail --retry 5 --retry-delay 3 --continue-at - --output $partial $Url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $partial) -or
        (Get-Item $partial).Length -eq 0) {
        throw "Download failed: $Url"
    }
    Move-Item -Force $partial $Destination
}

Write-Host "Preparing the Khronos IBL environment ..."
# PowerShell scripts do not set $LASTEXITCODE. With ErrorActionPreference
# set to Stop, any setup failure is propagated directly by the child script.
& (Join-Path $repoRoot "setup-engine-assets.ps1")

Write-Host ""
Write-Host "Downloading Khronos Damaged Helmet ..."
Download-File -Url "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb" -Destination (Join-Path $modelsRoot "DamagedHelmet.glb")

Write-Host ""
Write-Host "Downloading the public Assimp animated FBX fallback ..."
$publicFbx = Join-Path $downloadRoot "animation_with_skeleton.fbx"
Download-File -Url "https://raw.githubusercontent.com/assimp/assimp/master/test/models/FBX/animation_with_skeleton.fbx" -Destination $publicFbx
Copy-Item -Force $publicFbx (Join-Path $charactersRoot "Leonard.fbx")
Copy-Item -Force $publicFbx (Join-Path $charactersRoot "Bboy Hip Hop Move.fbx")

Write-Host ""
Write-Host "Downloading the complete Amazon Lumberyard Bistro exterior dataset ..."
$bistroBase = "https://raw.githubusercontent.com/jvm-graphics-labs/awesome-3d-meshes/master/McGuire/Amazon%20Lumberyard%20Bistro"
Download-File -Url "$bistroBase/Exterior.zip" -Destination (Join-Path $bistroDownloads "Exterior.zip")

$multipartSets = @(
    @{ Name = "BuildingTextures"; Count = 8 },
    @{ Name = "OtherTextures"; Count = 10 },
    @{ Name = "PropTextures"; Count = 7 }
)
foreach ($set in $multipartSets) {
    for ($part = 1; $part -le $set["Count"]; ++$part) {
        $suffix = $part.ToString("000")
        $name = "$($set.Name).7z.$suffix"
        Download-File -Url "$bistroBase/$name" -Destination (Join-Path $bistroDownloads $name)
    }
}

function Find-SevenZip {
    $candidates = @()

    # A 32-bit PowerShell process maps ProgramFiles to Program Files (x86).
    # ProgramW6432 always points at the native 64-bit Program Files directory.
    if ($env:ProgramW6432) {
        $candidates += Join-Path $env:ProgramW6432 "7-Zip\7z.exe"
    }
    if ($env:ProgramFiles) {
        $candidates += Join-Path $env:ProgramFiles "7-Zip\7z.exe"
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates += Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe"
    }
    if ($env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA "Programs\7-Zip\7z.exe"
        $candidates += Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\7z.exe"
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    if ($env:LOCALAPPDATA) {
        $packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
        if (Test-Path $packageRoot) {
            $packageExe = Get-ChildItem -Path $packageRoot -Filter "7z.exe" -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "7zip\.7zip" } |
                Select-Object -First 1
            if ($packageExe) {
                return $packageExe.FullName
            }
        }
    }

    return $null
}

$sevenZip = Find-SevenZip
if (-not $sevenZip) {
    Write-Host "Installing 7-Zip ..."
    & winget install --id 7zip.7zip --exact --accept-package-agreements --accept-source-agreements --silent

    # Some installers finish updating their files after winget returns.
    for ($attempt = 0; $attempt -lt 10 -and -not $sevenZip; ++$attempt) {
        Start-Sleep -Seconds 1
        $sevenZip = Find-SevenZip
    }
}
if (-not $sevenZip) {
    throw "7-Zip was installed but 7z.exe could not be located. Open a new PowerShell window and run this script again."
}
Write-Host "Using 7-Zip: $sevenZip"

Write-Host ""
Write-Host "Extracting Bistro geometry ..."
Expand-Archive -Path (Join-Path $bistroDownloads "Exterior.zip") -DestinationPath $bistroModelRoot -Force

foreach ($set in $multipartSets) {
    $firstPart = Join-Path $bistroDownloads "$($set.Name).7z.001"
    $outputDir = Join-Path $bistroModelRoot "LowRes\$($set.Name)"
    $legacyOutputDir = Join-Path $modelsRoot $set.Name

    # Versions before this fix extracted textures beside the Bistro directory.
    # Move those completed folders into the layout referenced by exterior.mtl.
    if ((Test-Path $legacyOutputDir) -and -not (Test-Path $outputDir)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputDir) | Out-Null
        Write-Host "Moving existing $($set.Name) into the Bistro LowRes directory ..."
        Move-Item -Path $legacyOutputDir -Destination $outputDir
        continue
    }

    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    Write-Host "Extracting $($set.Name) ..."
    & $sevenZip x $firstPart "-o$outputDir" -y
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract $($set.Name)."
    }
}

$sourceText = @"
Automatically downloaded runtime assets

IBL:
https://github.com/KhronosGroup/glTF-Sample-Environments

Damaged Helmet:
https://github.com/KhronosGroup/glTF-Sample-Assets

Animated FBX fallback:
https://github.com/assimp/assimp/blob/master/test/models/FBX/animation_with_skeleton.fbx
This public test asset replaces the unavailable Mixamo Leonard files.

Amazon Lumberyard Bistro:
https://github.com/jvm-graphics-labs/awesome-3d-meshes/tree/master/McGuire/Amazon%20Lumberyard%20Bistro
Amazon Lumberyard Bistro is CC BY 4.0.

These files are downloaded locally and remain excluded from git.
"@
Set-Content -Path (Join-Path $assetsRoot "DOWNLOADED_ASSETS.txt") -Value $sourceText -Encoding UTF8

$requiredFiles = @(
    (Join-Path $modelsRoot "DamagedHelmet.glb"),
    (Join-Path $charactersRoot "Leonard.fbx"),
    (Join-Path $charactersRoot "Bboy Hip Hop Move.fbx"),
    (Join-Path $bistroModelRoot "exterior.obj"),
    (Join-Path $bistroModelRoot "LowRes\BuildingTextures"),
    (Join-Path $bistroModelRoot "LowRes\OtherTextures"),
    (Join-Path $bistroModelRoot "LowRes\PropTextures")
)
$missing = @($requiredFiles | Where-Object { -not (Test-Path $_) })
if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Host "Missing after setup: $_" -ForegroundColor Red }
    throw "The complete asset setup did not finish correctly."
}

Write-Host ""
Write-Host "All example runtime assets are ready."
Write-Host "Run all examples with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\run-all-examples.ps1"
