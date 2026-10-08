<#
.SYNOPSIS
    Builds the Qt Advanced Docking System (ADS) for one Qt kit into this repository's build/ folder.

.DESCRIPTION
    MC Workbench (tools/mc_workbench, SPEC-gui-tool.md) links ADS as a DLL that is never part of the
    repository. The prebuilt package on this PC is for Qt 6 / MSVC only; this script builds ADS from
    its source with the chosen Qt kit (typically MinGW, or a Qt 5.15 kit), Debug and Release, and
    installs both into one prefix inside build/:
      - Qt 6 kit: build/ads-<kit>/install      (for example build/ads-mingw_64/install)
      - Qt 5 kit: build/ads-qt5-<kit>/install  (for example build/ads-qt5-msvc2019_64/install)
    where <kit> is the folder name of the Qt kit. The Qt major comes from the kit
    (lib/cmake/Qt6/Qt6Config.cmake or lib/cmake/Qt5/Qt5Config.cmake); for a Qt 5 kit the script
    passes QT_VERSION_MAJOR=5 to ADS. A Qt 6 kit is configured exactly as before.

    Nothing outside the project folder is written: the source folder is only read, the build trees
    are build-debug and build-release beside the install prefix. Both configurations share one
    prefix, so a CMake or qmake consumer finds qtadvanceddocking-qt<major>d and
    qtadvanceddocking-qt<major> there.

    At the end the script prints the MC_ADS_DIR to use:
      - CMake: -DMC_ADS_DIR=<install>  (or the environment variable MC_ADS_DIR)
      - qmake: MC_ADS_DIR (Qt 6 MSVC kits), MC_ADS_DIR_MINGW (Qt 6 MinGW kits) or MC_ADS_DIR_QT5
        (Qt 5 kits) in the git-ignored mc_local.pri

.PARAMETER QtDir
    The Qt kit to build for: a Qt 6 kit or a Qt 5.15 kit. Default C:/Qt/6.11.1/mingw_64. A kit
    folder whose name contains "mingw" is built with the MinGW compiler found under C:\Qt\Tools; any
    other kit with the Visual Studio developer shell (scripts/vsdev.ps1).

.PARAMETER SourceDir
    ADS source folder (CMake only), read only. Default: the environment variable MC_ADS_SOURCE_DIR,
    else C:/build_packages/Qt-Advanced-Docking-System.

.PARAMETER Jobs
    Parallel jobs. Default: the number of logical processors.

.PARAMETER Clean
    Delete build/ads-<kit> (Qt 5: build/ads-qt5-<kit>) first (only that folder) and build from
    scratch.

.PARAMETER VcVarsVer
    MSVC kits only: the MSVC toolset to load (vsdev.ps1 -VcVarsVer). Empty means the installation's
    default toolset for a Qt 6 kit and 14.44 for a Qt 5 kit: Qt 5.15 builds use a toolset older than
    14.50 (VS 2026), as scripts/check.ps1 does. Run from a fresh PowerShell: vsdev.ps1 refuses a
    shell that already has another toolset loaded.

.EXAMPLE
    scripts/build-ads.ps1
    scripts/build-ads.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64
    scripts/build-ads.ps1 -QtDir C:/Qt/5.15.0/msvc2019_64
#>
[CmdletBinding()]
param(
    [string]$QtDir = 'C:/Qt/6.11.1/mingw_64',
    [string]$SourceDir = '',
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Clean,
    [string]$VcVarsVer = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) {
    if ($env:MC_ADS_SOURCE_DIR) { $SourceDir = $env:MC_ADS_SOURCE_DIR }
    else { $SourceDir = 'C:/build_packages/Qt-Advanced-Docking-System' }
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir 'CMakeLists.txt'))) {
    throw "build-ads: no CMakeLists.txt in the ADS source folder '$SourceDir' (set -SourceDir or MC_ADS_SOURCE_DIR)."
}
if (Test-Path -LiteralPath (Join-Path $QtDir 'lib/cmake/Qt6/Qt6Config.cmake')) {
    $qtMajor = 6
} elseif (Test-Path -LiteralPath (Join-Path $QtDir 'lib/cmake/Qt5/Qt5Config.cmake')) {
    $qtMajor = 5
} else {
    throw "build-ads: '$QtDir' is not a Qt 6 or Qt 5 kit (lib/cmake/Qt6/Qt6Config.cmake and lib/cmake/Qt5/Qt5Config.cmake are missing)."
}

$kit = Split-Path -Leaf ($QtDir.TrimEnd('/', '\'))
# Qt 6 kits keep their historical folder; Qt 5 kits get a "qt5-" prefix so that a kit folder name
# shared by both majors (for example mingw_64) never mixes the two builds.
if ($qtMajor -eq 5) { $adsRoot = Join-Path $repoRoot "build/ads-qt5-$kit" }
else { $adsRoot = Join-Path $repoRoot "build/ads-$kit" }
$install = Join-Path $adsRoot 'install'

# Extra ADS configure arguments. A Qt 6 kit gets none (unchanged command line: ADS looks for Qt6
# first); a Qt 5 kit pins ADS to Qt 5 so it never picks up a Qt 6 found elsewhere.
$majorArgs = @()
if ($qtMajor -eq 5) { $majorArgs = @('-DQT_VERSION_MAJOR=5') }

# Only ever delete inside this project's build/ folder.
if ($Clean -and (Test-Path -LiteralPath $adsRoot)) {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')) + [IO.Path]::DirectorySeparatorChar
    if (-not ([IO.Path]::GetFullPath($adsRoot) + [IO.Path]::DirectorySeparatorChar).StartsWith($buildRoot)) {
        throw "build-ads: refusing to delete '$adsRoot' outside the project's build folder."
    }
    Remove-Item -LiteralPath $adsRoot -Recurse -Force
}

# Toolchain.
$cmakeDir = 'C:\Qt\Tools\CMake_64\bin'
$ninjaDir = 'C:\Qt\Tools\Ninja'
if ($kit -match 'mingw') {
    $mingwBin = Get-ChildItem -Directory 'C:\Qt\Tools' -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1
    if (-not $mingwBin) { throw "build-ads: no MinGW toolchain under C:\Qt\Tools (mingw*_64)." }
    $env:PATH = "$($mingwBin.FullName)\bin;$cmakeDir;$ninjaDir;$env:PATH"
} else {
    # Qt 5.15 builds use an MSVC toolset older than 14.50 (VS 2026), as check.ps1 does: 14.44
    # unless -VcVarsVer names another one.
    if (-not $VcVarsVer -and $qtMajor -eq 5) { $VcVarsVer = '14.44' }
    $adsVcVarsVer = $VcVarsVer
    . (Join-Path $PSScriptRoot 'vsdev.ps1') -VcVarsVer $adsVcVarsVer
    if ($adsVcVarsVer -and -not ("$env:VCToolsVersion".StartsWith($adsVcVarsVer))) {
        throw "build-ads: could not load the MSVC $adsVcVarsVer toolset (loaded: '$env:VCToolsVersion')."
    }
    Write-Host "== build-ads: MSVC toolset $env:VCToolsVersion =="
    $env:PATH = "$cmakeDir;$ninjaDir;$env:PATH"
}

New-Item -ItemType Directory -Force -Path $adsRoot | Out-Null

foreach ($config in @('Debug', 'Release')) {
    $buildDir = Join-Path $adsRoot ("build-" + $config.ToLower())
    Write-Host "== build-ads: $kit (Qt $qtMajor) $config =="
    & cmake -S $SourceDir -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$config" `
        "-DCMAKE_PREFIX_PATH=$QtDir" "-DCMAKE_INSTALL_PREFIX=$install" -DBUILD_EXAMPLES=OFF @majorArgs
    if ($LASTEXITCODE -ne 0) { throw "build-ads: configure ($config) failed with exit code $LASTEXITCODE" }
    & cmake --build $buildDir --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build-ads: build ($config) failed with exit code $LASTEXITCODE" }
    & cmake --install $buildDir
    if ($LASTEXITCODE -ne 0) { throw "build-ads: install ($config) failed with exit code $LASTEXITCODE" }
}

$installFull = (Resolve-Path -LiteralPath $install).Path.Replace('\', '/')
Write-Host ''
Write-Host "== build-ads: done =="
Write-Host "MC_ADS_DIR = $installFull"
Write-Host "  CMake : -DMC_ADS_DIR=$installFull   (or set the environment variable MC_ADS_DIR)"
if ($qtMajor -eq 5) {
    Write-Host "  qmake : MC_ADS_DIR_QT5 = $installFull   (in mc_local.pri)"
} elseif ($kit -match 'mingw') {
    Write-Host "  qmake : MC_ADS_DIR_MINGW = $installFull   (in mc_local.pri)"
} else {
    Write-Host "  qmake : MC_ADS_DIR = $installFull   (in mc_local.pri)"
}
