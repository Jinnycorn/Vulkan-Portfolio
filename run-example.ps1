param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet(
        "Ex01_Context",
        "Ex02_Compute",
        "Ex03_Triangle13",
        "Ex04_Triangle11",
        "Ex05_ShaderReflection",
        "Ex06_PipelineFactory",
        "Ex07_OnDemandDescriptorPool",
        "Ex08_Swapchain",
        "Ex09_Gui",
        "Ex10_Skybox",
        "Ex11_PostProcessing",
        "Ex12_PBR",
        "Ex13_FBX",
        "Ex14_Bistro"
    )]
    [string]$Example,

    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$requirements = @{
    Ex02_Compute = @(
        "assets\image.jpg",
        "assets\shaders\test.comp.spv"
    )
    Ex03_Triangle13 = @(
        "assets\shaders\triangle.vert.spv",
        "assets\shaders\triangle.frag.spv"
    )
    Ex04_Triangle11 = @(
        "assets\shaders\triangle.vert.spv",
        "assets\shaders\triangle.frag.spv"
    )
    Ex05_ShaderReflection = @(
        "assets\shaders\test.comp.spv"
    )
    Ex06_PipelineFactory = @(
        "assets\shaders\triangle.vert.spv",
        "assets\shaders\triangle.frag.spv"
    )
    Ex07_OnDemandDescriptorPool = @(
        "assets\image.jpg",
        "assets\shaders\test.comp.spv"
    )
    Ex09_Gui = @(
        "assets\shaders\imgui.vert",
        "assets\shaders\imgui.frag"
    )
    Ex10_Skybox = @(
        "assets\textures\golden_gate_hills_4k\specularGGX.ktx2",
        "assets\textures\golden_gate_hills_4k\diffuseLambertian.ktx2",
        "assets\textures\golden_gate_hills_4k\outputLUT.png"
    )
    Ex11_PostProcessing = @(
        "assets\textures\golden_gate_hills_4k\specularGGX.ktx2",
        "assets\textures\golden_gate_hills_4k\diffuseLambertian.ktx2",
        "assets\textures\golden_gate_hills_4k\outputLUT.png"
    )
    Ex12_PBR = @(
        "assets\models\DamagedHelmet.glb",
        "assets\textures\golden_gate_hills_4k\specularGGX.ktx2",
        "assets\textures\golden_gate_hills_4k\diffuseLambertian.ktx2",
        "assets\textures\golden_gate_hills_4k\outputLUT.png"
    )
    Ex13_FBX = @(
        "assets\characters\Leonard\Leonard.fbx"
    )
    Ex14_Bistro = @(
        "assets\models\AmazonLumberyardBistroMorganMcGuire\exterior.obj",
        "assets\textures\golden_gate_hills_4k\specularGGX.ktx2",
        "assets\textures\golden_gate_hills_4k\diffuseLambertian.ktx2",
        "assets\textures\golden_gate_hills_4k\outputLUT.png"
    )
}

$missing = @()
if ($requirements.ContainsKey($Example)) {
    $missing = @($requirements[$Example] | Where-Object {
        -not (Test-Path (Join-Path $repoRoot $_))
    })
}
if ($missing.Count -gt 0) {
    Write-Host "Cannot run $Example because these runtime assets are not in the repository:" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    throw "Add the missing assets and run the command again. The executable itself can still be built."
}

$exeDir = Join-Path $repoRoot "x64\$Configuration"
$exe = Join-Path $exeDir "$Example.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe. Run .\build-all.ps1 first."
}

$previousLocation = Get-Location
try {
    Set-Location $exeDir
    if ($Example -eq "Ex14_Bistro") {
        $env:HLAB_DISABLE_IBL = "1"
        $env:HLAB_LOW_SPEC = "1"
        Write-Host "MX110 preset enabled: 512px materials, 75% internal resolution, 1024px shadows, low-cost SSAO."
    }
    Write-Host "Running $Example on the selected Vulkan GPU ..."
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "$Example exited with code $LASTEXITCODE."
    }
} finally {
    Set-Location $previousLocation
}
