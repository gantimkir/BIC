param([string]$BuildRoot = $env:BIC_BUILD_ROOT)
$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) {
    $BuildRoot = 'D:\Dev\Build\BIC-Z7'
}
if (-not [IO.Path]::IsPathRooted($BuildRoot)) { throw 'BuildRoot must be an absolute local path.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer / vswhere not found.' }
$installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
if (-not $installation) { throw 'Install Visual Studio 2022 C++ Build Tools with CMake.' }
& (Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:BIC_BUILD_ROOT = $BuildRoot
Push-Location $PSScriptRoot
try {
    cmake --preset windows-debug
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build --preset windows-debug
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} finally { Pop-Location }
