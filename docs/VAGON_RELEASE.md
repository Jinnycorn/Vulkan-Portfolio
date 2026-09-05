# Vagon Windows Release

This repository builds a self-contained Windows x64 package for Vagon
Application Streaming.

## Automated build

1. Open **Actions** in GitHub.
2. Select **Build Vagon Release**.
3. Choose **Run workflow** on `portfolio-ex14-stable`.
4. Download the `VulkanPortfolio-Vagon-Windows-x64` artifact.
5. Upload `VulkanPortfolio-Vagon-Windows-x64.zip` to Vagon.
6. Select `x64/Release/Ex14_Bistro.exe` as the application executable.

The workflow checks out Git LFS assets, builds only `Ex14_Bistro` in Release
mode, gathers the required models, textures, shaders and runtime DLLs, validates
the package, then uploads the ZIP as a workflow artifact.

## Local build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with Desktop development with C++
- CMake 3.21 or newer
- vcpkg available through `VCPKG_INSTALLATION_ROOT`
- Git LFS

From PowerShell at the repository root:

```powershell
git lfs pull

cmake -S . -B build/vagon -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build/vagon --config Release --target Ex14_Bistro --parallel

./scripts/package_vagon_release.ps1 `
  -BuildDirectory "build/vagon" `
  -OutputDirectory "dist"
```

The resulting upload file is:

`dist/VulkanPortfolio-Vagon-Windows-x64.zip`

Do not flatten or rearrange the ZIP. The executable uses the preserved
`x64/Release` working directory and resolves assets at `../../assets`.

## Vagon runtime

Use a Windows stream machine with a Vulkan-capable GPU and an up-to-date graphics
driver. The Vulkan loader communicates with the GPU driver installed on the
stream machine.
