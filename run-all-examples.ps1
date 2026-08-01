param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$ContinueOnError
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$examples = @(
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
)

foreach ($example in $examples) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Starting $example"
    Write-Host "Close its window to continue to the next graphical example."
    Write-Host "============================================================"

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "run-example.ps1") $example -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        Write-Host "$example failed with exit code $LASTEXITCODE." -ForegroundColor Red
        if (-not $ContinueOnError) {
            throw "Stopped at $example. Use -ContinueOnError to test the remaining examples."
        }
    }
}

Write-Host ""
Write-Host "All 14 examples finished."
