param(
    [string]$BuildRoot = $env:BIC_BUILD_ROOT,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Portable
)
$ErrorActionPreference = 'Stop'
if ($Portable -and $Configuration -ne 'Release') {
    throw 'Portable packaging requires -Configuration Release; Debug is for development only.'
}
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
    $configurationFolder = $Configuration.ToLowerInvariant()
    $preset = 'windows-' + $configurationFolder
    $configureArgs = @('--preset', $preset)
    # Preset хранит сборку в <BuildRoot>/<конфигурация>. После переноса исходников старый
    # CMAKE_HOME_DIRECTORY несовместим с новым корнем: пересоздаём конфигурацию
    # штатным --fresh (CMake >= 3.24; проект требует >= 3.25), без удаления всей сборки.
    $configurationRoot = Join-Path $BuildRoot $configurationFolder
    $cachePath = Join-Path $configurationRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $sourceEntry = Get-Content -LiteralPath $cachePath |
            Select-String -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' |
            Select-Object -First 1
        if ($null -ne $sourceEntry) {
            # CMake записывает прямые слеши; сравниваем полные пути без учёта
            # регистра Windows, разделителей и завершающего слеша.
            $cachedSource = [IO.Path]::GetFullPath($sourceEntry.Matches[0].Groups[1].Value).Replace('\', '/').TrimEnd('/')
            $currentSource = [IO.Path]::GetFullPath($PSScriptRoot).Replace('\', '/').TrimEnd('/')
            if (-not [string]::Equals($cachedSource, $currentSource, [StringComparison]::OrdinalIgnoreCase)) {
                Write-Host "Source directory changed: $cachedSource -> $currentSource"
                Write-Host 'Recreating CMake configuration (--fresh); custom cache options return to preset defaults.'
                $configureArgs += '--fresh'
            }
        }
    }
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if ($Portable) {
        # Пакет формируется в отдельной папке; сборка/кэш в него не попадают.
        cmake --install $configurationRoot --prefix (Join-Path $BuildRoot 'portable')
        if ($LASTEXITCODE -ne 0) { throw 'Portable packaging failed.' }
    }
} finally { Pop-Location }
